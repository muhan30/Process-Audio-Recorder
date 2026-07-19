#include "AudioMixer.h"

#include <algorithm>
#include <cmath>

// ---- BiquadStereo ----

void AudioMixer::BiquadStereo::SetCutoff(float fcHz, float sampleRate)
{
    Reset();  // 改截止频率必须清状态，否则旧状态可能引发振荡

    if (fcHz <= 0.0f || fcHz >= sampleRate * 0.49f)
    {
        // 直通：y[n] = x[n]
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
        return;
    }

    constexpr float PI = 3.14159265f;
    constexpr float Q = 0.70710678f;  // 1/sqrt(2)，巴特沃斯最平坦响应

    float omega = 2.0f * PI * fcHz / sampleRate;
    float cosW = cosf(omega);
    float sinW = sinf(omega);
    float alpha = sinW / (2.0f * Q);

    // Bilinear transform → 未归一化系数
    float b0_raw = (1.0f - cosW) * 0.5f;
    float b1_raw = 1.0f - cosW;
    float b2_raw = (1.0f - cosW) * 0.5f;
    float a0_raw = 1.0f + alpha;
    float a1_raw = -2.0f * cosW;
    float a2_raw = 1.0f - alpha;

    // 归一化
    b0 = b0_raw / a0_raw;
    b1 = b1_raw / a0_raw;
    b2 = b2_raw / a0_raw;
    a1 = a1_raw / a0_raw;
    a2 = a2_raw / a0_raw;
}

void AudioMixer::BiquadStereo::Process(INT16* samples, size_t count)
{
    // 直通时跳过（零开销）
    if (b0 == 1.0f && b1 == 0.0f && b2 == 0.0f && a1 == 0.0f && a2 == 0.0f)
        return;

    for (size_t i = 0; i < count; i += 2)
    {
        float xL = static_cast<float>(samples[i]);
        float xR = static_cast<float>(samples[i + 1]);

        float yL = b0 * xL + b1 * x1L + b2 * x2L - a1 * y1L - a2 * y2L;
        float yR = b0 * xR + b1 * x1R + b2 * x2R - a1 * y1R - a2 * y2R;

        // 饱和钳制到 16bit
        int iL = static_cast<int>(yL);
        int iR = static_cast<int>(yR);
        if (iL < -32768) iL = -32768; else if (iL > 32767) iL = 32767;
        if (iR < -32768) iR = -32768; else if (iR > 32767) iR = 32767;
        samples[i]     = static_cast<INT16>(iL);
        samples[i + 1] = static_cast<INT16>(iR);

        // 状态推移
        x2L = x1L; x1L = xL;
        x2R = x1R; x1R = xR;
        y2L = y1L; y1L = yL;
        y2R = y1R; y1R = yR;
    }
}

void AudioMixer::BiquadStereo::Reset()
{
    x1L = x2L = y1L = y2L = 0;
    x1R = x2R = y1R = y2R = 0;
}

// ---- AudioMixer ----

void AudioMixer::SetLowpassCutoff(float fcHz)
{
    // 先更新系数和状态，再设标志位——防止采集线程在标志位已更新但系数未更新时进入 Process()
    m_filter.SetCutoff(fcHz);
    m_lowpassCutoff = fcHz;
}

void AudioMixer::SetNoiseGateThreshold(float dBFS)
{
    m_noiseGateThreshold = dBFS;
}

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

    // 噪声门：不说话时麦克风信号整块置零，消除全频段底噪
    float gateThreshold = m_noiseGateThreshold.load();
    if (gateThreshold < 0.0f)  // 负值 = 启用，0 = 关闭
    {
        auto* micSamples = reinterpret_cast<const INT16*>(micChunk.data());
        size_t sampleCount = micChunk.size() / sizeof(INT16);
        float sumSq = 0.0f;
        for (size_t i = 0; i < sampleCount; i++)
        {
            float s = static_cast<float>(micSamples[i]);
            sumSq += s * s;
        }
        float rms = sqrtf(sumSq / static_cast<float>(sampleCount));
        // RMS → dBFS：20*log10(rms/32768)，rms≈0 时 dBFS→-∞
        float dbfs = (rms > 0.5f) ? 20.0f * log10f(rms / 32768.0f) : -100.0f;
        if (dbfs < gateThreshold)
        {
            // 低于阈值 → 整块静音，跳过混音（滤波和增益都不需要）
            std::fill(micChunk.begin(), micChunk.end(), static_cast<BYTE>(0));
        }
    }

    // 低通滤波：滤除麦克风高频噪声（电流嘶声）
    if (m_lowpassCutoff.load() > 0.0f)
    {
        m_filter.Process(reinterpret_cast<INT16*>(micChunk.data()), micChunk.size() / sizeof(INT16));
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
