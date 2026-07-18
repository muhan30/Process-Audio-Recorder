#include "SettingsDialog.h"
#include <CommCtrl.h>

static SettingsData* g_pData = nullptr;
static HWND g_hSysEdit, g_hMicEdit;
static bool g_result = false;

void SettingsDialog::LoadFromIni(SettingsData& data)
{
    WCHAR path[MAX_PATH];
    GetModuleFileName(nullptr, path, MAX_PATH);
    WCHAR* last = wcsrchr(path, L'\\');
    if (last) *(last + 1) = L'\0';
    std::wstring iniPath = std::wstring(path) + L"ProcessAudioRecorder.ini";

    WCHAR buf[MAX_PATH];
    GetPrivateProfileString(L"Settings", L"SavePath", L"recordings", buf, MAX_PATH, iniPath.c_str());
    data.savePath = buf;
    GetPrivateProfileString(L"Settings", L"OutputFormat", L"m4a", buf, MAX_PATH, iniPath.c_str());
    data.outputFormat = buf;
    data.sysGain = (float)GetPrivateProfileInt(L"Settings", L"SysGain", 20, iniPath.c_str()) / 10.0f;
    data.micGain = (float)GetPrivateProfileInt(L"Settings", L"MicGain", 40, iniPath.c_str()) / 10.0f;
}

void SettingsDialog::SaveToIni(const SettingsData& data)
{
    WCHAR path[MAX_PATH];
    GetModuleFileName(nullptr, path, MAX_PATH);
    WCHAR* last = wcsrchr(path, L'\\');
    if (last) *(last + 1) = L'\0';
    std::wstring iniPath = std::wstring(path) + L"ProcessAudioRecorder.ini";

    WritePrivateProfileString(L"Settings", L"SavePath", data.savePath.c_str(), iniPath.c_str());
    WritePrivateProfileString(L"Settings", L"OutputFormat", data.outputFormat.c_str(), iniPath.c_str());
    WCHAR buf[32];
    wsprintfW(buf, L"%d", (int)(data.sysGain * 10));
    WritePrivateProfileString(L"Settings", L"SysGain", buf, iniPath.c_str());
    wsprintfW(buf, L"%d", (int)(data.micGain * 10));
    WritePrivateProfileString(L"Settings", L"MicGain", buf, iniPath.c_str());
}

static bool ParseGain(HWND edit, float& out)
{
    WCHAR buf[32];
    GetWindowText(edit, buf, 32);
    WCHAR* end = nullptr;
    double v = wcstod(buf, &end);
    if (end == buf || v < 0.1 || v > 8.0) return false;
    out = (float)v;
    return true;
}

static LRESULT CALLBACK DlgWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hi = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
        HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");

        int y = 14;

        // 系统声音增益
        CreateWindow(WC_STATIC, L"系统声音增益 (0.1-8.0)", WS_CHILD | WS_VISIBLE | SS_LEFT,
            14, y + 5, 200, 22, hWnd, nullptr, hi, nullptr);
        WCHAR buf[32];
        swprintf_s(buf, L"%.1f", g_pData->sysGain);
        g_hSysEdit = CreateWindow(WC_EDIT, buf, WS_CHILD | WS_VISIBLE | WS_BORDER,
            220, y, 60, 26, hWnd, nullptr, hi, nullptr);
        SendMessage(g_hSysEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        CreateWindow(WC_STATIC, L"x（1.0=原音量）", WS_CHILD | WS_VISIBLE | SS_LEFT,
            290, y + 5, 130, 22, hWnd, nullptr, hi, nullptr);

        // 麦克风增益
        y += 38;
        CreateWindow(WC_STATIC, L"麦克风增益 (0.1-8.0)", WS_CHILD | WS_VISIBLE | SS_LEFT,
            14, y + 5, 200, 22, hWnd, nullptr, hi, nullptr);
        swprintf_s(buf, L"%.1f", g_pData->micGain);
        g_hMicEdit = CreateWindow(WC_EDIT, buf, WS_CHILD | WS_VISIBLE | WS_BORDER,
            220, y, 60, 26, hWnd, nullptr, hi, nullptr);
        SendMessage(g_hMicEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        CreateWindow(WC_STATIC, L"x（1.0=原音量）", WS_CHILD | WS_VISIBLE | SS_LEFT,
            290, y + 5, 130, 22, hWnd, nullptr, hi, nullptr);

        // 输出格式
        y += 46;
        CreateWindow(WC_STATIC, L"输出格式", WS_CHILD | WS_VISIBLE | SS_LEFT,
            14, y + 4, 90, 22, hWnd, nullptr, hi, nullptr);
        HWND rM4a = CreateWindow(WC_BUTTON, L"M4A 压缩", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            110, y, 100, 22, hWnd, (HMENU)(UINT_PTR)IDC_FMT_M4A, hi, nullptr);
        HWND rWav = CreateWindow(WC_BUTTON, L"WAV 无损", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            220, y, 100, 22, hWnd, (HMENU)(UINT_PTR)IDC_FMT_WAV, hi, nullptr);
        SendMessage(rM4a, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(rWav, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_pData->outputFormat == L"wav" ? rWav : rM4a, BM_SETCHECK, BST_CHECKED, 0);

        // 按钮
        y += 42;
        CreateWindow(WC_BUTTON, L"确定", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, y, 80, 28, hWnd, (HMENU)(UINT_PTR)IDC_SETTINGS_OK, hi, nullptr);
        CreateWindow(WC_BUTTON, L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            220, y, 80, 28, hWnd, (HMENU)(UINT_PTR)IDC_SETTINGS_CANCEL, hi, nullptr);

        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SETTINGS_OK) {
            if (!ParseGain(g_hSysEdit, g_pData->sysGain) || !ParseGain(g_hMicEdit, g_pData->micGain)) {
                MessageBox(hWnd, L"增益值无效，请输入 0.1 至 8.0 之间的数字。", L"输入错误", MB_ICONWARNING);
                return 0;
            }
            g_pData->outputFormat = (SendDlgItemMessage(hWnd, IDC_FMT_M4A, BM_GETCHECK, 0, 0) == BST_CHECKED) ? L"m4a" : L"wav";
            g_result = true;
            DestroyWindow(hWnd);
            return 0;
        }
        if (LOWORD(wp) == IDC_SETTINGS_CANCEL) {
            g_result = false;
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        g_result = false;
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

bool SettingsDialog::Show(HWND parent, HINSTANCE hInst, SettingsData& data)
{
    g_pData = &data;
    g_result = false;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DlgWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SettingsDlgWnd";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowEx(WS_EX_DLGMODALFRAME, L"SettingsDlgWnd", L"设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        0, 0, 440, 220, parent, nullptr, hInst, nullptr);
    if (!hWnd) return false;

    RECT pr, dr;
    GetWindowRect(parent, &pr);
    GetWindowRect(hWnd, &dr);
    SetWindowPos(hWnd, nullptr,
        (pr.left + pr.right) / 2 - (dr.right - dr.left) / 2,
        (pr.top + pr.bottom) / 2 - (dr.bottom - dr.top) / 2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EnableWindow(parent, FALSE);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return g_result;
}
