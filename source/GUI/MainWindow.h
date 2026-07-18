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
    HWND m_hStatusBar = nullptr;

    // 子控件
    SessionList m_sessionList;
    HWND m_hRefreshBtn = nullptr;
    HWND m_hMicCheck = nullptr;
    HWND m_hStartBtn = nullptr;
    HWND m_hStopBtn = nullptr;
    HWND m_hSettingsBtn = nullptr;
    HWND m_hSaveLink = nullptr;
    HWND m_hHintLabel = nullptr;
    HWND m_hTimerLabel = nullptr;
    HWND m_hSizeLabel = nullptr;
    HWND m_hSysLevel = nullptr;
    HWND m_hMicLevel = nullptr;

    // 引擎
    CaptureEngine m_engine;
    std::wstring m_outputPath;
    std::wstring m_outputFormat{ L"m4a" };
    bool m_micEnabled = true;
    float m_sysGain = 2.0f;
    float m_micGain = 4.0f;
    bool m_isRecording = false;

    // 窗口过程
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // 布局
    void LayoutControls(int w, int h);
    void ShowIdleState();
    void ShowRecordingState();

    // 操作
    void OnRefresh();
    void OnStart();
    void OnStop();
    void OnSettings();
    void OnSaveLink();
    void UpdateStatus(const CaptureStatus& st);
};
