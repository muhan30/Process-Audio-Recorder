#include "MainWindow.h"
#include "SettingsDialog.h"
#include "Logger.h"
#include <CommCtrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <audiopolicy.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
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
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 580,
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

        y = 237;
        m_hAutoCheck = CreateWindowEx(0, WC_BUTTON, L"微信通话自动录音",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 10, y, 220, 22,
            m_hWnd, (HMENU)(UINT_PTR)IDC_AUTO_RECORD, hi, nullptr);
        SendMessage(m_hAutoCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

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

        // 录音历史列表
        m_hHistoryList = CreateWindowEx(0, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
            10, 380, 460, 150, m_hWnd, (HMENU)(UINT_PTR)IDC_HISTORY_LIST, hi, nullptr);
        ListView_SetExtendedListViewStyle(m_hHistoryList, LVS_EX_FULLROWSELECT);
        LVCOLUMN hcol = {};
        hcol.mask = LVCF_TEXT | LVCF_WIDTH;
        hcol.cx = 180; hcol.pszText = const_cast<LPWSTR>(L"文件名");
        ListView_InsertColumn(m_hHistoryList, 0, &hcol);
        hcol.cx = 100; hcol.pszText = const_cast<LPWSTR>(L"软件");
        ListView_InsertColumn(m_hHistoryList, 1, &hcol);
        hcol.cx = 110; hcol.pszText = const_cast<LPWSTR>(L"日期");
        ListView_InsertColumn(m_hHistoryList, 2, &hcol);
        hcol.cx = 60; hcol.pszText = const_cast<LPWSTR>(L"大小");
        ListView_InsertColumn(m_hHistoryList, 3, &hcol);

        // 字体
        for (HWND c : { m_hRefreshBtn, m_hHintLabel, m_hMicCheck, m_hSettingsBtn,
                        m_hTimerLabel, m_hSysLevel, m_hMicLevel, m_hSizeLabel, m_hSaveLink, m_hAutoCheck })
            if (c) SendMessage(c, WM_SETFONT, (WPARAM)hFont, TRUE);

        LoadSettings();
        SendMessage(m_hAutoCheck, BM_SETCHECK, m_autoRecord ? BST_CHECKED : BST_UNCHECKED, 0);

        // 注册音频会话变化通知（事件驱动，不用等1秒）
        RegisterSessionNotification(m_hWnd, WM_USER_SESSION_CHANGED);

        AddTrayIcon();
        ShowIdleState();
        OnRefresh();
        ScanRecordingHistory();
        SetTimer(m_hWnd, 2, 1000, nullptr);
        return 0;
    }

    case WM_SIZE:
        {
            int w = LOWORD(lp), h = HIWORD(lp);
            m_sessionList.Resize(w - 20, 150);
            if (m_hHistoryList)
                SetWindowPos(m_hHistoryList, nullptr, 10, 380, w - 20,
                    (std::max)(0, h - 400), SWP_NOZORDER);
        }
        return 0;

    case WM_CONTEXTMENU:
        if ((HWND)wp == m_hHistoryList)
        {
            // 在右击位置选中该项
            LVHITTESTINFO ht = {};
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ht.pt = pt;
            ScreenToClient(m_hHistoryList, &ht.pt);
            int idx = ListView_HitTest(m_hHistoryList, &ht);
            if (idx >= 0 && idx < (int)m_historyPaths.size())
            {
                ListView_SetItemState(m_hHistoryList, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                HMENU menu = CreatePopupMenu();
                AppendMenu(menu, MF_STRING, 4001, L"转为 AAC");
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
                DestroyMenu(menu);
            }
        }
        return 0;

    case WM_NOTIFY:
    {
        auto* nmh = reinterpret_cast<LPNMHDR>(lp);
        if (nmh->idFrom == IDC_SESSION_LIST && nmh->code == LVN_ITEMCHANGED)
        {
            auto* nmlv = reinterpret_cast<LPNMLISTVIEW>(lp);
            if ((nmlv->uNewState & LVIS_SELECTED) && !m_isRecording)
            {
                int idx = nmlv->iItem;
                const auto& sessions = m_sessionList.GetCurrentSessions();
                if (idx >= 0 && idx < (int)sessions.size())
                {
                    m_lastProcess = sessions[idx].processName;
                    SaveSettings();
                }
            }
        }
        if (nmh->idFrom == IDC_HISTORY_LIST && nmh->code == NM_DBLCLK)
        {
            auto* nmlv = reinterpret_cast<LPNMLISTVIEW>(lp);
            if (nmlv->iItem >= 0 && nmlv->iItem < (int)m_historyPaths.size())
            {
                std::wstring path = m_historyPaths[nmlv->iItem];
                // 文件存在检查 + 用 ShellExecuteEx 打开（关联默认播放器）
                if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    SHELLEXECUTEINFOW sei = {};
                    sei.cbSize = sizeof(sei);
                    sei.fMask = 0;
                    sei.hwnd = m_hWnd;
                    sei.lpVerb = L"open";
                    sei.lpFile = path.c_str();
                    sei.nShow = SW_SHOWNORMAL;
                    ShellExecuteExW(&sei);
                }
            }
        }
        return 0;
    }

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
        case IDC_AUTO_RECORD:
            m_autoRecord = (SendMessage(m_hAutoCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (!m_autoRecord) { m_autoRecording = false; m_noCallWindowCount = 0; m_findWechatRetries = 0; }
            SaveSettings();
            break;
        case IDM_TRAY_SHOW:
            ShowWindow(m_hWnd, SW_SHOW); break;
        case IDM_TRAY_STOP:
            if (m_isRecording) OnStop(); break;
        case IDM_TRAY_EXIT:
            if (OnExit()) { DestroyWindow(m_hWnd); } break;
        case 4001: // 转为 AAC
            {
                int idx = ListView_GetNextItem(m_hHistoryList, -1, LVNI_SELECTED);
                if (idx >= 0 && idx < (int)m_historyPaths.size())
                    ConvertToAac(m_historyPaths[idx]);
            }
            break;
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

    case WM_USER_SESSION_CHANGED:
        // 音频会话变化 → 释放 COM 引用（win-capture-audio 模式）→ 刷新列表
        if (wp) reinterpret_cast<IAudioSessionControl*>(wp)->Release();
        m_sessionList.Refresh();
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
        ScanRecordingHistory();
        return 0;
    }

    case WM_TIMER:
        if (wp == 1)
            UpdateStatus(m_engine.GetStatus());  // GUI 线程主动拉状态，线程安全
        else if (wp == 2) {
            OnRefresh();
            CheckAutoRecord();
        }
        return 0;

    case WM_DESTROY:
        UnregisterSessionNotification();
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
    ShowWindow(m_hAutoCheck, SW_SHOW);
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
    ShowWindow(m_hAutoCheck, SW_HIDE);
    ShowWindow(m_hStopBtn, SW_SHOW);
    ShowWindow(m_hTimerLabel, SW_SHOW);
    ShowWindow(m_hSysLevel, SW_SHOW);
    ShowWindow(m_hMicLevel, SW_SHOW);
    ShowWindow(m_hSizeLabel, SW_SHOW);
    ShowWindow(m_hSaveLink, SW_SHOW);
    UpdateTrayIcon();
}

// ---- 操作 ----
void MainWindow::OnRefresh()
{
    m_sessionList.Refresh();
    if (!m_lastProcess.empty())
        m_sessionList.SelectByProcessName(m_lastProcess);
}

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
    // 强度 0-10 → dBFS: 0=off, 1→-65, 5→-45, 10→-20
    float gateDBFS = (m_noiseGateStrength > 0.01f) ? -(70.0f - m_noiseGateStrength * 5.0f) : 0.0f;
    m_engine.SetNoiseGateThreshold(gateDBFS);

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

    // 按软件名分子文件夹：recordings/微信/（去掉 .exe 后缀）
    std::wstring folderName = procName;
    if (folderName.size() > 4 && _wcsicmp(folderName.c_str() + folderName.size() - 4, L".exe") == 0)
        folderName.resize(folderName.size() - 4);
    std::wstring appDir = recDir + L"\\" + folderName;
    CreateDirectory(appDir.c_str(), nullptr);

    WCHAR name[128];
    wsprintfW(name, L"%04d-%02d-%02d %s %02d-%02d-%02d.%s",
        localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
        procName.c_str(), localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
        m_outputFormat.c_str());
    std::wstring path = appDir + L"\\" + std::wstring(name);
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
    cfg.noiseGateStrength = m_noiseGateStrength;
    if (SettingsDialog::Show(m_hWnd, m_hInst, cfg))
    {
        m_outputFormat = cfg.outputFormat;
        m_sysGain = cfg.sysGain;
        m_micGain = cfg.micGain;
        m_lowpassCutoff = cfg.lowpassCutoff;
        m_noiseGateStrength = cfg.noiseGateStrength;
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
    m_noiseGateStrength = cfg.noiseGateStrength;
    // LastProcess
    WCHAR iniPath[MAX_PATH];
    GetModuleFileName(nullptr, iniPath, MAX_PATH);
    WCHAR* last = wcsrchr(iniPath, L'\\');
    if (last) *(last + 1) = L'\0';
    std::wstring ini = std::wstring(iniPath) + L"ProcessAudioRecorder.ini";
    WCHAR buf[MAX_PATH];
    GetPrivateProfileString(L"Settings", L"LastProcess", L"", buf, MAX_PATH, ini.c_str());
    m_lastProcess = buf;
    m_autoRecord = GetPrivateProfileInt(L"Settings", L"AutoRecord", 1, ini.c_str()) != 0;
}

void MainWindow::SaveSettings()
{
    SettingsData cfg;
    cfg.savePath = L"recordings";
    cfg.outputFormat = m_outputFormat;
    cfg.sysGain = m_sysGain;
    cfg.micGain = m_micGain;
    cfg.lowpassCutoff = m_lowpassCutoff;
    cfg.noiseGateStrength = m_noiseGateStrength;
    SettingsDialog::SaveToIni(cfg);
    // LastProcess
    WCHAR iniPath[MAX_PATH];
    GetModuleFileName(nullptr, iniPath, MAX_PATH);
    WCHAR* last = wcsrchr(iniPath, L'\\');
    if (last) *(last + 1) = L'\0';
    std::wstring ini = std::wstring(iniPath) + L"ProcessAudioRecorder.ini";
    WritePrivateProfileString(L"Settings", L"LastProcess", m_lastProcess.c_str(), ini.c_str());
    WCHAR abuf[8];
    wsprintfW(abuf, L"%d", m_autoRecord ? 1 : 0);
    WritePrivateProfileString(L"Settings", L"AutoRecord", abuf, ini.c_str());
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

// ---- 录音历史 ----
void MainWindow::ScanRecordingHistory()
{
    ListView_DeleteAllItems(m_hHistoryList);
    m_historyPaths.clear();

    WCHAR exeDir[MAX_PATH];
    GetModuleFileName(nullptr, exeDir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    std::wstring recDir = std::wstring(exeDir) + L"\\recordings";

    // 收集所有录音文件
    struct FileEntry {
        std::wstring path, name, folder, dateStr;
        ULONGLONG size;
        FILETIME ft;
    };
    std::vector<FileEntry> files;

    // 辅助：收集某目录下的录音文件
    auto collectDir = [&](const std::wstring& dir, const std::wstring& folderLabel) {
        for (auto* ext : { L"*.m4a", L"*.wav" })
        {
            WIN32_FIND_DATAW fFd;
            HANDLE hF = FindFirstFileW((dir + L"\\" + ext).c_str(), &fFd);
            if (hF == INVALID_HANDLE_VALUE) continue;
            do {
                if (fFd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                FileEntry fe;
                fe.path = dir + L"\\" + fFd.cFileName;
                fe.name = fFd.cFileName;
                fe.folder = folderLabel;
                fe.size = ((ULONGLONG)fFd.nFileSizeHigh << 32) | fFd.nFileSizeLow;
                fe.ft = fFd.ftCreationTime;
                FILETIME localFt;
                FileTimeToLocalFileTime(&fFd.ftCreationTime, &localFt);
                SYSTEMTIME st;
                FileTimeToSystemTime(&localFt, &st);
                WCHAR dateBuf[64];
                wsprintfW(dateBuf, L"%04d-%02d-%02d %02d:%02d",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
                fe.dateStr = dateBuf;
                files.push_back(fe);
            } while (FindNextFileW(hF, &fFd));
            FindClose(hF);
        }
    };

    // 扫描 recordings/ 根目录（旧录音，分文件夹功能之前）
    collectDir(recDir, L"(根目录)");

    // 遍历 recordings/ 下所有子文件夹
    std::wstring sdSearch = recDir + L"\\*";
    WIN32_FIND_DATAW sdFd;
    HANDLE hSd = FindFirstFileW(sdSearch.c_str(), &sdFd);
    if (hSd != INVALID_HANDLE_VALUE)
    {
        do {
            if (!(sdFd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (wcscmp(sdFd.cFileName, L".") == 0 || wcscmp(sdFd.cFileName, L"..") == 0) continue;

            std::wstring subDir = recDir + L"\\" + sdFd.cFileName;
            collectDir(subDir, sdFd.cFileName);
        } while (FindNextFileW(hSd, &sdFd));
        FindClose(hSd);
    }

    // 按时间倒序
    std::sort(files.begin(), files.end(),
        [](const FileEntry& a, const FileEntry& b) {
            return CompareFileTime(&a.ft, &b.ft) > 0;
        });

    // 最多 50 条
    size_t maxItems = (std::min)(files.size(), (size_t)50);
    for (size_t i = 0; i < maxItems; i++)
    {
        auto& fe = files[i];
        m_historyPaths.push_back(fe.path);

        LVITEM item = {};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.pszText = const_cast<LPWSTR>(fe.name.c_str());
        ListView_InsertItem(m_hHistoryList, &item);

        ListView_SetItemText(m_hHistoryList, (int)i, 1, const_cast<LPWSTR>(fe.folder.c_str()));
        ListView_SetItemText(m_hHistoryList, (int)i, 2, const_cast<LPWSTR>(fe.dateStr.c_str()));

        WCHAR sizeBuf[32];
        if (fe.size < 1024 * 1024)
            wsprintfW(sizeBuf, L"%u KB", (UINT)(fe.size / 1024));
        else
            swprintf_s(sizeBuf, L"%.1f MB", fe.size / (1024.0 * 1024.0));
        ListView_SetItemText(m_hHistoryList, (int)i, 3, sizeBuf);
    }
}

// ---- 微信自动录音 ----
// 实现直接复刻 WeChatRecorder.py: EnumWindows + GetClassNameW

bool MainWindow::IsWeChatInCall()
{
    bool found = false;
    EnumWindows([](HWND hWnd, LPARAM lp) -> BOOL {
        WCHAR cls[64] = {}, title[128] = {};
        GetClassNameW(hWnd, cls, 64);
        GetWindowTextW(hWnd, title, 128);

        // 方法1: Qt5 通话窗口 "Weixin Voice & Video Calls"
        if (wcsstr(cls, L"QWindowIcon") &&
            (wcsstr(title, L"Voice") || wcsstr(title, L"Call") ||
             wcsstr(title, L"语音") || wcsstr(title, L"通话") || wcsstr(title, L"视频")))
            { *(bool*)lp = true; return FALSE; }

        // 方法2: 通话视频渲染子窗口
        if (_wcsicmp(cls, L"MMUIRenderSubWindowHW") == 0)
            { *(bool*)lp = true; return FALSE; }

        return TRUE;
    }, (LPARAM)&found);
    return found;
}

int MainWindow::FindWeChatPid()
{
    for (auto& s : m_sessionList.GetCurrentSessions())
        if (_wcsicmp(s.processName.c_str(), L"WeChat.exe") == 0 ||
            _wcsicmp(s.processName.c_str(), L"Weixin.exe") == 0)
            return (int)s.processId;
    return 0;
}

void MainWindow::CheckAutoRecord()
{
    if (!m_autoRecord) return;

    bool inCall = IsWeChatInCall();

    if (m_autoRecording)
    {
        if (!m_isRecording) { m_autoRecording = false; m_noCallWindowCount = 0; return; }
        // 停止：通话窗口消失 → 3秒确认 → 停止
        if (!inCall)
        {
            m_noCallWindowCount++;
            if (m_noCallWindowCount >= 3)
            {
                OnStop();
                m_autoRecording = false;
                m_noCallWindowCount = 0;
            }
        }
        else { m_noCallWindowCount = 0; }
        return;
    }

    if (m_isRecording) { m_findWechatRetries = 0; return; }

    // 启动：通话窗口出现 → 找微信 PID → 开始录音
    if (!inCall) { m_findWechatRetries = 0; return; }
    m_findWechatRetries++;
    if (m_findWechatRetries < 2) return;  // 1秒确认

    int pid = FindWeChatPid();
    if (pid == 0) { m_findWechatRetries = 0; return; }

    m_autoRecording = true;
    m_findWechatRetries = 0;
    m_sessionList.SelectByProcessName(L"WeChat.exe");
    m_sessionList.SelectByProcessName(L"Weixin.exe");
    OnStart();
}

// ---- AAC 转换 ----
void MainWindow::ConvertToAac(const std::wstring& m4aPath)
{
    // 生成输出路径
    auto dot = m4aPath.rfind(L'.');
    std::wstring outPath = (dot != std::wstring::npos) ? m4aPath.substr(0, dot) + L".aac" : m4aPath + L".aac";

    if (GetFileAttributesW(outPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        WCHAR msg[512];
        wsprintfW(msg, L"文件已存在:\n%s\n\n覆盖？", outPath.c_str());
        if (MessageBox(m_hWnd, msg, L"转为 AAC", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }

    // MF 可能已被录音 Finalize 关闭，需重初始化。用 goto 保证 MFShutdown 必调用。
    MFStartup(MF_VERSION);
    HRESULT hr;
    IMFSourceReader* reader = nullptr;
    IMFSinkWriter* writer = nullptr;

    hr = MFCreateSourceReaderFromURL(m4aPath.c_str(), nullptr, &reader);
    if (FAILED(hr)) goto done;

    {
        UINT32 sampleRate = 44100, channels = 2;
        IMFMediaType* pcmType = nullptr;
        if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pcmType)) && pcmType) {
            pcmType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
            pcmType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            pcmType->Release();
        }

        IMFAttributes* sinkAttrs = nullptr;
        MFCreateAttributes(&sinkAttrs, 1);
        sinkAttrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_ADTS);
        hr = MFCreateSinkWriterFromURL(outPath.c_str(), nullptr, sinkAttrs, &writer);
        sinkAttrs->Release();
        if (FAILED(hr)) goto done;

        IMFMediaType* outType = nullptr;
        MFCreateMediaType(&outType);
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
        outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
        outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
        DWORD outIdx = 0;
        hr = writer->AddStream(outType, &outIdx);
        outType->Release();
        if (FAILED(hr)) goto done;

        DWORD blockAlign = channels * 2;
        IMFMediaType* inType = nullptr;
        MFCreateMediaType(&inType);
        inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
        inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
        inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
        inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * blockAlign);
        hr = writer->SetInputMediaType(outIdx, inType, nullptr);
        inType->Release();
        if (FAILED(hr)) goto done;

        hr = writer->BeginWriting();
        if (FAILED(hr)) goto done;

        DWORD streamIdx, flags;
        LONGLONG ts, rt = 0;
        for (;;) {
            IMFSample* sample = nullptr;
            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &streamIdx, &flags, &ts, &sample);
            if (FAILED(hr)) break;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (sample) sample->Release(); break; }
            if (!sample) continue;
            DWORD len = 0;
            if (FAILED(sample->GetTotalLength(&len)) || len == 0) { sample->Release(); continue; }
            sample->SetSampleTime(rt);
            sample->SetSampleDuration((LONGLONG)len * 10000000LL / (sampleRate * channels * 2));
            rt += (LONGLONG)len * 10000000LL / (sampleRate * channels * 2);
            writer->WriteSample(outIdx, sample);
            sample->Release();
        }
        hr = writer->Finalize();
    }

done:
    if (writer) writer->Release();
    if (reader) reader->Release();
    MFShutdown();

    if (SUCCEEDED(hr)) {
        ScanRecordingHistory();
        MessageBox(m_hWnd, L"转换完成。", L"OK", MB_OK);
    } else {
        DeleteFileW(outPath.c_str());
        MessageBox(m_hWnd, L"无法打开 M4A 文件，请确认文件存在且未被占用。", L"转换失败", MB_ICONERROR);
    }
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
