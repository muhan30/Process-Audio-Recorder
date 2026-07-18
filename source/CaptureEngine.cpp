#include "CaptureEngine.h"

#include "LoopbackCapture.h"
#include "MicCapture.h"
#include "WavSink.h"
#include "M4aSink.h"
#include <thread>

struct CaptureEngine::Impl
{
    CLoopbackCapture loopback;
    MicCapture mic;
    AudioMixer mixer;
    LevelState levelState;

    std::unique_ptr<AudioSink> sink;
    std::wstring outputPath;
    std::wstring outputFormat{ L"m4a" };
    bool micEnabled = false;

    StatusCallback onStatus;
    StoppedCallback onStopped;
    std::atomic<bool> isCapturing{ false };
    std::thread statusThread;
};

CaptureEngine::CaptureEngine() : m_impl(std::make_unique<Impl>()) {}

CaptureEngine::~CaptureEngine()
{
    if (m_impl->isCapturing)
        Stop();
}

void CaptureEngine::SetOutputPath(const std::wstring& path)   { m_impl->outputPath = path; }
void CaptureEngine::SetOutputFormat(const std::wstring& fmt)   { m_impl->outputFormat = fmt; }
void CaptureEngine::SetMicEnabled(bool enabled)                { m_impl->micEnabled = enabled; }
void CaptureEngine::SetMicGain(float gain)                     { m_impl->mixer.SetMicGain(gain); }
void CaptureEngine::SetSystemGain(float gain)                  { m_impl->mixer.SetSystemGain(gain); }
float CaptureEngine::GetMicGain() const                        { return m_impl->mixer.GetMicGain(); }
float CaptureEngine::GetSystemGain() const                     { return m_impl->mixer.GetSystemGain(); }

HRESULT CaptureEngine::Start(DWORD pid, bool includeTree,
                              StatusCallback onStatus, StoppedCallback onStopped)
{
    auto& impl = *m_impl;
    impl.onStatus = std::move(onStatus);
    impl.onStopped = std::move(onStopped);

    // 创建 Sink
    if (impl.outputFormat == L"wav")
        impl.sink = std::make_unique<WavSink>();
    else
        impl.sink = std::make_unique<M4aSink>();
    impl.loopback.SetAudioSink(impl.sink.get());

    // 统一分流（电平 + 混音/直通）
    impl.loopback.SetDataTap([&impl](std::vector<BYTE>&& chunk) {
        impl.levelState.systemLevel = CalcPeakLevel(chunk.data(), static_cast<DWORD>(chunk.size()));
        if (impl.micEnabled)
            impl.loopback.EnqueueAudioData(impl.mixer.MixWithLoopback(std::move(chunk)));
        else
            impl.loopback.EnqueueAudioData(impl.mixer.ApplySystemGain(std::move(chunk)));
    });

    if (impl.micEnabled)
    {
        RETURN_IF_FAILED(impl.mic.Initialize([&impl](const BYTE* data, DWORD size) {
            impl.levelState.micLevel = CalcPeakLevel(data, size);
            impl.mixer.PushMicData(data, size);
        }));
    }

    // 启动环回捕获
    RETURN_IF_FAILED(impl.loopback.StartCaptureAsync(pid, includeTree, impl.outputPath.c_str()));
    if (impl.micEnabled)
        RETURN_IF_FAILED(impl.mic.Start());

    impl.isCapturing = true;

    // 状态上报线程（200ms 一次）
    impl.statusThread = std::thread([&impl, start = std::chrono::steady_clock::now()]() mutable {
        while (impl.isCapturing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!impl.isCapturing) break;
            CaptureStatus st;
            st.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start);
            st.bytesWritten = impl.sink ? impl.sink->BytesWritten() : 0;
            st.systemLevel = impl.levelState.systemLevel;
            st.micLevel = impl.levelState.micLevel;
            st.micEnabled = impl.micEnabled;
            if (impl.onStatus) impl.onStatus(st);
        }
    });
    return S_OK;
}

HRESULT CaptureEngine::StartGlobal(StatusCallback onStatus, StoppedCallback onStopped)
{
    auto& impl = *m_impl;
    impl.onStatus = std::move(onStatus);
    impl.onStopped = std::move(onStopped);

    if (impl.outputFormat == L"wav")
        impl.sink = std::make_unique<WavSink>();
    else
        impl.sink = std::make_unique<M4aSink>();
    impl.loopback.SetAudioSink(impl.sink.get());
    impl.loopback.SetDataTap([&impl](std::vector<BYTE>&& chunk) {
        impl.levelState.systemLevel = CalcPeakLevel(chunk.data(), static_cast<DWORD>(chunk.size()));
        impl.loopback.EnqueueAudioData(impl.mixer.ApplySystemGain(std::move(chunk)));
    });

    RETURN_IF_FAILED(impl.loopback.StartGlobalCaptureAsync(impl.outputPath.c_str()));
    impl.isCapturing = true;

    impl.statusThread = std::thread([&impl, start = std::chrono::steady_clock::now()]() mutable {
        while (impl.isCapturing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!impl.isCapturing) break;
            CaptureStatus st;
            st.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start);
            st.bytesWritten = impl.sink ? impl.sink->BytesWritten() : 0;
            st.systemLevel = impl.levelState.systemLevel;
            st.micLevel = 0;
            st.micEnabled = false;
            if (impl.onStatus) impl.onStatus(st);
        }
    });
    return S_OK;
}

void CaptureEngine::Stop()
{
    auto& impl = *m_impl;
    if (!impl.isCapturing) return;

    if (impl.micEnabled)
        impl.mic.Stop();
    impl.loopback.StopCaptureAsync();
    impl.isCapturing = false;
    if (impl.statusThread.joinable())
        impl.statusThread.join();
    if (impl.onStopped)
        impl.onStopped(S_OK);
}
