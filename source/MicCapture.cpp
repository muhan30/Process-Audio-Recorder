#include "MicCapture.h"

#include <vector>
#include <wil/result.h>

#define BITS_PER_BYTE 8

MicCapture::~MicCapture()
{
    Stop();
}

HRESULT MicCapture::Initialize(DataCallback callback)
{
    m_callback = std::move(callback);

    // 目标格式与环回捕获一致，设备差异交给 AUTOCONVERTPCM 自动转换
    m_format.wFormatTag = WAVE_FORMAT_PCM;
    m_format.nChannels = 2;
    m_format.nSamplesPerSec = 44100;
    m_format.wBitsPerSample = 16;
    m_format.nBlockAlign = m_format.nChannels * m_format.wBitsPerSample / BITS_PER_BYTE;
    m_format.nAvgBytesPerSec = m_format.nSamplesPerSec * m_format.nBlockAlign;
    m_format.cbSize = 0;

    // 默认输入设备（麦克风）
    wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
    RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
    wil::com_ptr_nothrow<IMMDevice> device;
    RETURN_IF_FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device));
    RETURN_IF_FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_audioClient));

    RETURN_IF_FAILED(m_sampleReadyEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        200000,  // 缓冲区 200ms
        0,
        &m_format,
        nullptr));
    RETURN_IF_FAILED(m_audioClient->SetEventHandle(m_sampleReadyEvent.get()));
    RETURN_IF_FAILED(m_audioClient->GetService(IID_PPV_ARGS(&m_captureClient)));
    return S_OK;
}

HRESULT MicCapture::Start()
{
    RETURN_HR_IF(E_NOT_VALID_STATE, !m_audioClient);
    RETURN_IF_FAILED(m_audioClient->Start());
    m_running = true;
    m_thread = std::thread(&MicCapture::CaptureThreadProc, this);
    return S_OK;
}

void MicCapture::Stop()
{
    m_running = false;
    if (m_sampleReadyEvent)
    {
        m_sampleReadyEvent.SetEvent();  // 唤醒采集线程以便退出
    }
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    if (m_audioClient)
    {
        m_audioClient->Stop();
    }
}

void MicCapture::CaptureThreadProc()
{
    while (m_running)
    {
        // 等新数据（200ms 超时兜底，防事件丢失卡死）
        WaitForSingleObject(m_sampleReadyEvent.get(), 200);
        if (!m_running)
        {
            break;
        }

        UINT32 framesAvailable = 0;
        while (SUCCEEDED(m_captureClient->GetNextPacketSize(&framesAvailable)) && framesAvailable > 0)
        {
            BYTE* data = nullptr;
            DWORD flags = 0;
            if (FAILED(m_captureClient->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr)))
            {
                break;
            }
            DWORD bytes = framesAvailable * m_format.nBlockAlign;
            if (m_callback && bytes > 0)
            {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    // 静音标志：数据无效，送零
                    std::vector<BYTE> silence(bytes, 0);
                    m_callback(silence.data(), bytes);
                }
                else
                {
                    m_callback(data, bytes);
                }
            }
            m_captureClient->ReleaseBuffer(framesAvailable);
        }
    }
}
