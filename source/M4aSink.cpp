#include "M4aSink.h"

#include <mfapi.h>
#include <wil/result.h>
#include <sys/stat.h>

M4aSink::~M4aSink()
{
    Finalize();  // 兜底：调用方漏调时也保证资源释放
}

HRESULT M4aSink::Initialize(PCWSTR filePath, const WAVEFORMATEX& format)
{
    // SinkWriter 需要完整 MF 平台（引擎用的是 MFSTARTUP_LITE，计数式叠加一次）
    RETURN_IF_FAILED(MFStartup(MF_VERSION));
    m_mfStarted = true;
    m_avgBytesPerSec = format.nAvgBytesPerSec;
    m_filePath = filePath;

    // fMP4 容器：分段写入，进程意外终止时文件仍可播放到中断点（崩溃保护）
    wil::com_ptr_nothrow<IMFAttributes> attrs;
    RETURN_IF_FAILED(MFCreateAttributes(&attrs, 1));
    RETURN_IF_FAILED(attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_FMPEG4));
    RETURN_IF_FAILED(MFCreateSinkWriterFromURL(filePath, nullptr, attrs.get(), &m_writer));

    // 输出流：AAC，44.1kHz/立体声/128kbps（16000 字节每秒）
    wil::com_ptr_nothrow<IMFMediaType> outType;
    RETURN_IF_FAILED(MFCreateMediaType(&outType));
    RETURN_IF_FAILED(outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    RETURN_IF_FAILED(outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
    RETURN_IF_FAILED(outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, format.nSamplesPerSec));
    RETURN_IF_FAILED(outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, format.nChannels));
    RETURN_IF_FAILED(outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    RETURN_IF_FAILED(outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000));
    RETURN_IF_FAILED(m_writer->AddStream(outType.get(), &m_streamIndex));

    // 输入流：PCM，与捕获格式完全一致
    wil::com_ptr_nothrow<IMFMediaType> inType;
    RETURN_IF_FAILED(MFCreateMediaType(&inType));
    RETURN_IF_FAILED(inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    RETURN_IF_FAILED(inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, format.nSamplesPerSec));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, format.nChannels));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, format.wBitsPerSample));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, format.nBlockAlign));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, format.nAvgBytesPerSec));
    RETURN_IF_FAILED(m_writer->SetInputMediaType(m_streamIndex, inType.get(), nullptr));

    RETURN_IF_FAILED(m_writer->BeginWriting());
    return S_OK;
}

HRESULT M4aSink::WriteChunk(const BYTE* data, DWORD size)
{
    RETURN_HR_IF(E_NOT_VALID_STATE, !m_writer);
    if (size == 0)
    {
        return S_OK;
    }

    // PCM 块包装成 IMFSample
    wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
    RETURN_IF_FAILED(MFCreateMemoryBuffer(size, &buffer));
    BYTE* dst = nullptr;
    RETURN_IF_FAILED(buffer->Lock(&dst, nullptr, nullptr));
    memcpy(dst, data, size);
    buffer->Unlock();
    RETURN_IF_FAILED(buffer->SetCurrentLength(size));

    wil::com_ptr_nothrow<IMFSample> sample;
    RETURN_IF_FAILED(MFCreateSample(&sample));
    RETURN_IF_FAILED(sample->AddBuffer(buffer.get()));

    // 时间戳按累计 PCM 字节数换算（单位 100 纳秒）
    const LONGLONG hnsPerSec = 10000000;
    LONGLONG rtStart = static_cast<LONGLONG>(m_cbPcmWritten) * hnsPerSec / m_avgBytesPerSec;
    LONGLONG rtDuration = static_cast<LONGLONG>(size) * hnsPerSec / m_avgBytesPerSec;
    RETURN_IF_FAILED(sample->SetSampleTime(rtStart));
    RETURN_IF_FAILED(sample->SetSampleDuration(rtDuration));

    RETURN_IF_FAILED(m_writer->WriteSample(m_streamIndex, sample.get()));
    m_cbPcmWritten += size;
    return S_OK;
}

UINT64 M4aSink::BytesWritten() const
{
    // M4A 是压缩格式，PCM 输入量 ≠ 磁盘文件大小。优先读磁盘实际大小。
    if (m_filePath.empty()) return 0;
    struct __stat64 st;
    if (_wstat64(m_filePath.c_str(), &st) == 0 && st.st_size > 0)
        return static_cast<UINT64>(st.st_size);
    // stat 失败或文件尚未刷盘时，用 PCM 输入量 × 压缩比估算（AAC 128kbps ≈ 16000/avgBytesPerSec）
    if (m_avgBytesPerSec > 0)
        return static_cast<UINT64>(static_cast<double>(m_cbPcmWritten) * 16000.0 / m_avgBytesPerSec);
    return m_cbPcmWritten;  // 最终 fallback
}

HRESULT M4aSink::Finalize()
{
    if (m_finalized)
    {
        return S_OK;
    }
    m_finalized = true;

    HRESULT hr = S_OK;
    if (m_writer)
    {
        hr = m_writer->Finalize();  // 冲刷编码器并写 MP4 索引（moov）
        m_writer.reset();
    }
    if (m_mfStarted)
    {
        MFShutdown();
        m_mfStarted = false;
    }
    return hr;
}
