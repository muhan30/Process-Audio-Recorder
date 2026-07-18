/*
 * 主窗口：消息处理、控件布局、状态切换。
 * 待机态 = 列表 + 开始按钮；录音态 = 计时 + 电平 + 停止按钮。
 */
#pragma once

#include <Windows.h>
#include <CommCtrl.h>
#include <string>
#include <memory>
#include "SessionList.h"
#include "CaptureEngine.h"

#define WM_USER_RECORDING_STOPPED (WM_USER + 1)
#define WM_USER_TRAYICON        (WM_USER + 2)

#define IDM_TRAY_SHOW   3001
#define IDM_TRAY_STOP   3002
#define IDM_TRAY_EXIT   3003

class MainWindow
{
public:
    MainWindow(HINSTANCE hInst);
    ~MainWindow();

    bool CreateMainWindow(int nCmdShow);
    void RunMessageLoop();

private:
    HINSTANCE m_hInst;
    HWND m_hWnd = nullptr;

    // 子控件
    SessionList m_sessionList;
    HWND m_hRefreshBtn = nullptr;
    HWND m_hMicCheck = nullptr;
    HWND m_hStartBtn = nullptr;
    HWND m_hStopBtn = nullptr;
    HWND m_hSettingsBtn = nullptr;
    HWND m_hHintLabel = nullptr;
    HWND m_hTimerLabel = nullptr;
    HWND m_hSizeLabel = nullptr;
    HWND m_hSysLevel = nullptr;
    HWND m_hMicLevel = nullptr;
    HWND m_hSaveLink = nullptr;

    // 引擎
    CaptureEngine m_engine;
    std::wstring m_outputPath;
    std::wstring m_outputFormat{ L"m4a" };
    bool m_micEnabled = true;
    float m_sysGain = 2.0f;
    float m_micGain = 4.0f;
    bool m_isRecording = false;
    SYSTEMTIME m_recordingStartST = {};
    std::wstring m_lastRecordingPath;

    // 窗口过程
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // 布局
    void LayoutControls(int w, int h);
    void ShowIdleState();
    void ShowRecordingState();

    // 托盘
    NOTIFYICONDATA m_nid = {};
    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayIcon();
    void ShowTrayMenu();

    // 操作
    void OnRefresh();
    void OnStart();
    void OnStop();
    void OnSettings();
    bool OnExit();
    void UpdateStatus(const CaptureStatus& st);
    void LoadSettings();
    void SaveSettings();
};
