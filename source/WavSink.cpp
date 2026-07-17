#include "WavSink.h"

#include <mfapi.h>       // FCC 宏
#include <wil/result.h>

// 创建 WAV 文件并写入文件头（大小字段先占位为 0）
HRESULT WavSink::Initialize(PCWSTR filePath, const WAVEFORMATEX& format)
{
    m_hFile.reset(CreateFile(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    RETURN_LAST_ERROR_IF(!m_hFile);

    // RIFF 和 fmt 块头
    DWORD header[] = { FCC('RIFF'), 0, FCC('WAVE'), FCC('fmt '), sizeof(WAVEFORMATEX) };
    DWORD dwBytesWritten = 0;
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), header, sizeof(header), &dwBytesWritten, NULL));
    m_cbHeaderSize += dwBytesWritten;

    // 音频格式信息
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &format, sizeof(format), &dwBytesWritten, NULL));
    m_cbHeaderSize += dwBytesWritten;

    // data 块头
    DWORD data[] = { FCC('data'), 0 };
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), data, sizeof(data), &dwBytesWritten, NULL));
    m_cbHeaderSize += dwBytesWritten;

    return S_OK;
}

HRESULT WavSink::WriteChunk(const BYTE* data, DWORD size)
{
    // K6 保护：WAV 格式上限 4GB，逼近上限时报错停录，防止大小字段回绕
    RETURN_HR_IF(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), m_cbDataSize > 0xFFFFFFFFu - size - 1024);

    DWORD dwBytesWritten = 0;
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), data, size, &dwBytesWritten, NULL));
    m_cbDataSize += dwBytesWritten;
    return S_OK;
}

// 回填 data 大小与 RIFF 总大小
HRESULT WavSink::Finalize()
{
    if (m_finalized || !m_hFile)
    {
        return S_OK;
    }
    m_finalized = true;

    DWORD dwPtr = SetFilePointer(m_hFile.get(), m_cbHeaderSize - sizeof(DWORD), NULL, FILE_BEGIN);
    RETURN_LAST_ERROR_IF(INVALID_SET_FILE_POINTER == dwPtr);
    DWORD dwBytesWritten = 0;
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &m_cbDataSize, sizeof(DWORD), &dwBytesWritten, NULL));

    RETURN_LAST_ERROR_IF(INVALID_SET_FILE_POINTER == SetFilePointer(m_hFile.get(), sizeof(DWORD), NULL, FILE_BEGIN));
    DWORD cbTotalSize = m_cbDataSize + m_cbHeaderSize - 8;
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &cbTotalSize, sizeof(DWORD), &dwBytesWritten, NULL));

    RETURN_IF_WIN32_BOOL_FALSE(FlushFileBuffers(m_hFile.get()));
    return S_OK;
}
