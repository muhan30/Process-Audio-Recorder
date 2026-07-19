#include "MainWindow.h"
#include "SettingsDialog.h"
#include "Logger.h"
#include <CommCtrl.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <ctime>

// 窗口类名
static const WCHAR* WND_CLASS = L"AudioRecorderMainWindow";

// 创建 Segoe UI 字体
static HFONT CreateUIFont(int height)
{
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
}

MainWindow::MainWindow(HINSTANCE hInst) : m_hInst(hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbWndExtra = sizeof(MainWindow*);
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(101));
    wc.hIconSm = LoadIcon(hInst, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);
}

MainWindow::~MainWindow()
{
    if (m_hWnd) DestroyWindow(m_hWnd);
}

bool MainWindow::CreateMainWindow(int nCmdShow)
{
    m_hWnd = CreateWindowExW(0, WND_CLASS, L"进程录音机",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 440,
        nullptr, nullptr, m_hInst, this);
    if (!m_hWnd) return false;
    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);
    return true;
}

void MainWindow::RunMessageLoop()
{
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* self = nullptr;
    if (msg == WM_CREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hWnd = hWnd;
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProc(hWnd, msg, wp, lp);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hi = m_hInst;
        HFONT hFont = CreateUIFont(18);

        // 发声列表
        m_sessionList.Create(m_hWnd, hi, 10, 8, 460, 170);

        int y = 185;
        m_hRefreshBtn = CreateWindowEx(0, WC_BUTTON, L"刷新",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 390, y, 80, 24,
            m_hWnd, (HMENU)(UINT_PTR)IDC_REFRESH_BTN, hi, nullptr);
        m_hHintLabel = CreateWindowEx(0, WC_STATIC, L"没看到想录的软件？让它先出个声再点刷新",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 10, y, 370, 24,
            m_hWnd, (HMENU)(UINT_PTR)IDC_HINT_LABEL, hi, nullptr);

        y = 215;
        m_hMicCheck = CreateWindowEx(0, WC_BUTTON, L"同时录我的麦克风",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 10, y, 200, 24,
            m_hWnd, (HMENU)(UINT_PTR)IDC_MIC_CHECK, hi, nullptr);
        SendMessage(m_hMicCheck, BM_SETCHECK, m_micEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        m_hSettingsBtn = CreateWindowEx(0, WC_BUTTON, L"设置...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 390, y, 80, 24,
            m_hWnd, (HMENU)(UINT_PTR)IDC_SETTINGS_BTN, hi, nullptr);

        y = 255;
        m_hStartBtn = CreateWindowEx(0, WC_BUTTON, L"开始录音",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_VCENTER | BS_OWNERDRAW,
            140, y, 180, 50, m_hWnd, (HMENU)(UINT_PTR)IDC_START_BTN, hi, nullptr);
        m_hStopBtn = CreateWindowEx(0, WC_BUTTON, L"停止并保存",
            WS_CHILD | BS_PUSHBUTTON | BS_VCENTER | BS_OWNERDRAW,
            100, y, 280, 50, m_hWnd, (HMENU)(UINT_PTR)IDC_STOP_BTN, hi, nullptr);

        // 录音态控件
        m_hTimerLabel = CreateWindowEx(0, WC_STATIC, L"00:00:00",
            WS_CHILD | SS_CENTER, 100, 190, 260, 38,
            m_hWnd, (HMENU)(UINT_PTR)IDC_TIMER_LABEL, hi, nullptr);
        m_hSysLevel = CreateWindowEx(0, WC_STATIC, L"系统声音 ---",
            WS_CHILD | SS_LEFT, 40, 240, 200, 20,
            m_hWnd, (HMENU)(UINT_PTR)IDC_SYS_LEVEL, hi, nullptr);
        m_hMicLevel = CreateWindowEx(0, WC_STATIC, L"麦克风 ---",
            WS_CHILD | SS_LEFT, 260, 240, 200, 20,
            m_hWnd, (HMENU)(UINT_PTR)IDC_MIC_LEVEL, hi, nullptr);
        m_hSizeLabel = CreateWindowEx(0, WC_STATIC, L"",
            WS_CHILD | SS_CENTER, 100, 320, 260, 20,
            m_hWnd, (HMENU)(UINT_PTR)IDC_SIZE_LABEL, hi, nullptr);
        m_hSaveLink = CreateWindowEx(0, WC_STATIC, L"录音文件保存在 exe 同目录的 recordings 文件夹",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 350, 460, 20,
            m_hWnd, nullptr, hi, nullptr);

        // 字体
        for (HWND c : { m_hRefreshBtn, m_hHintLabel, m_hMicCheck, m_hSettingsBtn,
                        m_hTimerLabel, m_hSysLevel, m_hMicLevel, m_hSizeLabel, m_hSaveLink })
            if (c) SendMessage(c, WM_SETFONT, (WPARAM)hFont, TRUE);

        LoadSettings();
        AddTrayIcon();
        ShowIdleState();
        OnRefresh();
        SetTimer(m_hWnd, 2, 1000, nullptr);
        return 0;
    }

    case WM_SIZE:
        m_sessionList.Resize(LOWORD(lp) - 20, 150);
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        switch (id)
        {
        case IDC_REFRESH_BTN:   OnRefresh(); break;
        case IDC_START_BTN:     OnStart(); break;
        case IDC_STOP_BTN:      OnStop(); break;
        case IDC_SETTINGS_BTN:  OnSettings(); break;
        case IDC_MIC_CHECK:
            m_micEnabled = (SendMessage(m_hMicCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case IDM_TRAY_SHOW:
            ShowWindow(m_hWnd, SW_SHOW); break;
        case IDM_TRAY_STOP:
            if (m_isRecording) OnStop(); break;
        case IDM_TRAY_EXIT:
            if (OnExit()) { DestroyWindow(m_hWnd); } break;
        }
        return 0;
    }

    case WM_DRAWITEM:
    {
        auto* di = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
        if (di->CtlType != ODT_BUTTON) break;
        WCHAR text[64];
        GetWindowText(di->hwndItem, text, 64);
        bool pressed = (di->itemState & ODS_SELECTED) != 0;
        UINT edge = pressed ? EDGE_SUNKEN : EDGE_RAISED;
        DrawEdge(di->hDC, &di->rcItem, edge, BF_RECT | BF_ADJUST);
        FillRect(di->hDC, &di->rcItem, GetSysColorBrush(COLOR_BTNFACE));
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
        SelectObject(di->hDC, GetStockObject(DEFAULT_GUI_FONT));
        if (pressed) OffsetRect(&di->rcItem, 1, 1);
        DrawText(di->hDC, text, -1, &di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }

    case WM_CLOSE:
        ShowWindow(m_hWnd, SW_HIDE);  // 缩到托盘，不退出
        return 0;

    case WM_USER_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU)
            ShowTrayMenu();
        else if (LOWORD(lp) == WM_LBUTTONUP)
            ShowWindow(m_hWnd, SW_SHOW);
        return 0;

    case WM_USER_RECORDING_STOPPED:
    {
        KillTimer(m_hWnd, 1);
        // 日志：录音停止（记录时长和文件大小）
        CaptureStatus st = m_engine.GetStatus();
        Logger::LogRecStop(m_lastRecordingPath, st.bytesWritten, (int)st.elapsed.count(), !m_isRecording);

        // 写文件时间戳
        if (!m_lastRecordingPath.empty())
        {
            FILETIME ft;
            SystemTimeToFileTime(&m_recordingStartST, &ft);
            HANDLE hFile = CreateFile(m_lastRecordingPath.c_str(), FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                SetFileTime(hFile, &ft, &ft, &ft);
                CloseHandle(hFile);
            }
        }
        ShowIdleState();
        return 0;
    }

    case WM_TIMER:
        if (wp == 1)
            UpdateStatus(m_engine.GetStatus());  // GUI 线程主动拉状态，线程安全
        else if (wp == 2)
            OnRefresh();
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(m_hWnd, msg, wp, lp);
}

// ---- 状态切换 ----
void MainWindow::ShowIdleState()
{
    m_isRecording = false;
    ShowWindow(m_hStartBtn, SW_SHOW);
    ShowWindow(m_hRefreshBtn, SW_SHOW);
    ShowWindow(m_hHintLabel, SW_SHOW);
    ShowWindow(m_hMicCheck, SW_SHOW);
    ShowWindow(m_hSettingsBtn, SW_SHOW);
    ShowWindow(m_hStopBtn, SW_HIDE);
    ShowWindow(m_hTimerLabel, SW_HIDE);
    ShowWindow(m_hSysLevel, SW_HIDE);
    ShowWindow(m_hMicLevel, SW_HIDE);
    ShowWindow(m_hSizeLabel, SW_HIDE);
    ShowWindow(m_hSaveLink, SW_HIDE);
    UpdateTrayIcon();
}

void MainWindow::ShowRecordingState()
{
    m_isRecording = true;
    ShowWindow(m_hStartBtn, SW_HIDE);
    ShowWindow(m_hRefreshBtn, SW_HIDE);
    ShowWindow(m_hHintLabel, SW_HIDE);
    ShowWindow(m_hMicCheck, SW_HIDE);
    ShowWindow(m_hSettingsBtn, SW_HIDE);
    ShowWindow(m_hStopBtn, SW_SHOW);
    ShowWindow(m_hTimerLabel, SW_SHOW);
    ShowWindow(m_hSysLevel, SW_SHOW);
    ShowWindow(m_hMicLevel, SW_SHOW);
    ShowWindow(m_hSizeLabel, SW_SHOW);
    ShowWindow(m_hSaveLink, SW_SHOW);
    UpdateTrayIcon();
}

// ---- 操作 ----
void MainWindow::OnRefresh() { m_sessionList.Refresh(); }

void MainWindow::OnStart()
{
    int pid = m_sessionList.GetSelectedPid();
    if (pid == 0) {
        MessageBox(m_hWnd, L"请先在列表里点一下要录音的软件，再点开始录音。", L"提示", MB_OK);
        return;
    }
    m_engine.SetMicEnabled(m_micEnabled);
    m_engine.SetMicGain(m_micGain);
    m_engine.SetSystemGain(m_sysGain);
    m_engine.SetLowpassCutoff(m_lowpassCutoff * 1000.0f);  // kHz → Hz

    // Build recordings directory next to exe
    WCHAR exeDir[MAX_PATH];
    GetModuleFileName(nullptr, exeDir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    std::wstring recDir = std::wstring(exeDir) + L"\\recordings";
    CreateDirectory(recDir.c_str(), nullptr);

    auto t = std::time(nullptr);
    tm localTime;
    localtime_s(&localTime, &t);
    std::wstring procName = L"系统混音";
    for (const auto& s : m_sessionList.GetCurrentSessions())
        if (s.processId == (DWORD)pid) { procName = s.processName; break; }
    WCHAR name[128];
    wsprintfW(name, L"%04d-%02d-%02d %s %02d-%02d-%02d.%s",
        localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
        procName.c_str(), localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
        m_outputFormat.c_str());
    std::wstring path = recDir + L"\\" + std::wstring(name);
    m_engine.SetOutputPath(path);
    m_engine.SetOutputFormat(m_outputFormat);
    GetSystemTime(&m_recordingStartST);
    m_lastRecordingPath = path;

    // 日志：录音开始
    std::wstring target = procName + L" (PID " + std::to_wstring(pid) + L")";
    Logger::LogRecStart(target, path, m_sysGain, m_micGain);

    HRESULT hr = m_engine.Start(pid, true,
        [this](HRESULT) { PostMessage(m_hWnd, WM_USER_RECORDING_STOPPED, 0, 0); });
    if (FAILED(hr)) {
        Logger::LogError(L"recording start failed", hr);
        MessageBox(m_hWnd, L"启动录音失败，请检查音频设备。", L"错误", MB_ICONERROR);
        return;
    }
    ShowRecordingState();
    SetTimer(m_hWnd, 1, 200, nullptr);
}

void MainWindow::OnStop()
{
    KillTimer(m_hWnd, 1);
    m_engine.Stop();
    // ShowIdleState 和 SetFileTime 统一由 WM_USER_RECORDING_STOPPED 消息触发
}

void MainWindow::OnSettings()
{
    SettingsData cfg;
    cfg.outputFormat = m_outputFormat;
    cfg.sysGain = m_sysGain;
    cfg.micGain = m_micGain;
    cfg.lowpassCutoff = m_lowpassCutoff;
    if (SettingsDialog::Show(m_hWnd, m_hInst, cfg))
    {
        m_outputFormat = cfg.outputFormat;
        m_sysGain = cfg.sysGain;
        m_micGain = cfg.micGain;
        m_lowpassCutoff = cfg.lowpassCutoff;
        SaveSettings();
    }
}

// ---- 设置持久化 ----
void MainWindow::LoadSettings()
{
    SettingsData cfg;
    SettingsDialog::LoadFromIni(cfg);
    m_outputFormat = cfg.outputFormat;
    m_sysGain = cfg.sysGain;
    m_micGain = cfg.micGain;
    m_lowpassCutoff = cfg.lowpassCutoff;
}

void MainWindow::SaveSettings()
{
    SettingsData cfg;
    cfg.savePath = L"recordings";
    cfg.outputFormat = m_outputFormat;
    cfg.sysGain = m_sysGain;
    cfg.micGain = m_micGain;
    cfg.lowpassCutoff = m_lowpassCutoff;
    SettingsDialog::SaveToIni(cfg);
}

// ---- 电平 ----
static std::wstring DrawLevelBar(int level)
{
    std::wstring bar;
    int filled = (level + 9) / 10;
    for (int i = 0; i < 10; i++) bar += (i < filled) ? L'#' : L'-';
    return bar;
}

void MainWindow::UpdateStatus(const CaptureStatus& st)
{
    auto secs = st.elapsed.count();
    WCHAR buf[64];
    wsprintfW(buf, L"%02d:%02d:%02d", (int)(secs / 3600), (int)((secs % 3600) / 60), (int)(secs % 60));
    SetWindowText(m_hTimerLabel, buf);

    std::wstring sysText = L"系统声音 [" + DrawLevelBar(st.systemLevel) + L"]";
    SetWindowText(m_hSysLevel, sysText.c_str());
    if (st.micEnabled) {
        std::wstring micText = L"麦克风 [" + DrawLevelBar(st.micLevel) + L"]";
        SetWindowText(m_hMicLevel, micText.c_str());
    }
    UINT64 bytes = st.bytesWritten;
    if (bytes < 1024 * 1024) wsprintfW(buf, L"文件大小: %u KB", (UINT)(bytes / 1024));
    else swprintf_s(buf, L"文件大小: %.1f MB", bytes / (1024.0 * 1024.0));
    SetWindowText(m_hSizeLabel, buf);
}

// ---- 托盘 ----
void MainWindow::AddTrayIcon()
{
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_USER_TRAYICON;
    m_nid.hIcon = LoadIcon(m_hInst, MAKEINTRESOURCE(101));
    wcscpy_s(m_nid.szTip, L"进程录音机");
    Shell_NotifyIcon(NIM_ADD, &m_nid);
}

void MainWindow::RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &m_nid);
}

void MainWindow::UpdateTrayIcon()
{
    if (m_isRecording)
    {
        m_nid.uFlags = NIF_TIP;
        wcscpy_s(m_nid.szTip, L"🔴 正在录音...");
    }
    else
    {
        m_nid.uFlags = NIF_TIP;
        wcscpy_s(m_nid.szTip, L"进程录音机");
    }
    Shell_NotifyIcon(NIM_MODIFY, &m_nid);
}

void MainWindow::ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenu(menu, MF_STRING, IDM_TRAY_SHOW, L"打开主窗口");
    if (m_isRecording)
        AppendMenu(menu, MF_STRING, IDM_TRAY_STOP, L"停止录音");
    AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(menu, MF_STRING, IDM_TRAY_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    DestroyMenu(menu);
}

bool MainWindow::OnExit()
{
    if (m_isRecording)
    {
        int ret = MessageBox(m_hWnd,
            L"正在录音中，确定要退出吗？录音文件会正常保存。",
            L"确认退出", MB_YESNO | MB_ICONQUESTION);
        if (ret != IDYES) return false;
        OnStop();
    }
    return true;
}
