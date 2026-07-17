/*
 * 音频输出接口：捕获引擎只管产出 PCM 数据块，
 * 具体写成 WAV 还是 M4A 由注入的 Sink 实现决定。
 * 约定：Initialize → 多次 WriteChunk（单线程调用）→ Finalize（必调，负责收尾与资源释放）
 */
#pragma once

#include <Windows.h>
#include <mmreg.h>

class AudioSink
{
public:
    virtual ~AudioSink() = default;

    // 打开输出文件并做好写入准备（format 为送入的 PCM 格式）
    virtual HRESULT Initialize(PCWSTR filePath, const WAVEFORMATEX& format) = 0;

    // 写入一块 PCM 数据（由写入线程串行调用）
    virtual HRESULT WriteChunk(const BYTE* data, DWORD size) = 0;

    // 收尾：补文件头/结束编码，无论成败都会被调用一次
    virtual HRESULT Finalize() = 0;

    // 已写入的音频数据字节数（供进度显示）
    virtual UINT64 BytesWritten() const = 0;
};
