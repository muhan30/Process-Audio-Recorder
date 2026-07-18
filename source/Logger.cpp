#include "Logger.h"
#include <ctime>
#include <fstream>
#include <mutex>

static std::wstring g_logPath;
static std::mutex g_logMutex;

void Logger::Init()
{
    WCHAR exeDir[MAX_PATH];
    GetModuleFileName(nullptr, exeDir, MAX_PATH);
    WCHAR* last = wcsrchr(exeDir, L'\\');
    if (last) *(last + 1) = L'\0';
    g_logPath = std::wstring(exeDir) + L"ProcessAudioRecorder.log";
}

void Logger::Log(const std::wstring& msg)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    auto t = std::time(nullptr);
    tm localTime;
    localtime_s(&localTime, &t);
    WCHAR ts[32];
    wsprintfW(ts, L"%04d-%02d-%02d %02d:%02d:%02d",
        localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
        localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

    std::wofstream f(g_logPath, std::ios::app);
    if (f.is_open())
    {
        f << L"[" << ts << L"] " << msg << std::endl;
    }
}

void Logger::LogStartup()               { Log(L"APP START"); }
void Logger::LogShutdown()              { Log(L"APP EXIT"); }

void Logger::LogRecStart(const std::wstring& target, const std::wstring& path,
                          float sysGain, float micGain)
{
    WCHAR buf[512];
    swprintf_s(buf, L"REC START | target=%s | file=%s | sys_gain=%.1f mic_gain=%.1f",
        target.c_str(), path.c_str(), sysGain, micGain);
    Log(buf);
}

void Logger::LogRecStop(const std::wstring& path, UINT64 bytes, int elapsedSec, bool userStop)
{
    WCHAR buf[512];
    const WCHAR* reason = userStop ? L"user" : L"auto";
    swprintf_s(buf, L"REC STOP  | file=%s | size=%llu B | duration=%ds | reason=%s",
        path.c_str(), bytes, elapsedSec, reason);
    Log(buf);
}

void Logger::LogError(const std::wstring& ctx, HRESULT hr)
{
    WCHAR buf[512];
    swprintf_s(buf, L"ERROR     | context=%s | hr=0x%08X", ctx.c_str(), hr);
    Log(buf);
}
