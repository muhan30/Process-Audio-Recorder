/*
 * WAV 输出实现：RIFF 头占位 → 追加 PCM → Finalize 回填大小。
 * 逻辑自 CLoopbackCapture::CreateWAVFile / FixWAVHeader 迁入，行为不变。
 */
#pragma once

#include "AudioSink.h"
#include <wil/resource.h>

class WavSink : public AudioSink
{
public:
    HRESULT Initialize(PCWSTR filePath, const WAVEFORMATEX& format) override;
    HRESULT WriteChunk(const BYTE* data, DWORD size) override;
    HRESULT Finalize() override;
    UINT64 BytesWritten() const override { return m_cbDataSize; }

private:
    wil::unique_hfile m_hFile;     // 输出文件句柄
    DWORD m_cbHeaderSize = 0;      // WAV 文件头大小
    DWORD m_cbDataSize = 0;        // 已写入的音频数据大小
    bool m_finalized = false;      // 防止重复收尾
};
