#include "SessionList.h"
#include <CommCtrl.h>

SessionList::SessionList() = default;

void SessionList::Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h)
{
    m_hWnd = CreateWindowEx(0, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        x, y, w, h, parent, (HMENU)(UINT_PTR)IDC_SESSION_LIST, hInst, nullptr);

    // 启用整行选择和网格线
    ListView_SetExtendedListViewStyle(m_hWnd, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // 列
    LVCOLUMN col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 60;
    col.pszText = const_cast<LPWSTR>(L"PID");
    ListView_InsertColumn(m_hWnd, 0, &col);
    col.cx = 200;
    col.pszText = const_cast<LPWSTR>(L"进程");
    ListView_InsertColumn(m_hWnd, 1, &col);
    col.cx = 100;
    col.pszText = const_cast<LPWSTR>(L"状态");
    ListView_InsertColumn(m_hWnd, 2, &col);
}

void SessionList::Refresh()
{
    // 保存当前选中 PID，刷新后恢复
    int selectedPid = GetSelectedPid();

    ListView_DeleteAllItems(m_hWnd);
    m_sessions.clear();
    ListAudioSessions(m_sessions);

    int restoreIndex = -1;
    for (size_t i = 0; i < m_sessions.size(); i++)
    {
        const auto& s = m_sessions[i];
        WCHAR pidStr[32];
        wsprintfW(pidStr, L"%u", s.processId);

        LVITEM item = {};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.pszText = pidStr;
        ListView_InsertItem(m_hWnd, &item);

        ListView_SetItemText(m_hWnd, (int)i, 1, const_cast<LPWSTR>(s.processName.c_str()));
        ListView_SetItemText(m_hWnd, (int)i, 2,
            const_cast<LPWSTR>(s.isActive ? L"<<< 正在发声" : L"安静"));

        if (s.processId == (DWORD)selectedPid)
            restoreIndex = (int)i;
    }

    // 恢复选中
    if (restoreIndex >= 0)
    {
        ListView_SetItemState(m_hWnd, restoreIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(m_hWnd, restoreIndex, FALSE);
    }
}

int SessionList::GetSelectedPid() const
{
    int idx = ListView_GetNextItem(m_hWnd, -1, LVNI_SELECTED);
    if (idx < 0 || idx >= (int)m_sessions.size()) return 0;
    return (int)m_sessions[idx].processId;
}

void SessionList::Resize(int w, int h)
{
    if (m_hWnd)
        SetWindowPos(m_hWnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
}
