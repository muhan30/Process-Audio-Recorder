/*
 * GUI 入口：WinMain → 主窗口 → 消息循环。
 */
#include <Windows.h>
#include <CommCtrl.h>
#include "MainWindow.h"

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    // COM 初始化（引擎依赖）
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrCom))
    {
        MessageBox(nullptr, L"COM 初始化失败，程序无法运行。", L"错误", MB_ICONERROR);
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
        return 1;
    }

    mainWin.RunMessageLoop();

    CoUninitialize();
    return 0;
}
