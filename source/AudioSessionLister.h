/*
 * 音频会话枚举模块：列出默认播放设备上的所有音频会话。
 * 会话上报的 PID 就是"直接向系统提交声音的进程"，
 * 用它作 --mode 1 的目标必然命中，解决多进程应用找不到发声 PID 的问题。
 */
#pragma once

#include <Windows.h>
#include <string>
#include <vector>

// 一条发声会话记录
struct AudioSessionInfo
{
    DWORD processId = 0;        // 进程 ID
    std::wstring processName;   // 进程 exe 名（不含路径）
    bool isActive = false;      // true = 正在发声（AudioSessionStateActive）
    bool isSystemSounds = false;// true = 系统提示音会话
};

// 枚举默认播放设备上的所有音频会话，正在发声的排前面
// 调用前须已完成 COM 初始化（CoInitializeEx）
HRESULT ListAudioSessions(std::vector<AudioSessionInfo>& sessions);
