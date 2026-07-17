# 阶段一：引擎功能实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有命令行程序上完成录音引擎全部核心能力：发声进程列表、Sink 输出抽象（WAV/M4A）、麦克风混音、实时电平、崩溃保护，每个任务用户可亲手验证。

**Architecture:** 引擎层新增 AudioSessionLister / AudioSink(WavSink,M4aSink) / MicCapture / AudioMixer / LevelMeter 模块；CLoopbackCapture 保留捕获职责，文件写入迁出到 Sink，新增数据分流回调（tap）供混音接入。wmain 负责组装。上游规格：`docs/superpowers/specs/2026-07-17-process-recorder-app-design.md`。

**Tech Stack:** C++17 / CMake+Ninja / WASAPI（环回+麦克风）/ Media Foundation（AAC 编码 IMFSinkWriter）/ WIL。

**通用约定：**
- 构建命令一律为：`cmake --build build`（在仓库根目录执行），期望零 error。
- 每个任务完成即 commit；提交信息用英文，风格与现有历史一致。
- 控制台输出用 ASCII 字符画电平/标记（`#`、`-`、`<<<`），避免 GBK 终端乱码。
- 所有新文件使用 UTF-8 编码，中文注释密度与现有代码保持一致。
- 用户验证步骤（标 👤）是每个任务的验收关卡，必须等用户确认后才进入下一任务。

---

### Task 1: AudioSessionLister —— `--list` 列出正在发声的软件

**Files:**
- Create: `source/AudioSessionLister.h`
- Create: `source/AudioSessionLister.cpp`
- Modify: `source/ProcessAudioRecorder.cpp`（加 `--list` 分支 + 统一 COM 初始化）
- Modify: `CMakeLists.txt`（加源文件）

- [ ] **Step 1.1: 写 `source/AudioSessionLister.h`**

```cpp
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
```

- [ ] **Step 1.2: 写 `source/AudioSessionLister.cpp`**

```cpp
#include "AudioSessionLister.h"

#include <mmdeviceapi.h>   // IMMDeviceEnumerator
#include <audiopolicy.h>   // IAudioSessionManager2 / IAudioSessionControl2
#include <algorithm>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

// 由 PID 取进程 exe 名（失败返回空串）
static std::wstring GetProcessNameByPid(DWORD pid)
{
    wil::unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process)
    {
        return L"";
    }
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(process.get(), 0, path, &size))
    {
        return L"";
    }
    std::wstring fullPath(path);
    size_t pos = fullPath.find_last_of(L'\\');
    return (pos == std::wstring::npos) ? fullPath : fullPath.substr(pos + 1);
}

HRESULT ListAudioSessions(std::vector<AudioSessionInfo>& sessions)
{
    sessions.clear();

    // 1. 取默认播放设备
    wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
    RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
    wil::com_ptr_nothrow<IMMDevice> device;
    RETURN_IF_FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));

    // 2. 激活会话管理器
    wil::com_ptr_nothrow<IAudioSessionManager2> sessionManager;
    RETURN_IF_FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, sessionManager.put_void()));

    // 3. 枚举所有会话
    wil::com_ptr_nothrow<IAudioSessionEnumerator> sessionEnum;
    RETURN_IF_FAILED(sessionManager->GetSessionEnumerator(&sessionEnum));
    int count = 0;
    RETURN_IF_FAILED(sessionEnum->GetCount(&count));

    for (int i = 0; i < count; i++)
    {
        wil::com_ptr_nothrow<IAudioSessionControl> control;
        if (FAILED(sessionEnum->GetSession(i, &control)))
        {
            continue;
        }
        wil::com_ptr_nothrow<IAudioSessionControl2> control2;
        if (FAILED(control->QueryInterface(IID_PPV_ARGS(&control2))))
        {
            continue;
        }

        AudioSessionInfo info;
        if (FAILED(control2->GetProcessId(&info.processId)))
        {
            continue;
        }
        info.isSystemSounds = (control2->IsSystemSoundsSession() == S_OK);

        AudioSessionState state = AudioSessionStateInactive;
        control->GetState(&state);
        info.isActive = (state == AudioSessionStateActive);

        info.processName = GetProcessNameByPid(info.processId);
        if (info.processName.empty())
        {
            if (info.isSystemSounds)
            {
                info.processName = L"[System Sounds]";
            }
            else
            {
                continue;  // 拿不到名字且非系统音，跳过
            }
        }
        sessions.push_back(std::move(info));
    }

    // 4. 正在发声的排前面（稳定排序保持系统枚举顺序）
    std::stable_sort(sessions.begin(), sessions.end(),
        [](const AudioSessionInfo& a, const AudioSessionInfo& b)
        {
            return a.isActive > b.isActive;
        });
    return S_OK;
}
```

- [ ] **Step 1.3: 修改 `source/ProcessAudioRecorder.cpp`**

三处改动：

(a) 文件头 include 区加：

```cpp
#include <iomanip>   // 已有则不重复
#include "AudioSessionLister.h"
```

(b) `wmain` 开头（`_wsetlocale` 之后）加统一 COM 初始化——现有代码依赖隐式初始化，显式化更稳：

```cpp
	HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hrCom)) {
		std::wcout << L"Error: COM initialization failed. 0x" << std::hex << hrCom << std::endl;
		return 2;
	}
```

(c) COM 初始化之后、`ParseCommandLine` 之前加 `--list` 分支：

```cpp
	if (argc >= 2 && wcscmp(argv[1], L"--list") == 0) {
		std::vector<AudioSessionInfo> sessions;
		HRESULT hr = ListAudioSessions(sessions);
		if (FAILED(hr)) {
			std::wcout << L"Error: failed to enumerate audio sessions. 0x" << std::hex << hr << std::endl;
			return 2;
		}
		if (sessions.empty()) {
			std::wcout << L"No app is using audio right now.\n"
				<< L"Tip: start playing sound in the target app, then run --list again." << std::endl;
			return 0;
		}
		std::wcout << L"Apps using audio (louder ones first):\n\n";
		std::wcout << std::setw(8) << L"PID" << L"  "
			<< std::left << std::setw(28) << L"Process" << std::right
			<< L"Status" << L"\n";
		std::wcout << L"--------  ----------------------------  ---------------\n";
		for (const auto& s : sessions) {
			std::wcout << std::setw(8) << s.processId << L"  "
				<< std::left << std::setw(28) << s.processName << std::right
				<< (s.isActive ? L"<<< PLAYING" : L"silent") << L"\n";
		}
		std::wcout << L"\nTo record one of them:\n"
			<< L"  ProcessAudioRecorder --pid <PID> --mode 1 --path D:\\rec.wav" << std::endl;
		return 0;
	}
```

`wcscmp` 需要 `<cwchar>`（`<Windows.h>` 环境下已可用，无需新增）。

- [ ] **Step 1.4: 修改 `CMakeLists.txt`**

`add_executable` 块加一行：

```cmake
add_executable(ProcessAudioRecorder
    source/ProcessAudioRecorder.cpp
    source/LoopbackCapture.cpp
    source/AudioSessionLister.cpp
)
```

- [ ] **Step 1.5: 编译**

Run: `cmake --build build`
Expected: 零 error，生成 `build/ProcessAudioRecorder.exe`。

- [ ] **Step 1.6: 👤 用户验证**

请用户操作：
1. 打开酷狗音乐并播放一首歌；
2. 终端运行 `build\ProcessAudioRecorder.exe --list`；
3. 预期：列表出现 KuGou 相关进程且状态为 `<<< PLAYING`，其余安静软件显示 `silent`；
4. 加分验证：拿该 PID 跑 `--pid <PID> --mode 1 --path D:\test_kugou.wav`，录 10 秒 Ctrl+C，文件能听到歌声——**直接终结"盲试 PID"痛点**。

- [ ] **Step 1.7: Commit**

```bash
git add source/AudioSessionLister.h source/AudioSessionLister.cpp source/ProcessAudioRecorder.cpp CMakeLists.txt
git commit -m "Add --list to enumerate audio sessions with PID and playing state"
```

---

### Task 2: AudioSink 抽象重构 + 已知问题修复（行为不变的纯重构）

把"往哪写、写成什么格式"从 CLoopbackCapture 中剥离成 AudioSink 接口，为 Task 3 的 M4A 铺路；顺带修复 Wiki 已知问题 K2/K3/K4/K5/K7。**本任务完成后录音行为必须与重构前完全一致。**

**Files:**
- Create: `source/AudioSink.h`（接口）
- Create: `source/WavSink.h`
- Create: `source/WavSink.cpp`（现有 WAV 逻辑迁入）
- Modify: `source/LoopbackCapture.h`（删文件成员，持 Sink 指针）
- Modify: `source/LoopbackCapture.cpp`（K3/K4/K5 修复 + 写入改走 Sink）
- Modify: `source/ProcessAudioRecorder.cpp`（K2/K7 修复 + 注入 WavSink）
- Modify: `CMakeLists.txt`

- [ ] **Step 2.1: 写 `source/AudioSink.h`**

```cpp
/*
 * 音频输出接口：捕获引擎只管产出 PCM 数据块，
 * 具体写成 WAV 还是 M4A 由注入的 Sink 实现决定。
 * 约定：Initialize → 多次 WriteChunk（单线程调用）→ Finalize（必调，负责收尾与资源释放）
 */
#pragma once

#include <Windows.h>
#include <mmreg.h>

class AudioSink
{
public:
    virtual ~AudioSink() = default;

    // 打开输出文件并做好写入准备（format 为送入的 PCM 格式）
    virtual HRESULT Initialize(PCWSTR filePath, const WAVEFORMATEX& format) = 0;

    // 写入一块 PCM 数据（由写入线程串行调用）
    virtual HRESULT WriteChunk(const BYTE* data, DWORD size) = 0;

    // 收尾：补文件头/结束编码，无论成败都会被调用一次
    virtual HRESULT Finalize() = 0;

    // 已写入的音频数据字节数（供进度显示）
    virtual UINT64 BytesWritten() const = 0;
};
```

- [ ] **Step 2.2: 写 `source/WavSink.h`**

```cpp
/*
 * WAV 输出实现：RIFF 头占位 → 追加 PCM → Finalize 回填大小。
 * 逻辑自 CLoopbackCapture::CreateWAVFile / FixWAVHeader 迁入，行为不变。
 */
#pragma once

#include "AudioSink.h"
#include <wil/resource.h>

class WavSink : public AudioSink
{
public:
    HRESULT Initialize(PCWSTR filePath, const WAVEFORMATEX& format) override;
    HRESULT WriteChunk(const BYTE* data, DWORD size) override;
    HRESULT Finalize() override;
    UINT64 BytesWritten() const override { return m_cbDataSize; }

private:
    wil::unique_hfile m_hFile;     // 输出文件句柄
    DWORD m_cbHeaderSize = 0;      // WAV 文件头大小
    DWORD m_cbDataSize = 0;        // 已写入的音频数据大小
    bool m_finalized = false;      // 防止重复收尾
};
```

- [ ] **Step 2.3: 写 `source/WavSink.cpp`**

```cpp
#include "WavSink.h"

#include <mfapi.h>       // FCC 宏
#include <wil/result.h>

// 创建 WAV 文件并写入文件头（大小字段先占位为 0）
HRESULT WavSink::Initialize(PCWSTR filePath, const WAVEFORMATEX& format)
{
    m_hFile.reset(CreateFile(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    RETURN_LAST_ERROR_IF(!m_hFile);

    // RIFF 和 fmt 块头
    DWORD header[] = { FCC('RIFF'), 0, FCC('WAVE'), FCC('fmt '), sizeof(WAVEFORMATEX) };
    DWORD dwBytesWritten = 0;
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), header, sizeof(header), &dwBytesWritten, NULL));
    m_cbHeaderSize += dwBytesWritten;

    // 音频格式信息
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &format, sizeof(format), &dwBytesWritten, NULL));
    m_cbHeaderSize += dwBytesWritten;

    // data 块头
    DWORD data[] = { FCC('data'), 0 };
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), data, sizeof(data), &dwBytesWritten, NULL));
    m_cbHeaderSize += dwBytesWritten;

    return S_OK;
}

HRESULT WavSink::WriteChunk(const BYTE* data, DWORD size)
{
    // K6 保护：WAV 格式上限 4GB，逼近上限时报错停录，防止大小字段回绕
    RETURN_HR_IF(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), m_cbDataSize > 0xFFFFFFFFu - size - 1024);

    DWORD dwBytesWritten = 0;
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), data, size, &dwBytesWritten, NULL));
    m_cbDataSize += dwBytesWritten;
    return S_OK;
}

// 回填 data 大小与 RIFF 总大小
HRESULT WavSink::Finalize()
{
    if (m_finalized || !m_hFile)
    {
        return S_OK;
    }
    m_finalized = true;

    DWORD dwPtr = SetFilePointer(m_hFile.get(), m_cbHeaderSize - sizeof(DWORD), NULL, FILE_BEGIN);
    RETURN_LAST_ERROR_IF(INVALID_SET_FILE_POINTER == dwPtr);
    DWORD dwBytesWritten = 0;
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &m_cbDataSize, sizeof(DWORD), &dwBytesWritten, NULL));

    RETURN_LAST_ERROR_IF(INVALID_SET_FILE_POINTER == SetFilePointer(m_hFile.get(), sizeof(DWORD), NULL, FILE_BEGIN));
    DWORD cbTotalSize = m_cbDataSize + m_cbHeaderSize - 8;
    RETURN_IF_WIN32_BOOL_FALSE(WriteFile(m_hFile.get(), &cbTotalSize, sizeof(DWORD), &dwBytesWritten, NULL));

    RETURN_IF_WIN32_BOOL_FALSE(FlushFileBuffers(m_hFile.get()));
    return S_OK;
}
```

- [ ] **Step 2.4: 修改 `source/LoopbackCapture.h`**

四处改动：
(a) include 区加 `class AudioSink;` 前向声明（放在类定义前任意位置）；
(b) public 区加注入方法：

```cpp
 // 注入输出 Sink（须在 StartXXXCaptureAsync 之前调用，生命周期由调用方管理）
 void SetAudioSink(AudioSink* sink) { m_pSink = sink; }
```

(c) private 区删除以下三个成员声明及 `CreateWAVFile()`、`FixWAVHeader()` 两个方法声明：

```cpp
 wil::unique_hfile m_hFile;
 DWORD m_cbHeaderSize = 0;
 DWORD m_cbDataSize = 0;
```

(d) private 区加：

```cpp
 AudioSink* m_pSink = nullptr;                           // 输出 Sink（不持有所有权）
```

- [ ] **Step 2.5: 修改 `source/LoopbackCapture.cpp`**

五处改动：

(a) include 区加 `#include "AudioSink.h"`；

(b) **删除**整个 `CreateWAVFile()` 与 `FixWAVHeader()` 函数定义；

(c) `ActivateCompleted`（进程模式）与 `ActivateAudioInterfaceGlobal`（全局模式）中，原 `RETURN_IF_FAILED(CreateWAVFile());` 一行均替换为：

```cpp
			// 初始化输出 Sink（未注入则报错）
			RETURN_HR_IF(E_POINTER, m_pSink == nullptr);
			RETURN_IF_FAILED(m_pSink->Initialize(m_outputFileName, m_CaptureFormat));
```

(d) **K3 修复**：`ActivateCompleted` 中 `m_AudioClient->Initialize` 调用改为与全局模式一致（AUTOCONVERTPCM 并入 flags、periodicity 传 0）：

```cpp
			RETURN_IF_FAILED(m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
				AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
				200000,
				0,
				&m_CaptureFormat,
				nullptr));
```

> 回退预案：进程模式实测若激活/录音失败，恢复原第 4 参写法并在 PROJECT_WIKI.md K3 记录"虚拟环回设备要求该写法"的实测结论。

(e) **K4 修复**：`WriterThreadProc` 整体替换为（循环条件不再无锁读队列；Finalize 必调）：

```cpp
// 写入线程处理函数
void CLoopbackCapture::WriterThreadProc()
{
	for (;;)
	{
		std::vector<BYTE> audioData;
		{
			std::unique_lock<std::mutex> lock(m_QueueMutex);
			// 等待新数据或停止信号（谓词防虚假唤醒）
			m_QueueCV.wait(lock, [this] {
				return !m_AudioQueue.empty() || !m_bIsCapturing;
				});
			if (m_AudioQueue.empty())
			{
				if (!m_bIsCapturing)
				{
					break;  // 已停止且队列排空，收工
				}
				continue;
			}
			audioData = std::move(m_AudioQueue.front());
			m_AudioQueue.pop();
		}

		// 写入 Sink（写失败记录首个错误，继续排空队列避免生产端阻塞）
		if (!audioData.empty() && m_pSink && SUCCEEDED(m_writerThreadResult))
		{
			HRESULT hr = m_pSink->WriteChunk(audioData.data(), static_cast<DWORD>(audioData.size()));
			if (FAILED(hr))
			{
				m_writerThreadResult = hr;
			}
		}
	}

	// 收尾（无论成败都要调，保证资源释放；错误保留首个）
	if (m_pSink)
	{
		HRESULT hr = m_pSink->Finalize();
		if (SUCCEEDED(m_writerThreadResult) && FAILED(hr))
		{
			m_writerThreadResult = hr;
		}
	}
}
```

(f) **K5 修复**：`StopCaptureAsync` 开头两个判断交换顺序：

```cpp
	// 已在停止流程中则直接返回
	if (m_DeviceState == DeviceState::Stopping || m_DeviceState == DeviceState::Stopped)
	{
		return S_OK;
	}
	// 仅捕获中或错误态允许停止
	RETURN_HR_IF(E_NOT_VALID_STATE, (m_DeviceState != DeviceState::Capturing) && (m_DeviceState != DeviceState::Error));
```

- [ ] **Step 2.6: 修改 `source/ProcessAudioRecorder.cpp`**

三处改动：

(a) include 区加 `#include "WavSink.h"`；

(b) `wmain` 中 `CLoopbackCapture loopbackCapture;` 之后加注入（**K2 顺带修复**：删除其后的 `HANDLE hStopEvent = ...; if (hStopEvent != NULL) { }` 死代码段）：

```cpp
	WavSink wavSink;
	loopbackCapture.SetAudioSink(&wavSink);
```

(c) **K7 修复**：`loopbackCapture.StopCaptureAsync();` 一行替换为：

```cpp
	HRESULT hrStop = loopbackCapture.StopCaptureAsync();
	std::wcout << L"Finishing capture and saving file..." << std::endl;
	if (FAILED(hrStop)) {
		std::wcout << L"Warning: recording stopped with error 0x" << std::hex << hrStop
			<< L". The file may be incomplete." << std::endl;
	}
```

（原先无条件打印 "Capture completed. File saved to..." 的行保留在成功分支：`if (SUCCEEDED(hrStop))` 时打印。）

- [ ] **Step 2.7: 修改 `CMakeLists.txt`**

```cmake
add_executable(ProcessAudioRecorder
    source/ProcessAudioRecorder.cpp
    source/LoopbackCapture.cpp
    source/AudioSessionLister.cpp
    source/WavSink.cpp
)
```

- [ ] **Step 2.8: 编译**

Run: `cmake --build build`
Expected: 零 error。

- [ ] **Step 2.9: 👤 用户验证（重构后行为必须与之前一致）**

1. 全局模式：`build\ProcessAudioRecorder.exe --mode 0 --path D:\test_global.wav`，放歌 5 秒 Ctrl+C → 文件能正常播放；
2. 进程模式：先 `--list` 拿酷狗 PID，`--pid <PID> --mode 1 --path D:\test_proc.wav` 录 5 秒 → 只有酷狗声音（**此步同时验证 K3 修复无回归**）；
3. 两个文件在播放器里时长、音质与重构前无差别。

- [ ] **Step 2.10: Commit**

```bash
git add source/AudioSink.h source/WavSink.h source/WavSink.cpp source/LoopbackCapture.h source/LoopbackCapture.cpp source/ProcessAudioRecorder.cpp CMakeLists.txt
git commit -m "Extract AudioSink abstraction (WavSink) and fix K2-K5, K7 issues"
```

---

### Task 3: M4aSink —— 压缩格式输出（AAC/M4A）

用 Media Foundation 内置 AAC 编码器（Windows 自带，零第三方依赖）实现 M4A 输出，`--format` 切换，默认 m4a。

**Files:**
- Create: `source/M4aSink.h`
- Create: `source/M4aSink.cpp`
- Modify: `source/ProcessAudioRecorder.cpp`（--format 参数 + 按格式注入 Sink）
- Modify: `CMakeLists.txt`

- [ ] **Step 3.1: 写 `source/M4aSink.h`**

```cpp
/*
 * M4A（AAC）输出实现：IMFSinkWriter + Windows 内置 AAC 编码器。
 * 输入 PCM（与捕获格式一致），输出 128kbps AAC / MP4 容器。
 * 30 分钟录音约 28MB，体积为 WAV 的 1/10。
 */
#pragma once

#include "AudioSink.h"
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wil/com.h>

class M4aSink : public AudioSink
{
public:
    ~M4aSink() override;
    HRESULT Initialize(PCWSTR filePath, const WAVEFORMATEX& format) override;
    HRESULT WriteChunk(const BYTE* data, DWORD size) override;
    HRESULT Finalize() override;
    UINT64 BytesWritten() const override { return m_cbPcmWritten; }

private:
    wil::com_ptr_nothrow<IMFSinkWriter> m_writer;  // MF 写入器（含编码）
    DWORD m_streamIndex = 0;      // 音频流索引
    UINT64 m_cbPcmWritten = 0;    // 已送入编码器的 PCM 字节数
    DWORD m_avgBytesPerSec = 0;   // PCM 每秒字节数（时间戳换算用）
    bool m_mfStarted = false;     // MFStartup 配对标志
    bool m_finalized = false;     // 防止重复收尾
};
```

- [ ] **Step 3.2: 写 `source/M4aSink.cpp`**

```cpp
#include "M4aSink.h"

#include <mfapi.h>
#include <wil/result.h>

M4aSink::~M4aSink()
{
    Finalize();  // 兜底：调用方漏调时也保证资源释放
}

HRESULT M4aSink::Initialize(PCWSTR filePath, const WAVEFORMATEX& format)
{
    // SinkWriter 需要完整 MF 平台（引擎用的是 MFSTARTUP_LITE，计数式叠加一次 FULL）
    RETURN_IF_FAILED(MFStartup(MF_VERSION));
    m_mfStarted = true;
    m_avgBytesPerSec = format.nAvgBytesPerSec;

    RETURN_IF_FAILED(MFCreateSinkWriterFromURL(filePath, nullptr, nullptr, &m_writer));

    // 输出流：AAC，44.1kHz/立体声/128kbps（16000 字节每秒）
    wil::com_ptr_nothrow<IMFMediaType> outType;
    RETURN_IF_FAILED(MFCreateMediaType(&outType));
    RETURN_IF_FAILED(outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    RETURN_IF_FAILED(outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
    RETURN_IF_FAILED(outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, format.nSamplesPerSec));
    RETURN_IF_FAILED(outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, format.nChannels));
    RETURN_IF_FAILED(outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    RETURN_IF_FAILED(outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000));
    RETURN_IF_FAILED(m_writer->AddStream(outType.get(), &m_streamIndex));

    // 输入流：PCM，与捕获格式完全一致
    wil::com_ptr_nothrow<IMFMediaType> inType;
    RETURN_IF_FAILED(MFCreateMediaType(&inType));
    RETURN_IF_FAILED(inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    RETURN_IF_FAILED(inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, format.nSamplesPerSec));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, format.nChannels));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, format.wBitsPerSample));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, format.nBlockAlign));
    RETURN_IF_FAILED(inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, format.nAvgBytesPerSec));
    RETURN_IF_FAILED(m_writer->SetInputMediaType(m_streamIndex, inType.get(), nullptr));

    RETURN_IF_FAILED(m_writer->BeginWriting());
    return S_OK;
}

HRESULT M4aSink::WriteChunk(const BYTE* data, DWORD size)
{
    RETURN_HR_IF(E_NOT_VALID_STATE, !m_writer);
    RETURN_HR_IF(S_OK, size == 0);

    // PCM 块包装成 IMFSample
    wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
    RETURN_IF_FAILED(MFCreateMemoryBuffer(size, &buffer));
    BYTE* dst = nullptr;
    RETURN_IF_FAILED(buffer->Lock(&dst, nullptr, nullptr));
    memcpy(dst, data, size);
    buffer->Unlock();
    RETURN_IF_FAILED(buffer->SetCurrentLength(size));

    wil::com_ptr_nothrow<IMFSample> sample;
    RETURN_IF_FAILED(MFCreateSample(&sample));
    RETURN_IF_FAILED(sample->AddBuffer(buffer.get()));

    // 时间戳按累计 PCM 字节数换算（单位 100 纳秒）
    const LONGLONG hnsPerSec = 10000000;
    LONGLONG rtStart = static_cast<LONGLONG>(m_cbPcmWritten) * hnsPerSec / m_avgBytesPerSec;
    LONGLONG rtDuration = static_cast<LONGLONG>(size) * hnsPerSec / m_avgBytesPerSec;
    RETURN_IF_FAILED(sample->SetSampleTime(rtStart));
    RETURN_IF_FAILED(sample->SetSampleDuration(rtDuration));

    RETURN_IF_FAILED(m_writer->WriteSample(m_streamIndex, sample.get()));
    m_cbPcmWritten += size;
    return S_OK;
}

HRESULT M4aSink::Finalize()
{
    if (m_finalized)
    {
        return S_OK;
    }
    m_finalized = true;

    HRESULT hr = S_OK;
    if (m_writer)
    {
        hr = m_writer->Finalize();  // 冲刷编码器并写 MP4 索引（moov）
        m_writer.reset();
    }
    if (m_mfStarted)
    {
        MFShutdown();
        m_mfStarted = false;
    }
    return hr;
}
```

- [ ] **Step 3.3: 修改 `source/ProcessAudioRecorder.cpp`**

四处改动：

(a) include 区加：

```cpp
#include <memory>
#include "M4aSink.h"
```

(b) `CommandLineArgs` 结构体加字段：

```cpp
	std::wstring format;  // 输出格式："m4a"（默认）或 "wav"
```

(c) `ParseCommandLine` 中，`--path` 校验段之后加格式解析（缺省按扩展名推断，`.wav` 结尾 → wav，其余 → m4a）：

```cpp
	if (params.find(L"format") != params.end()) {
		args.format = params[L"format"];
		if (args.format != L"m4a" && args.format != L"wav") {
			args.errorMessage = L"Error: Invalid format: " + args.format +
				L"\nMust be m4a or wav.";
			return args;
		}
	}
	else {
		std::wstring lowerPath = args.outputPath;
		for (auto& ch : lowerPath) ch = towlower(ch);
		const std::wstring wavExt = L".wav";
		args.format = (lowerPath.size() >= wavExt.size() &&
			lowerPath.compare(lowerPath.size() - wavExt.size(), wavExt.size(), wavExt) == 0)
			? L"wav" : L"m4a";
	}
```

(d) `wmain` 中 Task 2 注入的 `WavSink wavSink; loopbackCapture.SetAudioSink(&wavSink);` 两行替换为按格式选择：

```cpp
	std::unique_ptr<AudioSink> sink;
	if (args.format == L"wav") {
		sink = std::make_unique<WavSink>();
	}
	else {
		sink = std::make_unique<M4aSink>();
	}
	loopbackCapture.SetAudioSink(sink.get());
	std::wcout << L"Output format: " << args.format << std::endl;
```

同时更新 `usage()`，Options 列表加一行：

```cpp
			<< L"  --format <F>   Output format: m4a (default, compressed) or wav (lossless)\n"
```

- [ ] **Step 3.4: 修改 `CMakeLists.txt`**

```cmake
add_executable(ProcessAudioRecorder
    source/ProcessAudioRecorder.cpp
    source/LoopbackCapture.cpp
    source/AudioSessionLister.cpp
    source/WavSink.cpp
    source/M4aSink.cpp
)
```

- [ ] **Step 3.5: 编译**

Run: `cmake --build build`
Expected: 零 error。

- [ ] **Step 3.6: 👤 用户验证**

1. `build\ProcessAudioRecorder.exe --mode 0 --path D:\test.m4a`，放歌录 30 秒 Ctrl+C；
2. `build\ProcessAudioRecorder.exe --mode 0 --path D:\test.wav`，同一首歌录 30 秒 Ctrl+C；
3. 预期：两个文件都能双击播放且听感一致；`test.m4a` 约 0.5MB，`test.wav` 约 5MB——**体积缩小约 10 倍**；
4. `--format wav --path D:\force.m4a` 应生成 WAV 内容（--format 优先于扩展名，文件后缀名不影响实际内容）。

- [ ] **Step 3.7: Commit**

```bash
git add source/M4aSink.h source/M4aSink.cpp source/ProcessAudioRecorder.cpp CMakeLists.txt
git commit -m "Add M4aSink with MF AAC encoder and --format option"
```

---

### Task 4: MicCapture —— 麦克风采集（独立验证通路）

麦克风 WASAPI 采集模块 + `--mic-test` 自检命令（录 5 秒麦克风存 WAV）。先把麦克风通路单独跑通，Task 5 才做混音集成。

**Files:**
- Create: `source/MicCapture.h`
- Create: `source/MicCapture.cpp`
- Modify: `source/ProcessAudioRecorder.cpp`（加 `--mic-test` 分支）
- Modify: `CMakeLists.txt`

- [ ] **Step 4.1: 写 `source/MicCapture.h`**

```cpp
/*
 * 麦克风采集模块：WASAPI 事件驱动捕获默认输入设备。
 * 输出统一为 44.1kHz/16bit/立体声（AUTOCONVERTPCM 负责重采样与声道转换），
 * 与环回捕获格式一致，便于混音。
 * 线程模型：专用采集线程等待事件（比 MF 工作队列简单，采集一路足够）。
 */
#pragma once

#include <AudioClient.h>
#include <mmdeviceapi.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <atomic>
#include <functional>
#include <thread>

class MicCapture
{
public:
    // 音频数据回调：每采到一块 PCM 调用一次（在采集线程上执行，须快速返回）
    using DataCallback = std::function<void(const BYTE* data, DWORD size)>;

    ~MicCapture();

    // 打开默认麦克风并配置格式（须先完成 COM 初始化）
    HRESULT Initialize(DataCallback callback);

    // 开始采集（启动采集线程）
    HRESULT Start();

    // 停止采集（等待线程退出，幂等）
    void Stop();

    const WAVEFORMATEX& Format() const { return m_format; }

private:
    void CaptureThreadProc();  // 采集线程主循环

    wil::com_ptr_nothrow<IAudioClient> m_audioClient;
    wil::com_ptr_nothrow<IAudioCaptureClient> m_captureClient;
    wil::unique_event_nothrow m_sampleReadyEvent;  // 数据就绪事件
    WAVEFORMATEX m_format{};
    DataCallback m_callback;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
};
```

- [ ] **Step 4.2: 写 `source/MicCapture.cpp`**

```cpp
#include "MicCapture.h"

#include <vector>
#include <wil/result.h>

#define BITS_PER_BYTE 8

MicCapture::~MicCapture()
{
    Stop();
}

HRESULT MicCapture::Initialize(DataCallback callback)
{
    m_callback = std::move(callback);

    // 目标格式与环回捕获一致，设备差异交给 AUTOCONVERTPCM 自动转换
    m_format.wFormatTag = WAVE_FORMAT_PCM;
    m_format.nChannels = 2;
    m_format.nSamplesPerSec = 44100;
    m_format.wBitsPerSample = 16;
    m_format.nBlockAlign = m_format.nChannels * m_format.wBitsPerSample / BITS_PER_BYTE;
    m_format.nAvgBytesPerSec = m_format.nSamplesPerSec * m_format.nBlockAlign;
    m_format.cbSize = 0;

    // 默认输入设备（麦克风）
    wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
    RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
    wil::com_ptr_nothrow<IMMDevice> device;
    RETURN_IF_FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device));
    RETURN_IF_FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_audioClient));

    RETURN_IF_FAILED(m_sampleReadyEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        200000,  // 缓冲区 200ms
        0,
        &m_format,
        nullptr));
    RETURN_IF_FAILED(m_audioClient->SetEventHandle(m_sampleReadyEvent.get()));
    RETURN_IF_FAILED(m_audioClient->GetService(IID_PPV_ARGS(&m_captureClient)));
    return S_OK;
}

HRESULT MicCapture::Start()
{
    RETURN_HR_IF(E_NOT_VALID_STATE, !m_audioClient);
    RETURN_IF_FAILED(m_audioClient->Start());
    m_running = true;
    m_thread = std::thread(&MicCapture::CaptureThreadProc, this);
    return S_OK;
}

void MicCapture::Stop()
{
    m_running = false;
    if (m_sampleReadyEvent)
    {
        m_sampleReadyEvent.SetEvent();  // 唤醒采集线程以便退出
    }
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    if (m_audioClient)
    {
        m_audioClient->Stop();
    }
}

void MicCapture::CaptureThreadProc()
{
    while (m_running)
    {
        // 等新数据（200ms 超时兜底，防事件丢失卡死）
        WaitForSingleObject(m_sampleReadyEvent.get(), 200);
        if (!m_running)
        {
            break;
        }

        UINT32 framesAvailable = 0;
        while (SUCCEEDED(m_captureClient->GetNextPacketSize(&framesAvailable)) && framesAvailable > 0)
        {
            BYTE* data = nullptr;
            DWORD flags = 0;
            if (FAILED(m_captureClient->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr)))
            {
                break;
            }
            DWORD bytes = framesAvailable * m_format.nBlockAlign;
            if (m_callback && bytes > 0)
            {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    // 静音标志：数据无效，送零
                    std::vector<BYTE> silence(bytes, 0);
                    m_callback(silence.data(), bytes);
                }
                else
                {
                    m_callback(data, bytes);
                }
            }
            m_captureClient->ReleaseBuffer(framesAvailable);
        }
    }
}
```

- [ ] **Step 4.3: 修改 `source/ProcessAudioRecorder.cpp`**

include 区加 `#include "MicCapture.h"`；在 `--list` 分支之后加 `--mic-test` 分支：

```cpp
	if (argc >= 2 && wcscmp(argv[1], L"--mic-test") == 0) {
		std::wcout << L"Mic test: recording 5 seconds from default microphone..." << std::endl;
		WavSink sink;
		MicCapture mic;
		HRESULT hr = mic.Initialize([&sink](const BYTE* data, DWORD size) {
			sink.WriteChunk(data, size);  // 单生产者，测试场景直接写
			});
		if (SUCCEEDED(hr)) {
			hr = sink.Initialize(L"mic_test.wav", mic.Format());
		}
		if (SUCCEEDED(hr)) {
			hr = mic.Start();
		}
		if (FAILED(hr)) {
			std::wcout << L"Mic test FAILED. 0x" << std::hex << hr << L"\n"
				<< L"Check: is a microphone connected?\n"
				<< L"Check: Windows Settings > Privacy > Microphone > allow desktop apps." << std::endl;
			return 2;
		}
		std::wcout << L"Speak now..." << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(5));
		mic.Stop();
		sink.Finalize();
		std::wcout << L"Saved to mic_test.wav (current directory). Play it to verify your voice." << std::endl;
		return 0;
	}
```

- [ ] **Step 4.4: 修改 `CMakeLists.txt`**

```cmake
add_executable(ProcessAudioRecorder
    source/ProcessAudioRecorder.cpp
    source/LoopbackCapture.cpp
    source/AudioSessionLister.cpp
    source/WavSink.cpp
    source/M4aSink.cpp
    source/MicCapture.cpp
)
```

- [ ] **Step 4.5: 编译**

Run: `cmake --build build`
Expected: 零 error。

- [ ] **Step 4.6: 👤 用户验证**

1. 运行 `build\ProcessAudioRecorder.exe --mic-test`；
2. 看到 "Speak now..." 后对着麦克风说 5 秒话；
3. 播放生成的 `mic_test.wav`：**能清楚听到自己刚才说的话**；
4. 若失败提示隐私设置：Windows 设置 → 隐私 → 麦克风 → 允许桌面应用访问，改完重试。

- [ ] **Step 4.7: Commit**

```bash
git add source/MicCapture.h source/MicCapture.cpp source/ProcessAudioRecorder.cpp CMakeLists.txt
git commit -m "Add MicCapture module with --mic-test self check"
```

---

### Task 5: AudioMixer —— 麦克风混音集成（`--mic on`）

混音策略（规格 5.2）：**环回块到达驱动混音**，不开独立混音线程。麦克风数据进字节缓冲；每当环回块到达，取等量麦克风数据饱和叠加后送回写入队列。水位控制吸收时钟漂移。

**Files:**
- Create: `source/AudioMixer.h`
- Create: `source/AudioMixer.cpp`
- Modify: `source/LoopbackCapture.h`（加数据分流 tap + 公开入队方法）
- Modify: `source/LoopbackCapture.cpp`（OnAudioSampleRequested 走 tap）
- Modify: `source/ProcessAudioRecorder.cpp`（`--mic` 参数 + 组装）
- Modify: `CMakeLists.txt`

- [ ] **Step 5.1: 写 `source/AudioMixer.h`**

```cpp
/*
 * 双流混音器：以环回流为主时钟，麦克风数据入缓冲随取随混。
 * 同步策略：麦克风缓冲不足补静音、超水位丢最旧（约 2 秒上限），
 * 时钟漂移被水位吸收，瞬时不同步 < 20ms，通话场景无感。
 */
#pragma once

#include <Windows.h>
#include <deque>
#include <mutex>
#include <vector>

class AudioMixer
{
public:
    // 麦克风数据入缓冲（麦克风采集线程调用）
    void PushMicData(const BYTE* data, DWORD size);

    // 用环回块驱动混音：取等量麦克风数据饱和叠加，返回混合块（环回回调线程调用）
    std::vector<BYTE> MixWithLoopback(std::vector<BYTE>&& loopbackChunk);

private:
    std::deque<BYTE> m_micBuffer;  // 麦克风字节缓冲（16bit 立体声样本流）
    std::mutex m_mutex;            // 缓冲锁（两线程并发访问）

    // 水位上限：约 2 秒（44100 帧/秒 x 4 字节/帧 x 2 秒）
    static constexpr size_t kMaxBuffer = 44100 * 4 * 2;
};
```

- [ ] **Step 5.2: 写 `source/AudioMixer.cpp`**

```cpp
#include "AudioMixer.h"

#include <algorithm>

void AudioMixer::PushMicData(const BYTE* data, DWORD size)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_micBuffer.insert(m_micBuffer.end(), data, data + size);
    // 水位控制：超限丢最旧数据，防止延迟累积
    if (m_micBuffer.size() > kMaxBuffer)
    {
        m_micBuffer.erase(m_micBuffer.begin(), m_micBuffer.begin() + (m_micBuffer.size() - kMaxBuffer));
    }
}

std::vector<BYTE> AudioMixer::MixWithLoopback(std::vector<BYTE>&& loopbackChunk)
{
    // 取等量麦克风数据（不足部分保持为零 = 静音）
    std::vector<BYTE> micChunk(loopbackChunk.size(), 0);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t take = (std::min)(m_micBuffer.size(), micChunk.size());
        take -= take % 4;  // 对齐到立体声帧边界（2 声道 x 2 字节），防左右声道错位
        std::copy(m_micBuffer.begin(), m_micBuffer.begin() + take, micChunk.begin());
        m_micBuffer.erase(m_micBuffer.begin(), m_micBuffer.begin() + take);
    }

    // 逐样本饱和叠加（16bit 有符号，钳制防爆音）
    auto* dst = reinterpret_cast<INT16*>(loopbackChunk.data());
    auto* mic = reinterpret_cast<const INT16*>(micChunk.data());
    size_t samples = loopbackChunk.size() / sizeof(INT16);
    for (size_t i = 0; i < samples; i++)
    {
        int mixed = static_cast<int>(dst[i]) + static_cast<int>(mic[i]);
        mixed = (std::max)(-32768, (std::min)(32767, mixed));
        dst[i] = static_cast<INT16>(mixed);
    }
    return std::move(loopbackChunk);
}
```

- [ ] **Step 5.3: 修改 `source/LoopbackCapture.h`**

(a) include 区加 `#include <functional>`；
(b) public 区加两个方法声明：

```cpp
 // 数据分流回调：设置后捕获数据交给 tap 处理（混音），不再直接入写队列
 void SetDataTap(std::function<void(std::vector<BYTE>&&)> tap) { m_dataTap = std::move(tap); }

 // 把（混音后的）数据块送入写入队列（tap 处理完后回送用）
 void EnqueueAudioData(std::vector<BYTE>&& chunk);
```

(c) private 成员区加：

```cpp
 std::function<void(std::vector<BYTE>&&)> m_dataTap;     // 数据分流回调（未设置则直接入队）
```

- [ ] **Step 5.4: 修改 `source/LoopbackCapture.cpp`**

(a) 加 `EnqueueAudioData` 定义（放在 `OnAudioSampleRequested` 之前）：

```cpp
// 数据块入写入队列并唤醒写入线程
void CLoopbackCapture::EnqueueAudioData(std::vector<BYTE>&& chunk)
{
	{
		std::lock_guard<std::mutex> queueLock(m_QueueMutex);
		m_AudioQueue.push(std::move(chunk));
	}
	m_QueueCV.notify_one();
}
```

(b) `OnAudioSampleRequested` 中 try 块内原"复制到向量→入队→notify"段替换为：

```cpp
			// 复制音频数据到向量
			std::vector<BYTE> audioChunk(Data, Data + cbBytesToCapture);

			// 设置了分流回调则交给它（混音路径），否则直接入写队列
			if (m_dataTap)
			{
				m_dataTap(std::move(audioChunk));
			}
			else
			{
				EnqueueAudioData(std::move(audioChunk));
			}
```

- [ ] **Step 5.5: 修改 `source/ProcessAudioRecorder.cpp`**

(a) include 区加 `#include "AudioMixer.h"`；
(b) `CommandLineArgs` 加 `bool micEnabled = false;`，`ParseCommandLine` 中加解析（放 format 解析之后）：

```cpp
	if (params.find(L"mic") != params.end()) {
		if (params[L"mic"] == L"on") {
			args.micEnabled = true;
		}
		else if (params[L"mic"] != L"off") {
			args.errorMessage = L"Error: Invalid --mic value: " + params[L"mic"] + L"\nMust be on or off.";
			return args;
		}
	}
```

(c) `usage()` Options 加一行：

```cpp
			<< L"  --mic <on|off> Mix your microphone into the recording (default off)\n"
```

(d) `wmain` 组装：`loopbackCapture.SetAudioSink(sink.get());` 之后加（**声明顺序即析构安全顺序，勿调换**）：

```cpp
	AudioMixer mixer;
	MicCapture mic;
	if (args.micEnabled) {
		// 环回块 → 混音 → 回送写入队列
		loopbackCapture.SetDataTap([&mixer, &loopbackCapture](std::vector<BYTE>&& chunk) {
			loopbackCapture.EnqueueAudioData(mixer.MixWithLoopback(std::move(chunk)));
			});
		HRESULT hrMic = mic.Initialize([&mixer](const BYTE* data, DWORD size) {
			mixer.PushMicData(data, size);
			});
		if (FAILED(hrMic)) {
			std::wcout << L"Error: microphone init failed. 0x" << std::hex << hrMic << L"\n"
				<< L"Check: microphone connected? Privacy settings allow desktop apps?\n"
				<< L"Or run without --mic on to record system audio only." << std::endl;
			return 2;
		}
		std::wcout << L"Microphone mixing: ON" << std::endl;
	}
```

(e) 现有 `hr = loopbackCapture.StartCaptureAsync(...)` / `StartGlobalCaptureAsync(...)` 成功判定之后（`FAILED(hr)` 检查块的后面）加启动麦克风：

```cpp
	if (args.micEnabled) {
		HRESULT hrMicStart = mic.Start();
		if (FAILED(hrMicStart)) {
			std::wcout << L"Error: microphone start failed. 0x" << std::hex << hrMicStart << std::endl;
			loopbackCapture.StopCaptureAsync();
			return 2;
		}
	}
```

(f) 停止段：`HRESULT hrStop = loopbackCapture.StopCaptureAsync();` **之前**加（先停麦克风再停环回，保证排空期间无新混音输入）：

```cpp
	if (args.micEnabled) {
		mic.Stop();
	}
```

- [ ] **Step 5.6: 修改 `CMakeLists.txt`**

```cmake
add_executable(ProcessAudioRecorder
    source/ProcessAudioRecorder.cpp
    source/LoopbackCapture.cpp
    source/AudioSessionLister.cpp
    source/WavSink.cpp
    source/M4aSink.cpp
    source/MicCapture.cpp
    source/AudioMixer.cpp
)
```

- [ ] **Step 5.7: 编译**

Run: `cmake --build build`
Expected: 零 error。

- [ ] **Step 5.8: 👤 用户验证（本任务是"微信通话双方声音"的核心验证）**

1. 放歌，`--list` 拿酷狗 PID；
2. `build\ProcessAudioRecorder.exe --pid <PID> --mode 1 --mic on --path D:\mix_test.m4a`；
3. 录 15 秒，期间**边放歌边对麦克风说话**，Ctrl+C；
4. 播放 `mix_test.m4a`：**同时听到歌声和自己的说话声**，两者不互相盖没；
5. 不带 `--mic on` 再录一次：只有歌声（回归验证）。

- [ ] **Step 5.9: Commit**

```bash
git add source/AudioMixer.h source/AudioMixer.cpp source/LoopbackCapture.h source/LoopbackCapture.cpp source/ProcessAudioRecorder.cpp CMakeLists.txt
git commit -m "Add AudioMixer with --mic option for microphone mixing"
```

---

### Task 6: LevelMeter —— 实时电平显示（防"空录"反馈）

录音时控制台显示双电平字符条（系统声/麦克风）。选错进程时电平条静止，用户当场可见——对应验收标准 4，也是 GUI 阶段音量条的数据源。

**Files:**
- Create: `source/LevelMeter.h`（纯头文件）
- Modify: `source/ProcessAudioRecorder.cpp`（tap 统一化 + 进度行加电平条）

- [ ] **Step 6.1: 写 `source/LevelMeter.h`**

```cpp
/*
 * 电平计算：对 16bit PCM 块取峰值，归一化为 0-100。
 * 音频线程写、显示线程读，用原子变量传递。
 */
#pragma once

#include <Windows.h>
#include <atomic>

// 双通道电平状态
struct LevelState
{
    std::atomic<int> systemLevel{ 0 };  // 系统/目标软件声音电平（0-100）
    std::atomic<int> micLevel{ 0 };     // 麦克风电平（0-100）
};

// 计算一块 16bit PCM 的峰值电平，返回 0-100
inline int CalcPeakLevel(const BYTE* data, DWORD size)
{
    const INT16* samples = reinterpret_cast<const INT16*>(data);
    size_t count = size / sizeof(INT16);
    int peak = 0;
    for (size_t i = 0; i < count; i++)
    {
        int v = samples[i];
        if (v < 0)
        {
            v = -v;
        }
        if (v > peak)
        {
            peak = v;
        }
    }
    return peak * 100 / 32768;
}
```

- [ ] **Step 6.2: 修改 `source/ProcessAudioRecorder.cpp` —— tap 统一化**

(a) include 区加 `#include "LevelMeter.h"`；

(b) **替换 Task 5 步骤 5.5(d) 写入的整个组装块**（自 `AudioMixer mixer;` 起到 `std::wcout << L"Microphone mixing: ON"...` 块尾）为下面的统一版——tap 无论麦克风开关都设置，先算电平再分流（`LevelState levelState;` 声明包含在块首）：

```cpp
	LevelState levelState;
	AudioMixer mixer;
	MicCapture mic;
	// 统一分流：先算系统声电平，再按麦克风开关决定混音或直通
	loopbackCapture.SetDataTap([&](std::vector<BYTE>&& chunk) {
		levelState.systemLevel = CalcPeakLevel(chunk.data(), static_cast<DWORD>(chunk.size()));
		if (args.micEnabled) {
			loopbackCapture.EnqueueAudioData(mixer.MixWithLoopback(std::move(chunk)));
		}
		else {
			loopbackCapture.EnqueueAudioData(std::move(chunk));
		}
		});
	if (args.micEnabled) {
		HRESULT hrMic = mic.Initialize([&mixer, &levelState](const BYTE* data, DWORD size) {
			levelState.micLevel = CalcPeakLevel(data, size);
			mixer.PushMicData(data, size);
			});
		if (FAILED(hrMic)) {
			std::wcout << L"Error: microphone init failed. 0x" << std::hex << hrMic << L"\n"
				<< L"Check: microphone connected? Privacy settings allow desktop apps?\n"
				<< L"Or run without --mic on to record system audio only." << std::endl;
			return 2;
		}
		std::wcout << L"Microphone mixing: ON" << std::endl;
	}
```

（Task 5 里 mic.Start / mic.Stop 的启动停止代码保持不变。）

- [ ] **Step 6.3: 修改 `source/ProcessAudioRecorder.cpp` —— 进度行加电平条**

(a) `DisplayProgress` 整体替换为：

```cpp
// 画 0-100 电平为 10 格字符条，例：[###-------]
static std::wstring DrawLevelBar(int level)
{
	std::wstring bar;
	int filled = (level + 9) / 10;
	for (int i = 0; i < 10; i++) {
		bar += (i < filled) ? L'#' : L'-';
	}
	return bar;
}

void DisplayProgress(const std::chrono::seconds& duration, int sysLevel, int micLevel, bool micEnabled)
{
	auto totalSeconds = duration.count();
	auto hours = totalSeconds / 3600;
	auto minutes = (totalSeconds % 3600) / 60;
	auto seconds = totalSeconds % 60;

	std::wcout << L"\r● Recording [";
	std::wcout << std::setw(2) << std::setfill(L'0') << hours << L":"
		<< std::setw(2) << std::setfill(L'0') << minutes << L":"
		<< std::setw(2) << std::setfill(L'0') << seconds << L"]";
	std::wcout << L" SYS[" << DrawLevelBar(sysLevel) << L"]";
	if (micEnabled) {
		std::wcout << L" MIC[" << DrawLevelBar(micLevel) << L"]";
	}
	std::wcout << L" Ctrl+C to stop  " << std::flush;
}
```

(b) 主循环里调用处改为：

```cpp
			DisplayProgress(duration, levelState.systemLevel, levelState.micLevel, args.micEnabled);
```

(c) 主循环刷新间隔从 500ms 改为 200ms（电平跳动更灵敏）：`std::this_thread::sleep_for(std::chrono::milliseconds(200));`

- [ ] **Step 6.4: 编译**

Run: `cmake --build build`
Expected: 零 error。

- [ ] **Step 6.5: 👤 用户验证**

1. 放歌录酷狗（`--mic on`）：`SYS[######----]` 随歌声起伏跳动；对麦克风说话时 `MIC[####------]` 跟着跳；
2. **防空录验证**：找一个安静的进程 PID（--list 里 silent 的）录音 → SYS 条保持 `[----------]` 静止——当场看出"没录到声音"；
3. 不带 `--mic on`：只显示 SYS 条。

- [ ] **Step 6.6: Commit**

```bash
git add source/LevelMeter.h source/ProcessAudioRecorder.cpp
git commit -m "Add real-time level meters to recording progress display"
```

---

### Task 7: 崩溃保护 —— fMP4 技术验证与定案（规格 5.4）

**验证型任务**：确认 fragmented MP4 能否让"强杀进程后的 .m4a 仍可播放"，按实测结果定案并记录。

**Files:**
- Modify: `source/M4aSink.cpp`（SinkWriter 加 fMP4 容器属性）
- Modify: `PROJECT_WIKI.md`（记录实测结论）

- [ ] **Step 7.1: 修改 `source/M4aSink.cpp`**

`Initialize` 中 `MFCreateSinkWriterFromURL` 一行替换为带属性版本：

```cpp
    // fMP4 容器：分段写入，进程意外终止时文件仍可播放到中断点（崩溃保护）
    wil::com_ptr_nothrow<IMFAttributes> attrs;
    RETURN_IF_FAILED(MFCreateAttributes(&attrs, 1));
    RETURN_IF_FAILED(attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_FMPEG4));
    RETURN_IF_FAILED(MFCreateSinkWriterFromURL(filePath, nullptr, attrs.get(), &m_writer));
```

（`MF_TRANSCODE_CONTAINERTYPE` 声明于 `<mfidl.h>`，M4aSink.h 已包含。）

- [ ] **Step 7.2: 编译**

Run: `cmake --build build`
Expected: 零 error。

- [ ] **Step 7.3: 👤 用户验证 A —— 正常路径兼容性**

正常录一段 30 秒 m4a（正常 Ctrl+C 停止）→ 用日常播放器双击播放 + 拖进微信发给"文件传输助手"试听。预期：正常播放。若播放器不认 fMP4 → 记录后进 Step 7.5 回退。

- [ ] **Step 7.4: 👤 用户验证 B —— 强杀存活性**

1. 开始录音（放着歌），录满 30 秒**不要停止**；
2. 打开任务管理器 → 找到 ProcessAudioRecorder → 结束任务（模拟崩溃）；
3. 双击刚才的 .m4a：预期**能播放到中断点附近**（允许末尾缺几秒）。

- [ ] **Step 7.5: 按结果定案**

- A、B 都通过 → fMP4 默认开启，完工；
- 任一不通过 → 还原 Step 7.1 改动（去掉 attrs，恢复三参 nullptr 调用），崩溃保护记为"未实现"，把实测现象写入 PROJECT_WIKI.md 风险表，与用户讨论是否值得用"WAV 临时文件 + 停止时转码"方案（磁盘开销大，须用户拍板，本计划不擅自实施）。

- [ ] **Step 7.6: 更新 `PROJECT_WIKI.md` 并 Commit**

Wiki 三处更新：目录结构加新模块文件；已知问题表 K2-K5/K7 标记"已修复（2026-07）"、K3 附实测结论；路线图 P1/P2/P3 状态改"已完成（CLI）"，附 fMP4 定案结论。

```bash
git add source/M4aSink.cpp PROJECT_WIKI.md
git commit -m "Enable fMP4 container for crash-resilient m4a output"
git push origin main
```

（若 Step 7.5 走了回退分支，提交信息改为 `Document fMP4 test result and revert to standard MP4 container`。）

---

## 阶段一完成检查（全部勾选才算收工）

- [ ] 验收 1（进程隔离）：微信/酷狗通话场景实录，文件只含目标声音 —— 对应规格验收标准 1
- [ ] 验收 2（混音完整）：`--mic on` 文件同时含对方与自己声音 —— 标准 2
- [ ] 验收 3（自动收尾）：录音中退出目标进程，文件自动完整保存 —— 标准 3
- [ ] 验收 4（防空录）：安静进程电平条静止可辨 —— 标准 4
- [ ] 验收 5（体积)：M4A 30 分钟 ≈ 30MB（可用 3 分钟 ≈ 3MB 折算验证） —— 标准 5
- [ ] PROJECT_WIKI.md 已同步（模块/K 问题/路线图/fMP4 结论）
- [ ] 全部提交已 push 到 origin/main

**收工后向用户宣布：阶段一引擎完成，等用户口头验收通过，再按规格第 6 节启动"阶段二 GUI 包装"的实施计划编写。**

