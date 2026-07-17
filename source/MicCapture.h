/*
 * 麦克风采集模块：WASAPI 事件驱动捕获默认输入设备。
 * 输出统一为 44.1kHz/16bit/立体声（AUTOCONVERTPCM 负责重采样与声道转换），
 * 与环回捕获格式一致，便于混音。
 * 线程模型：专用采集线程等待事件（比 MF 工作队列简单，采集一路足够）。
 */
#pragma once

#include <AudioClient.h>
#include <mmdeviceapi.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <atomic>
#include <functional>
#include <thread>

class MicCapture
{
public:
    // 音频数据回调：每采到一块 PCM 调用一次（在采集线程上执行，须快速返回）
    using DataCallback = std::function<void(const BYTE* data, DWORD size)>;

    ~MicCapture();

    // 打开默认麦克风并配置格式（须先完成 COM 初始化）
    HRESULT Initialize(DataCallback callback);

    // 开始采集（启动采集线程）
    HRESULT Start();

    // 停止采集（等待线程退出，幂等）
    void Stop();

    const WAVEFORMATEX& Format() const { return m_format; }

private:
    void CaptureThreadProc();  // 采集线程主循环

    wil::com_ptr_nothrow<IAudioClient> m_audioClient;
    wil::com_ptr_nothrow<IAudioCaptureClient> m_captureClient;
    wil::unique_event_nothrow m_sampleReadyEvent;  // 数据就绪事件
    WAVEFORMATEX m_format{};
    DataCallback m_callback;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
};
