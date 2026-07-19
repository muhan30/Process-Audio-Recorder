/*
 * M4A（AAC）输出实现：IMFSinkWriter + Windows 内置 AAC 编码器。
 * 输入 PCM（与捕获格式一致），输出 128kbps AAC / MP4 容器。
 * 30 分钟录音约 28MB，体积为 WAV 的 1/10。
 */
#pragma once

#include "AudioSink.h"
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wil/com.h>
#include <string>

class M4aSink : public AudioSink
{
public:
    ~M4aSink() override;
    HRESULT Initialize(PCWSTR filePath, const WAVEFORMATEX& format) override;
    HRESULT WriteChunk(const BYTE* data, DWORD size) override;
    HRESULT Finalize() override;
    UINT64 BytesWritten() const override;  // 读磁盘实际文件大小（非 PCM 输入量）

private:
    wil::com_ptr_nothrow<IMFSinkWriter> m_writer;  // MF 写入器（含编码）
    DWORD m_streamIndex = 0;      // 音频流索引
    UINT64 m_cbPcmWritten = 0;    // 已送入编码器的 PCM 字节数
    DWORD m_avgBytesPerSec = 0;   // PCM 每秒字节数（时间戳换算用）
    std::wstring m_filePath;      // 输出文件路径（BytesWritten 读磁盘用）
    bool m_mfStarted = false;     // MFStartup 配对标志
    bool m_finalized = false;     // 防止重复收尾
};
