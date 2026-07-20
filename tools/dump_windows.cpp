#include <Windows.h>
#include <stdio.h>

int main() {
    FILE* f = fopen("dump_windows.txt", "w");
    fwprintf(f, L"===  通话中运行，查找微信相关窗口 ===\n\n");

    // 1. 顶层窗口
    fwprintf(f, L"--- 顶层窗口 ---\n");
    EnumWindows([](HWND hWnd, LPARAM lp) -> BOOL {
        WCHAR cls[64] = {}, title[128] = {};
        GetClassNameW(hWnd, cls, 64);
        GetWindowTextW(hWnd, title, 128);
        FILE* ff = (FILE*)lp;
        fwprintf(ff, L"cls=[%s]  title=[%s]\n", cls, title);
        return TRUE;
    }, (LPARAM)f);

    // 2. Message-only 窗口
    fwprintf(f, L"\n--- Message-only 窗口 ---\n");
    HWND msgWnd = FindWindowExW(HWND_MESSAGE, NULL, NULL, NULL);
    while (msgWnd) {
        WCHAR cls[64] = {}, title[128] = {};
        GetClassNameW(msgWnd, cls, 64);
        GetWindowTextW(msgWnd, title, 128);
        fwprintf(f, L"cls=[%s]  title=[%s]\n", cls, title);
        msgWnd = FindWindowExW(HWND_MESSAGE, msgWnd, NULL, NULL);
    }

    // 3. 查找 WeChatMainWndForPC 子窗口
    HWND wechat = FindWindowW(L"WeChatMainWndForPC", NULL);
    if (!wechat) wechat = FindWindowW(L"Qt51514QWindowIcon", L"Weixin");
    if (wechat) {
        fwprintf(f, L"\n--- 微信主窗口子窗口 ---\n");
        EnumChildWindows(wechat, [](HWND hWnd, LPARAM lp) -> BOOL {
            WCHAR cls[64] = {}, title[128] = {};
            GetClassNameW(hWnd, cls, 64);
            GetWindowTextW(hWnd, title, 128);
            FILE* ff = (FILE*)lp;
            fwprintf(ff, L"cls=[%s]  title=[%s]\n", cls, title);
            return TRUE;
        }, (LPARAM)f);
    }

    fclose(f);
    MessageBoxW(NULL, L"dump_windows.txt    ", L"OK", MB_OK);
    return 0;
}
