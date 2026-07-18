/*
 * GUI 入口：WinMain → 单实例检测 → 主窗口 → 消息循环。
 */
#include <Windows.h>
#include <CommCtrl.h>
#include "MainWindow.h"

static const WCHAR* MUTEX_NAME = L"Local\\ProcessAudioRecorderGUI_SingleInstance";

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    // 单实例检测
    HANDLE hMutex = CreateMutex(nullptr, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // 已有实例在运行，找到它的窗口并恢复
        HWND hExisting = FindWindow(L"AudioRecorderMainWindow", L"进程录音机");
        if (hExisting)
        {
            ShowWindow(hExisting, SW_SHOW);
            SetForegroundWindow(hExisting);
        }
        CloseHandle(hMutex);
        return 0;
    }

    // COM 初始化（引擎依赖）
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrCom))
    {
        MessageBox(nullptr, L"COM 初始化失败，程序无法运行。", L"错误", MB_ICONERROR);
        CloseHandle(hMutex);
        return 2;
    }

    // Common Controls 6
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    MainWindow mainWin(hInstance);
    if (!mainWin.CreateMainWindow(nCmdShow))
    {
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    mainWin.RunMessageLoop();

    CoUninitialize();
    CloseHandle(hMutex);
    return 0;
}
