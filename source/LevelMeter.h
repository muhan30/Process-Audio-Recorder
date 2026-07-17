/*
 * 电平计算：对 16bit PCM 块取峰值，归一化为 0-100。
 * 音频线程写、显示线程读，用原子变量传递。
 */
#pragma once

#include <Windows.h>
#include <atomic>

// 双通道电平状态
struct LevelState
{
    std::atomic<int> systemLevel{ 0 };  // 系统/目标软件声音电平（0-100）
    std::atomic<int> micLevel{ 0 };     // 麦克风电平（0-100）
};

// 计算一块 16bit PCM 的峰值电平，返回 0-100
inline int CalcPeakLevel(const BYTE* data, DWORD size)
{
    const INT16* samples = reinterpret_cast<const INT16*>(data);
    size_t count = size / sizeof(INT16);
    int peak = 0;
    for (size_t i = 0; i < count; i++)
    {
        int v = samples[i];
        if (v < 0)
        {
            v = -v;
        }
        if (v > peak)
        {
            peak = v;
        }
    }
    return peak * 100 / 32768;
}
