#include "MainWindow.h"
#include <CommCtrl.h>
#include <sstream>
#include <iomanip>
#include <ctime>

// 窗口类名
static const WCHAR* WND_CLASS = L"AudioRecorderMainWindow";

// 创建 Segoe UI 字体（抗锯齿清晰，替换系统默认粗糙字体）
static HFONT CreateUIFont(int height)
{
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
}

MainWindow::MainWindow(HINSTANCE hInst) : m_hInst(hInst)
{
    // 注册窗口类
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(MainWindow*);
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);
}

MainWindow::~MainWindow()
{
    if (m_hWnd)
        DestroyWindow(m_hWnd);
}

bool MainWindow::CreateMainWindow(int nCmdShow)
{
    m_hWnd = CreateWindowExW(0, WND_CLASS, L"进程录音机",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,  // WS_CLIPCHILDREN 阻止父窗口在子控件区域绘制，按钮文字不再被覆盖
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 520,
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

// ---- 静态窗口过程 ----
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
    if (self)
        return self->HandleMessage(msg, wp, lp);
    return DefWindowProc(hWnd, msg, wp, lp);
}

// ---- 消息处理 ----
LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hi = m_hInst;

        // 发声列表
        m_sessionList.Create(m_hWnd, hi, 10, 10, 440, 200);

        // 刷新按钮
        m_hRefreshBtn = CreateWindowEx(0, WC_BUTTON, L"刷新",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 215, 90, 26, m_hWnd, (HMENU)(UINT_PTR)IDC_REFRESH_BTN, hi, nullptr);

        // 提示
        m_hHintLabel = CreateWindowEx(0, WC_STATIC, L"没看到想录的软件？让它先出个声，再点刷新",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 220, 340, 20, m_hWnd, (HMENU)(UINT_PTR)IDC_HINT_LABEL, hi, nullptr);

        // 麦克风勾选框
        m_hMicCheck = CreateWindowEx(0, WC_BUTTON, L"同时录我的麦克风",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            10, 250, 200, 22, m_hWnd, (HMENU)(UINT_PTR)IDC_MIC_CHECK, hi, nullptr);
        SendMessage(m_hMicCheck, BM_SETCHECK, m_micEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        // 设置按钮
        m_hSettingsBtn = CreateWindowEx(0, WC_BUTTON, L"设置...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            300, 248, 70, 24, m_hWnd, (HMENU)(UINT_PTR)IDC_SETTINGS_BTN, hi, nullptr);

        // 开始按钮（BS_OWNERDRAW 自绘，绕过 Win32 按钮控件的 CJK 文字裁切 bug）
        m_hStartBtn = CreateWindowEx(0, WC_BUTTON, L"开始录音",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_VCENTER | BS_OWNERDRAW,
            140, 285, 180, 52, m_hWnd, (HMENU)(UINT_PTR)IDC_START_BTN, hi, nullptr);

        // 停止按钮（BS_OWNERDRAW 自绘）
        m_hStopBtn = CreateWindowEx(0, WC_BUTTON, L"停止并保存",
            WS_CHILD | BS_PUSHBUTTON | BS_VCENTER | BS_OWNERDRAW,
            100, 285, 280, 52, m_hWnd, (HMENU)(UINT_PTR)IDC_STOP_BTN, hi, nullptr);

        // 计时标签（录音态，初始隐藏）
        m_hTimerLabel = CreateWindowEx(0, WC_STATIC, L"00:00:00",
            WS_CHILD | SS_CENTER,
            100, 240, 260, 40, m_hWnd, (HMENU)(UINT_PTR)IDC_TIMER_LABEL, hi, nullptr);

        // 系统声音电平（放在按钮上方，避免与按钮区域重叠）
        m_hSysLevel = CreateWindowEx(0, WC_STATIC, L"系统声音 ---",
            WS_CHILD | SS_LEFT,
            10, 270, 220, 20, m_hWnd, (HMENU)(UINT_PTR)IDC_SYS_LEVEL, hi, nullptr);

        // 麦克风电平
        m_hMicLevel = CreateWindowEx(0, WC_STATIC, L"麦克风 ---",
            WS_CHILD | SS_LEFT,
            250, 270, 220, 20, m_hWnd, (HMENU)(UINT_PTR)IDC_MIC_LEVEL, hi, nullptr);

        // 文件大小标签（放在按钮下方，不与按钮区域重叠）
        m_hSizeLabel = CreateWindowEx(0, WC_STATIC, L"",
            WS_CHILD | SS_CENTER,
            100, 345, 260, 20, m_hWnd, (HMENU)(UINT_PTR)IDC_SIZE_LABEL, hi, nullptr);

        // 保存路径链接
        m_hSaveLink = CreateWindowEx(0, WC_STATIC, L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 375, 440, 20, m_hWnd, (HMENU)(UINT_PTR)IDC_SAVE_LINK, hi, nullptr);

        // 默认输出路径
        m_outputPath = L"recordings\\";

        // ---- 统一字体美化（Segoe UI 10pt，ClearType 抗锯齿） ----
        HFONT hFont = CreateUIFont(18);      // 18px ≈ 10pt 用于标签、列表
        // 标签/文本用 18px（按钮不设自定义字体，用系统按钮默认字体防边框裁切）
        SendMessage(m_hHintLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(m_hMicCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(m_hTimerLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(m_hSysLevel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(m_hMicLevel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(m_hSizeLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(m_hSaveLink, WM_SETFONT, (WPARAM)hFont, TRUE);
        // hFont 不 delete，随进程生命周期持续

        ShowIdleState();
        OnRefresh();

        // 自动刷新列表（1 秒一次）
        SetTimer(m_hWnd, 2, 1000, nullptr);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        switch (id)
        {
        case IDC_REFRESH_BTN:  OnRefresh(); break;
        case IDC_START_BTN:    OnStart(); break;
        case IDC_STOP_BTN:     OnStop(); break;
        case IDC_SETTINGS_BTN: OnSettings(); break;
        case IDC_SAVE_LINK:    OnSaveLink(); break;
        case IDC_MIC_CHECK:
            m_micEnabled = (SendMessage(m_hMicCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        }
        return 0;
    }

    case WM_DRAWITEM:
    {
        // 自绘按钮（解决 Win32 BS_PUSHBUTTON 对 CJK 文字裁切的已知兼容问题）
        auto* di = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
        if (di->CtlType != ODT_BUTTON) break;

        WCHAR text[64];
        GetWindowText(di->hwndItem, text, 64);
        bool pressed = (di->itemState & ODS_SELECTED) != 0;

        // 3D 边框
        UINT edge = pressed ? EDGE_SUNKEN : EDGE_RAISED;
        DrawEdge(di->hDC, &di->rcItem, edge, BF_RECT | BF_ADJUST);

        // 按钮面
        HBRUSH bg = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(di->hDC, &di->rcItem, bg);

        // 文字（居中，ClearType 抗锯齿自动生效）
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
        SelectObject(di->hDC, GetStockObject(DEFAULT_GUI_FONT));
        if (pressed) OffsetRect(&di->rcItem, 1, 1); // 按下态偏移
        DrawText(di->hDC, text, -1, &di->rcItem,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }

    case WM_CLOSE:
        // Task 5 改为缩托盘；现阶段直接隐藏
        ShowWindow(m_hWnd, SW_HIDE);
        return 0;

    case WM_USER_RECORDING_STOPPED:
        KillTimer(m_hWnd, 1);
        ShowIdleState();
        return 0;

    case WM_TIMER:
        if (wp == 1)
        {
            // 录音中更新电平（从 CaptureEngine 的 onStatus 回调里已经推了，这里只做重绘）
        }
        else if (wp == 2)
        {
            // 空闲态+录音态都刷新列表
            OnRefresh();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(m_hWnd, msg, wp, lp);
}

// ---- 控件布局 ----
void MainWindow::LayoutControls(int w, int h)
{
    m_sessionList.Resize(w - 20, 180);
    if (m_hSaveLink)
        SetWindowPos(m_hSaveLink, nullptr, 10, h - 40, w - 20, 20, SWP_NOZORDER);
}

// ---- 状态切换 ----
void MainWindow::ShowIdleState()
{
    m_isRecording = false;
    ShowWindow(m_hStartBtn, SW_SHOW);
    ShowWindow(m_hStopBtn, SW_HIDE);
    ShowWindow(m_hTimerLabel, SW_HIDE);
    ShowWindow(m_hSysLevel, SW_HIDE);
    ShowWindow(m_hMicLevel, SW_HIDE);
    ShowWindow(m_hSizeLabel, SW_HIDE);
    ShowWindow(m_hHintLabel, SW_SHOW);
    ShowWindow(m_hRefreshBtn, SW_SHOW);
    ShowWindow(m_hMicCheck, SW_SHOW);
    ShowWindow(m_hSettingsBtn, SW_SHOW);
    // 启动空闲刷新定时器
    SetTimer(m_hWnd, 2, 1000, nullptr);
    m_sessionList.Refresh();
    // 不用 InvalidateRect（会刷掉按钮文字），让各控件自行重绘
}

void MainWindow::ShowRecordingState()
{
    m_isRecording = true;
    ShowWindow(m_hStartBtn, SW_HIDE);
    ShowWindow(m_hStopBtn, SW_SHOW);
    UpdateWindow(m_hStopBtn); // 即刻重绘按钮，不走父窗口擦除
    ShowWindow(m_hTimerLabel, SW_SHOW);
    ShowWindow(m_hSysLevel, SW_SHOW);
    ShowWindow(m_hMicLevel, SW_SHOW);
    ShowWindow(m_hSizeLabel, SW_SHOW);
    ShowWindow(m_hHintLabel, SW_HIDE);
    ShowWindow(m_hRefreshBtn, SW_HIDE);
    ShowWindow(m_hMicCheck, SW_HIDE);
    ShowWindow(m_hSettingsBtn, SW_HIDE);
    // 保留定时器 2 持续刷新列表（录音中也需要看最新状态）
    // 不用 InvalidateRect（会刷掉按钮文字）
}

// ---- 操作 ----
void MainWindow::OnRefresh()
{
    m_sessionList.Refresh();
}

void MainWindow::OnStart()
{
    // 获取选中的 PID——必须先在列表里点一下要录的软件
    int pid = m_sessionList.GetSelectedPid();
    if (pid == 0)
    {
        MessageBox(m_hWnd, L"请先在列表里点一下要录音的软件（比如微信），再点开始录音。", L"提示", MB_OK);
        return;
    }
    // 配置引擎
    m_engine.SetMicEnabled(m_micEnabled);
    m_engine.SetMicGain(m_micGain);
    m_engine.SetSystemGain(m_sysGain);

    // 确保 recordings 目录存在（解决双击打开时工作目录不是项目根的问题）
    WCHAR exeDir[MAX_PATH];
    GetModuleFileName(nullptr, exeDir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    std::wstring recDir = std::wstring(exeDir) + L"\\recordings";
    CreateDirectory(recDir.c_str(), nullptr);  // 已存在无害

    auto t = std::time(nullptr);
    tm localTime;
    localtime_s(&localTime, &t);
    WCHAR name[128];
    // 取目标进程名用于文件名
    std::wstring procName = L"系统混音";
    for (const auto& s : m_sessionList.GetCurrentSessions())
    {
        if (s.processId == (DWORD)pid) { procName = s.processName; break; }
    }
    wsprintfW(name, L"%04d-%02d-%02d %s %02d-%02d-%02d.m4a",
        localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
        procName.c_str(),
        localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    std::wstring path = recDir + L"\\" + std::wstring(name);
    m_engine.SetOutputPath(path);
    m_engine.SetOutputFormat(L"m4a");

    // 带状态的异步启动
    HRESULT hr = m_engine.Start(pid, true,
        [this](const CaptureStatus& st) { UpdateStatus(st); },
        [this](HRESULT) { PostMessage(m_hWnd, WM_USER_RECORDING_STOPPED, 0, 0); });

    if (FAILED(hr))
    {
        WCHAR msg[256];
        wsprintfW(msg, L"启动录音失败。\n错误码: 0x%08X\n目标 PID: %d\n\n请检查：\n"
            L"1. 是否在列表里选中了正在发声的软件？\n"
            L"2. 音频设备（扬声器/耳机）是否正常工作？",
            hr, pid);
        MessageBox(m_hWnd, msg, L"错误", MB_ICONERROR);
        return;
    }
    ShowRecordingState();
    SetTimer(m_hWnd, 1, 200, nullptr);
}

void MainWindow::OnStop()
{
    KillTimer(m_hWnd, 1);
    m_engine.Stop();
    ShowIdleState();
}

void MainWindow::OnSettings()
{
    // Task 4 实现
    MessageBox(m_hWnd, L"设置功能将在下一步实现", L"设置", MB_OK);
}

void MainWindow::OnSaveLink()
{
    // 打开保存目录
    ShellExecute(m_hWnd, L"open", m_outputPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- 电平绘制 ----
static std::wstring DrawLevelBar(int level)
{
    std::wstring bar;
    int filled = (level + 9) / 10;
    for (int i = 0; i < 10; i++)
        bar += (i < filled) ? L'#' : L'-';
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

    if (st.micEnabled)
    {
        std::wstring micText = L"麦克风 [" + DrawLevelBar(st.micLevel) + L"]";
        SetWindowText(m_hMicLevel, micText.c_str());
    }

    // 文件大小
    UINT64 bytes = st.bytesWritten;
    if (bytes < 1024 * 1024)
        wsprintfW(buf, L"文件大小: %u KB", (UINT)(bytes / 1024));
    else
        wsprintfW(buf, L"文件大小: %.1f MB", bytes / (1024.0 * 1024.0));
    SetWindowText(m_hSizeLabel, buf);
}
