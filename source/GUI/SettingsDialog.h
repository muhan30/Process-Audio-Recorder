/*
 * 设置对话框：增益滑块 + 输出格式，.ini 持久化。路径写死不在此修改。
 */
#pragma once

#include <Windows.h>
#include <CommCtrl.h>
#include <string>

#define IDC_SETTINGS_OK     2001
#define IDC_SETTINGS_CANCEL 2002
#define IDC_FMT_M4A         2003
#define IDC_FMT_WAV         2004
#define IDC_SYS_GAIN_TRK    2005
#define IDC_MIC_GAIN_TRK    2006

struct SettingsData
{
    std::wstring savePath;
    std::wstring outputFormat = L"m4a";
    float sysGain = 2.0f;
    float micGain = 4.0f;
    float lowpassCutoff = 10.0f;  // kHz, 0 = 关闭低通滤波
};

class SettingsDialog
{
public:
    static bool Show(HWND parent, HINSTANCE hInst, SettingsData& data);
    static void LoadFromIni(SettingsData& data);
    static void SaveToIni(const SettingsData& data);
};
