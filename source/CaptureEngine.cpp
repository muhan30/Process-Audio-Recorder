#include "CaptureEngine.h"

#include "LoopbackCapture.h"
#include "MicCapture.h"
#include "WavSink.h"
#include "M4aSink.h"
#include <thread>
#include <mutex>

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

    StoppedCallback onStopped;
    std::atomic<bool> isCapturing{ false };
    std::thread statusThread;
    std::chrono::steady_clock::time_point startTime;

    // 线程安全的状态快照：statusThread 写，GUI 线程 GetStatus() 读
    mutable std::mutex statusMutex;
    CaptureStatus latestStatus;
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
void CaptureEngine::SetLowpassCutoff(float fcHz)                { m_impl->mixer.SetLowpassCutoff(fcHz); }
float CaptureEngine::GetMicGain() const                        { return m_impl->mixer.GetMicGain(); }
float CaptureEngine::GetSystemGain() const                     { return m_impl->mixer.GetSystemGain(); }

CaptureStatus CaptureEngine::GetStatus() const
{
    std::lock_guard<std::mutex> lock(m_impl->statusMutex);
    return m_impl->latestStatus;
}

HRESULT CaptureEngine::Start(DWORD pid, bool includeTree, StoppedCallback onStopped)
{
    auto& impl = *m_impl;
    impl.onStopped = std::move(onStopped);

    if (impl.outputFormat == L"wav")
        impl.sink = std::make_unique<WavSink>();
    else
        impl.sink = std::make_unique<M4aSink>();
    impl.loopback.SetAudioSink(impl.sink.get());

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

    RETURN_IF_FAILED(impl.loopback.StartCaptureAsync(pid, includeTree, impl.outputPath.c_str()));
    if (impl.micEnabled)
        RETURN_IF_FAILED(impl.mic.Start());

    impl.isCapturing = true;
    impl.startTime = std::chrono::steady_clock::now();

    // 状态线程：只写原子/锁保护数据，不碰 GUI
    impl.statusThread = std::thread([&impl]() {
        while (impl.isCapturing)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!impl.isCapturing) break;

            CaptureStatus st;
            st.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - impl.startTime);
            st.bytesWritten = impl.sink ? impl.sink->BytesWritten() : 0;
            st.systemLevel = impl.levelState.systemLevel;
            st.micLevel = impl.levelState.micLevel;
            st.micEnabled = impl.micEnabled;
            {
                std::lock_guard<std::mutex> lock(impl.statusMutex);
                impl.latestStatus = st;
            }
        }
    });
    return S_OK;
}

HRESULT CaptureEngine::StartGlobal(StoppedCallback onStopped)
{
    auto& impl = *m_impl;
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
    impl.startTime = std::chrono::steady_clock::now();

    impl.statusThread = std::thread([&impl]() {
        while (impl.isCapturing)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!impl.isCapturing) break;

            CaptureStatus st;
            st.elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - impl.startTime);
            st.bytesWritten = impl.sink ? impl.sink->BytesWritten() : 0;
            st.systemLevel = impl.levelState.systemLevel;
            st.micLevel = 0;
            st.micEnabled = false;
            {
                std::lock_guard<std::mutex> lock(impl.statusMutex);
                impl.latestStatus = st;
            }
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
        impl.onStopped(S_OK);  // PostMessage 线程安全
}
