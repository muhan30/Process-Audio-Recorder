/*
 * CaptureEngine —— GUI 与录音引擎的唯一接口。
 * 封装环回捕获、麦克风采集、混音、电平反馈的全部状态管理，
 * 表现层（GUI/CLI）只需调用此类的方法，无需接触底层模块。
 */
#pragma once

#include "AudioMixer.h"
#include "LevelMeter.h"
#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <atomic>

// 供 GUI 消费的录音状态快照
struct CaptureStatus
{
    std::chrono::seconds elapsed{ 0 };
    UINT64 bytesWritten = 0;
    int systemLevel = 0;
    int micLevel = 0;
    bool micEnabled = false;
};

class CaptureEngine
{
public:
    using StatusCallback = std::function<void(const CaptureStatus&)>;
    using StoppedCallback = std::function<void(HRESULT)>;

    CaptureEngine();
    ~CaptureEngine();

    // 设置输出参数（Start 之前调用）
    void SetOutputPath(const std::wstring& path);
    void SetOutputFormat(const std::wstring& fmt);  // "m4a" 或 "wav"
    void SetMicEnabled(bool enabled);
    void SetMicGain(float gain);
    void SetSystemGain(float gain);

    // ---- 查询当前增益（供设置对话框初始化） ----
    float GetMicGain() const;
    float GetSystemGain() const;

    // ---- 捕获控制 ----
    // Start 异步返回，成功后通过 onStatus 定期推送状态，停止后通过 onStopped 通知
    HRESULT Start(DWORD processId, bool includeProcessTree,
                  StatusCallback onStatus, StoppedCallback onStopped);
    HRESULT StartGlobal(StatusCallback onStatus, StoppedCallback onStopped);

    // Stop 阻塞等待捕获线程完全退出
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
