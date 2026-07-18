/*
 * 发声软件列表控件：封装 ListView + AudioSessionLister 刷新逻辑。
 */
#pragma once

#include <Windows.h>
#include <CommCtrl.h>
#include <vector>
#include "AudioSessionLister.h"

// 控件 ID
#define IDC_SESSION_LIST   1001
#define IDC_REFRESH_BTN    1002
#define IDC_MIC_CHECK      1003
#define IDC_START_BTN      1004
#define IDC_STOP_BTN       1005
#define IDC_SAVE_LINK      1006
#define IDC_SETTINGS_BTN   1007
#define IDC_SYS_GAIN_SLD   1008
#define IDC_MIC_GAIN_SLD   1009
#define IDC_GAIN_LABEL_SYS 1010
#define IDC_GAIN_LABEL_MIC 1011
#define IDC_SYS_LEVEL      1012
#define IDC_MIC_LEVEL      1013
#define IDC_TIMER_LABEL    1014
#define IDC_SIZE_LABEL     1015
#define IDC_HINT_LABEL     1016

class SessionList
{
public:
    SessionList();
    void Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h);
    void Refresh();
    std::vector<AudioSessionInfo> GetCurrentSessions() const { return m_sessions; }
    int GetSelectedPid() const;
    void Resize(int w, int h);

private:
    HWND m_hWnd = nullptr;
    std::vector<AudioSessionInfo> m_sessions;
    static LRESULT CALLBACK ListProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subId, DWORD_PTR ref);
};
