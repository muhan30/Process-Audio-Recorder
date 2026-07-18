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
    // 设置麦克风增益（1.0 = 原样，2.0 = 放大一倍；调用时机不限）
    void SetMicGain(float gain) { m_micGain = gain; }

    // 设置系统声音增益（1.0 = 原样，对环回数据缩放后再混音或直通）
    void SetSystemGain(float gain) { m_systemGain = gain; }

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

    // 水位上限：约 2 秒（44100 帧/秒 x 4 字节/帧 x 2 秒）
    static constexpr size_t kMaxBuffer = 44100 * 4 * 2;
};
