/*
 * 轻量日志：写文本行到 exe 同目录的 ProcessAudioRecorder.log。
 * 记录：启动/退出、录音开始/停止、报错、时长与文件大小。
 */
#pragma once

#include <Windows.h>
#include <string>

class Logger
{
public:
    // 初始化日志文件路径（取 exe 目录）
    static void Init();

    // 写一行带时间戳的日志
    static void Log(const std::wstring& msg);

    // 格式化常用事件
    static void LogStartup();
    static void LogShutdown();
    static void LogRecStart(const std::wstring& target, const std::wstring& path, float sysGain, float micGain);
    static void LogRecStop(const std::wstring& path, UINT64 bytes, int elapsedSec, bool userStop);
    static void LogError(const std::wstring& ctx, HRESULT hr);
};
