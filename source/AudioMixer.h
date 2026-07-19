/*
 * 双流混音器：以环回流为主时钟，麦克风数据入缓冲随取随混。
 * 同步策略：麦克风缓冲不足补静音、超水位丢最旧（约 2 秒上限），
 * 时钟漂移被水位吸收，瞬时不同步 < 20ms，通话场景无感。
 * 支持麦克风增益（软件放大，带饱和保护），用于平衡两路响度。
 */
#pragma once

#include <Windows.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

class AudioMixer
{
public:
    // 二阶巴特沃斯低通滤波器（Biquad, Direct Form 1），双声道独立处理。
    // 注意：系数和状态为 public 以支持原地处理，但请勿手动修改系数——
    // Process() 的旁路优化依赖系数精确匹配直通值 (1,0,0,0,0)，手动改系数会打破此假设。
    struct BiquadStereo
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;  // 归一化系数（直通默认 y[n]=x[n]）
        float x1L = 0, x2L = 0, y1L = 0, y2L = 0;       // 左声道状态
        float x1R = 0, x2R = 0, y1R = 0, y2R = 0;       // 右声道状态

        // 按截止频率重新计算 Butterworth 系数。fcHz=0 时设为直通。
        void SetCutoff(float fcHz, float sampleRate = 44100.0f);

        // 原地处理立体声 16bit PCM（count = INT16 个数，必须为偶数）
        void Process(INT16* samples, size_t count);

        // 清零全部状态（改截止频率或录音开始时调用，防旧状态引发振荡）
        void Reset();
    };

    // 设置麦克风增益（1.0 = 原样，2.0 = 放大一倍；调用时机不限）
    void SetMicGain(float gain) { m_micGain = gain; }

    // 设置系统声音增益（1.0 = 原样，对环回数据缩放后再混音或直通）
    void SetSystemGain(float gain) { m_systemGain = gain; }

    // 设置低通滤波器截止频率（Hz），0 = 关闭直通
    void SetLowpassCutoff(float fcHz);

    // 设置噪声门阈值（dBFS），0 = 关闭。典型值 -45（桌面麦克风），-60 极安静，-20 嘈杂。
    void SetNoiseGateThreshold(float dBFS);

    // 查询当前增益值（供 GUI 设置对话框初始化）
    float GetMicGain() const { return m_micGain.load(); }
    float GetSystemGain() const { return m_systemGain.load(); }

    // 麦克风数据入缓冲（麦克风采集线程调用）
    void PushMicData(const BYTE* data, DWORD size);

    // 用环回块驱动混音：取等量麦克风数据饱和叠加，返回混合块（环回回调线程调用）
    std::vector<BYTE> MixWithLoopback(std::vector<BYTE>&& loopbackChunk);

    // 纯系统增益（麦克风未启用时的直通路径）
    std::vector<BYTE> ApplySystemGain(std::vector<BYTE>&& chunk);

private:
    std::deque<BYTE> m_micBuffer;         // 麦克风字节缓冲（16bit 立体声样本流）
    std::mutex m_mutex;                   // 缓冲锁（两线程并发访问）
    std::atomic<float> m_micGain{ 1.0f };   // 麦克风增益系数
    std::atomic<float> m_systemGain{ 1.0f };// 系统声音增益系数
    BiquadStereo m_filter;                   // 低通滤波器（系数+状态）
    std::atomic<float> m_lowpassCutoff{ 0 }; // 截止频率 Hz，0=关闭直通
    std::atomic<float> m_noiseGateThreshold{ 0 }; // 噪声门阈值 dBFS，0=关闭

    // 水位上限：约 2 秒（44100 帧/秒 x 4 字节/帧 x 2 秒）
    static constexpr size_t kMaxBuffer = 44100 * 4 * 2;
};
