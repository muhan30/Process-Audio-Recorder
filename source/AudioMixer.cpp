#include "AudioMixer.h"

#include <algorithm>

void AudioMixer::PushMicData(const BYTE* data, DWORD size)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_micBuffer.insert(m_micBuffer.end(), data, data + size);
    // 水位控制：超限丢最旧数据，防止延迟累积
    if (m_micBuffer.size() > kMaxBuffer)
    {
        m_micBuffer.erase(m_micBuffer.begin(), m_micBuffer.begin() + (m_micBuffer.size() - kMaxBuffer));
    }
}

// 对 16bit PCM 块逐样本乘增益并饱和钳制（原地）
static void ApplyGainInPlace(std::vector<BYTE>& chunk, float gain)
{
    if (gain == 1.0f)
    {
        return;
    }
    auto* samples = reinterpret_cast<INT16*>(chunk.data());
    size_t count = chunk.size() / sizeof(INT16);
    for (size_t i = 0; i < count; i++)
    {
        int v = static_cast<int>(static_cast<float>(samples[i]) * gain);
        v = (std::max)(-32768, (std::min)(32767, v));
        samples[i] = static_cast<INT16>(v);
    }
}

std::vector<BYTE> AudioMixer::ApplySystemGain(std::vector<BYTE>&& chunk)
{
    ApplyGainInPlace(chunk, m_systemGain.load());
    return std::move(chunk);
}

std::vector<BYTE> AudioMixer::MixWithLoopback(std::vector<BYTE>&& loopbackChunk)
{
    // 先对系统声道应用增益
    ApplyGainInPlace(loopbackChunk, m_systemGain.load());

    // 取等量麦克风数据（不足部分保持为零 = 静音）
    std::vector<BYTE> micChunk(loopbackChunk.size(), 0);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t take = (std::min)(m_micBuffer.size(), micChunk.size());
        take -= take % 4;  // 对齐到立体声帧边界（2 声道 x 2 字节），防左右声道错位
        std::copy(m_micBuffer.begin(), m_micBuffer.begin() + take, micChunk.begin());
        m_micBuffer.erase(m_micBuffer.begin(), m_micBuffer.begin() + take);
    }

    // 逐样本饱和叠加（16bit 有符号；麦克风路先乘增益，钳制防爆音）
    const float micGain = m_micGain.load();
    auto* dst = reinterpret_cast<INT16*>(loopbackChunk.data());
    auto* mic = reinterpret_cast<const INT16*>(micChunk.data());
    size_t samples = loopbackChunk.size() / sizeof(INT16);
    for (size_t i = 0; i < samples; i++)
    {
        int boosted = static_cast<int>(static_cast<float>(mic[i]) * micGain);
        int mixed = static_cast<int>(dst[i]) + boosted;
        mixed = (std::max)(-32768, (std::min)(32767, mixed));
        dst[i] = static_cast<INT16>(mixed);
    }
    return std::move(loopbackChunk);
}
