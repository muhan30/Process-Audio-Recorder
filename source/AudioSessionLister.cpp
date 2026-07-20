#include "AudioSessionLister.h"

#include <mmdeviceapi.h>   // IMMDeviceEnumerator
#include <audiopolicy.h>   // IAudioSessionManager2 / IAudioSessionControl2 / IAudioSessionNotification
#include <endpointvolume.h>// IAudioMeterInformation
#include <algorithm>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <wrl/implements.h>

using namespace Microsoft::WRL;

// 由 PID 取进程 exe 名（失败返回空串）
static std::wstring GetProcessNameByPid(DWORD pid)
{
    wil::unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process)
    {
        return L"";
    }
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(process.get(), 0, path, &size))
    {
        return L"";
    }
    std::wstring fullPath(path);
    size_t pos = fullPath.find_last_of(L'\\');
    return (pos == std::wstring::npos) ? fullPath : fullPath.substr(pos + 1);
}

HRESULT ListAudioSessions(std::vector<AudioSessionInfo>& sessions)
{
    sessions.clear();

    // 1. 取默认播放设备
    wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
    RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
    wil::com_ptr_nothrow<IMMDevice> device;
    RETURN_IF_FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));

    // 2. 激活会话管理器
    wil::com_ptr_nothrow<IAudioSessionManager2> sessionManager;
    RETURN_IF_FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, sessionManager.put_void()));

    // 3. 枚举所有会话
    wil::com_ptr_nothrow<IAudioSessionEnumerator> sessionEnum;
    RETURN_IF_FAILED(sessionManager->GetSessionEnumerator(&sessionEnum));
    int count = 0;
    RETURN_IF_FAILED(sessionEnum->GetCount(&count));

    for (int i = 0; i < count; i++)
    {
        wil::com_ptr_nothrow<IAudioSessionControl> control;
        if (FAILED(sessionEnum->GetSession(i, &control)))
        {
            continue;
        }
        wil::com_ptr_nothrow<IAudioSessionControl2> control2;
        if (FAILED(control->QueryInterface(IID_PPV_ARGS(&control2))))
        {
            continue;
        }

        AudioSessionInfo info;
        if (FAILED(control2->GetProcessId(&info.processId)))
        {
            continue;
        }
        info.isSystemSounds = (control2->IsSystemSoundsSession() == S_OK);

        // 用峰值电平判断是否真正在发声（比会话状态延迟小得多）
        wil::com_ptr_nothrow<IAudioMeterInformation> meter;
        info.peakValue = 0.0f;
        if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioMeterInformation), meter.put_void())))
        {
            meter->GetPeakValue(&info.peakValue);
        }
        info.isActive = (info.peakValue > 0.001f);  // 峰值 > 0 才是真在响

        info.processName = GetProcessNameByPid(info.processId);
        if (info.processName.empty())
        {
            if (info.isSystemSounds)
            {
                info.processName = L"[System Sounds]";
            }
            else
            {
                continue;  // 拿不到名字且非系统音，跳过
            }
        }
        sessions.push_back(std::move(info));
    }

    // 4. 正在发声的排前面（稳定排序保持系统枚举顺序）
    std::stable_sort(sessions.begin(), sessions.end(),
        [](const AudioSessionInfo& a, const AudioSessionInfo& b)
        {
            return a.isActive > b.isActive;
        });
    return S_OK;
}

// ---- IAudioSessionNotification 事件驱动（参考 win-capture-audio session-monitor） ----

class SessionNotifier
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase, IAudioSessionNotification>
{
    HWND m_hWnd = nullptr;
    UINT m_msg = 0;
public:
    SessionNotifier(HWND hWnd, UINT msg) : m_hWnd(hWnd), m_msg(msg) {}

    STDMETHOD(OnSessionCreated)(IAudioSessionControl* session) override
    {
        session->AddRef();  // win-capture-audio 做法：AddRef 后把指针抛给消息处理方
        PostMessageW(m_hWnd, m_msg, reinterpret_cast<WPARAM>(session), 0);
        return S_OK;
    }
};

static wil::com_ptr<SessionNotifier> g_sessionNotifier;
static wil::com_ptr<IAudioSessionManager2> g_sessionManager2;

HRESULT RegisterSessionNotification(HWND hWnd, UINT msg)
{
    // 1. 取默认播放设备
    wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
    RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator)));
    wil::com_ptr_nothrow<IMMDevice> device;
    RETURN_IF_FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));

    // 2. 激活会话管理器
    RETURN_IF_FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
        g_sessionManager2.put_void()));

    // 3. 注册通知
    g_sessionNotifier = Make<SessionNotifier>(hWnd, msg);
    RETURN_IF_FAILED(g_sessionManager2->RegisterSessionNotification(g_sessionNotifier.get()));
    return S_OK;
}

void UnregisterSessionNotification()
{
    if (g_sessionManager2 && g_sessionNotifier)
        g_sessionManager2->UnregisterSessionNotification(g_sessionNotifier.get());
    g_sessionNotifier.reset();
    g_sessionManager2.reset();
}
