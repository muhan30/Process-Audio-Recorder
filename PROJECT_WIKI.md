# PROJECT_WIKI — ProcessAudioRecorder

> 本文档是项目的"单一事实来源"，供每次开发前 AI/人快速掌握项目状态。
> 任何功能开发、架构变更后必须同步更新本文档。
> 最后更新：2026-07-17

---

## 1. 项目定位与目标

**现状**：一个 Windows C++ 命令行工具，利用 Win10 2004+ 的 WASAPI 进程级音频环回捕获 API，实现无虚拟声卡的进程隔离录音。fork 自 [CingZeoi/AudioLoopbackRecorder](https://github.com/CingZeoi/AudioLoopbackRecorder)（MIT 协议，原作者张赐荣）。

**愿景**：发展为一款进程隔离录音 APP。

**核心目标场景**：微信通话时录下**双方完整通话**，同时排除浏览器视频、音乐软件等一切无关声音。

已确认的需求决策：
- 通话录音需要**自己的声音**（麦克风）——loopback 只含系统播放的声音（对方），因此必须开发"麦克风同步捕获 + 混音"能力。
- 多进程应用（微信/酷狗/Chrome）难以人工定位真正发声的 PID——需要 `--list` 功能自动枚举正在发声的进程。

## 2. 仓库信息

| Remote | 地址 | 用途 |
|--------|------|------|
| `origin` | https://github.com/muhan30/My_AudioLoopbackRecorder.git | 自己的仓库，日常 push |
| `upstream` | https://github.com/CingZeoi/AudioLoopbackRecorder.git | 原作者仓库，只读，`git fetch upstream` 同步更新 |

分支：`main`（单分支开发）。

## 3. 构建方法

**主用：CMake（本机已验证）**
```bash
cmake -B build          # 首次配置（本机使用 Ninja 生成器）
cmake --build build     # 产物：build/ProcessAudioRecorder.exe
```
- 依赖全部自包含：WIL 头文件已 vendor 在 `include/wil/`（.gitattributes 标记为 vendored），无需 NuGet/网络。
- CMake 方案不编译 `.rc` 版本资源（当初因报错移除，见"已知问题 K1"）。

**备用：Visual Studio 2022（原作者方案）**
- 打开 `source/ProcessAudioRecorder.sln`，还原 NuGet 包（WIL 1.0.250325.1），Release x64 生成。

**运行环境要求**：Windows 10 2004 (build 19041) 及以上（进程环回 API 的最低版本）。

## 4. 目录结构

```
AudioLoopbackRecorder/
├── CMakeLists.txt              # CMake 构建脚本（C++17，UNICODE，链接 mfplat/mmdevapi 等）
├── include/wil/                # vendor 的微软 WIL 库（第三方，勿改）
├── references/                 # 微软 WASAPI 官方文档副本（capturing-a-stream / loopback-recording）
├── build/                      # 构建产物（git 已忽略）
└── source/
    ├── ProcessAudioRecorder.cpp  # 入口：命令行解析、进程存活监控、Ctrl+C、进度显示
    ├── LoopbackCapture.h/.cpp    # 核心类 CLoopbackCapture：三种模式的捕获引擎
    ├── Common.h                  # METHODASYNCCALLBACK 宏（COM 回调样板消除）
    ├── resource.h / ProcessAudioRecorder.rc  # 版本资源（仅 VS 构建使用，.rc 为 UTF-16 编码）
    └── ProcessAudioRecorder.sln/.vcxproj/packages.config  # VS2022 构建体系
```

所有源文件统一为 UTF-8 编码（`ProcessAudioRecorder.cpp` 带 BOM，其余无 BOM）。

## 5. 命令行接口

```
ProcessAudioRecorder [--pid <PID>] --mode <MODE> --path <FILEPATH>
```
| 参数 | 说明 |
|------|------|
| `--mode 0` | 全局环回（系统混音），无需 --pid |
| `--mode 1` | 进程包含：只录 PID 及其**子进程树** |
| `--mode 2` | 进程排除：录除 PID 及其子进程树外的所有声音 |
| `--path` | 输出 WAV 路径（必填） |

输出格式固定：PCM 44.1kHz / 16bit / 立体声。
停止条件：Ctrl+C，或目标进程退出（模式 1/2，主循环每 500ms 轮询）。

## 6. 架构与数据流

### 6.1 核心类 CLoopbackCapture

WRL `RuntimeClass`，实现 `IActivateAudioInterfaceCompletionHandler`；通过 `METHODASYNCCALLBACK` 宏内嵌三个 `IMFAsyncCallback` 回调（StartCapture / StopCapture / SampleReady）。

### 6.2 两条激活路径

- **进程模式**（`ActivateAudioInterface`）：`ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, ...)` + `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` 参数，系统在内核层构建只关联目标进程树音频流的虚拟捕获端点。include/exclude 由 `PROCESS_LOOPBACK_MODE_*_TARGET_PROCESS_TREE` 区分。
- **全局模式**（`ActivateAudioInterfaceGlobal`）：传统路径，`IMMDeviceEnumerator` → 默认渲染设备 → `IMMDevice::Activate` + `AUDCLNT_STREAMFLAGS_LOOPBACK`。

### 6.3 事件驱动捕获闭环（生产者）

`SetEventHandle(m_SampleReadyEvent)` → 音频引擎每批数据就绪时触发事件 → `MFPutWaitingWorkItem` 调度 MF 线程池执行 `OnSampleReady` → `OnAudioSampleRequested` 内循环 `GetNextPacketSize`/`GetBuffer` 抓数据、拷入 `std::vector<BYTE>`、push 进 `m_AudioQueue`（`m_QueueMutex` 保护）→ 回调末尾**重新提交** `MFPutWaitingWorkItem` 续订监听。无数据时零 CPU 占用。

### 6.4 写入线程（消费者）

`OnStartCapture` 启动 `WriterThreadProc`（std::thread）：`condition_variable::wait`（带谓词防虚假唤醒）→ 队列取数据 → `WriteFile` 落盘 → 累计 `m_cbDataSize`。捕获与 I/O 完全解耦，磁盘慢不会导致音频丢帧。

### 6.5 生命周期与 WAV 头修复

启动时 `CreateWAVFile` 写入 RIFF/fmt/data 头（大小字段留 0）；`StopCaptureAsync` → `OnStopCapture`（停 AudioClient、取消工作项、唤醒写线程）→ 等待 `m_hCaptureStopped` → join 写线程 → 写线程收尾时 `FixWAVHeader` 回填 data 大小与 RIFF 总大小。

### 6.6 设备状态机

`Uninitialized → Initialized → Starting → Capturing → Stopping → Stopped`，任何失败进入 `Error`（`SetDeviceStateErrorIfFailed` 统一处理）。

## 7. 已知问题与优化候选（待 REVIEW 阶段逐项决策）

| # | 位置 | 问题 | 严重度 |
|---|------|------|--------|
| K1 | CMakeLists.txt | 未编译 .rc，CMake 产物无版本信息（且 MSVC 下未加 /utf-8 编译选项，无 BOM 的 UTF-8 源文件注释可能触发 C4819 警告） | 低 |
| K2 | ProcessAudioRecorder.cpp:228-230 | `GetStopEventHandle` 取出句柄后 `if (hStopEvent != NULL) {}` 空体，死代码 | 低 |
| K3 | LoopbackCapture.cpp:159-164 | 进程模式 `Initialize` 第 4 参（periodicity）误传 `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM` 标志；全局模式则正确地把它并入第 2 参 flags。进程环回虚拟设备会忽略 periodicity 所以未出错，但两处写法应统一 | 中 |
| K4 | LoopbackCapture.cpp:506 | `WriterThreadProc` 循环条件读 `m_AudioQueue.empty()` 未持锁，数据竞争（实践中影响小，但属 UB） | 中 |
| K5 | LoopbackCapture.cpp:369-375 | `StopCaptureAsync` 先因状态非 Capturing/Error 返回 `E_NOT_VALID_STATE`，其后的 Stopping/Stopped 判断为不可达死代码 | 低 |
| K6 | LoopbackCapture.h | `m_cbDataSize` 为 DWORD，录音超 4GB（约 6.7 小时）溢出；WAV 格式本身也有 4GB 上限 | 中 |
| K7 | ProcessAudioRecorder.cpp:226 | `StopCaptureAsync()` 返回值未检查，写盘失败（如磁盘满）用户无感知 | 中 |
| K8 | 体验 | 录不到声（选错 PID）时无任何反馈，产出无声文件——需实时电平显示 | 高（与 --list 同源痛点） |

## 8. 路线图

| 优先级 | 功能 | 状态 | 说明 |
|--------|------|------|------|
| P1 | `--list`：枚举正在发声的进程（PID/进程名/会话状态） | 未开始 | 用 `IAudioSessionManager2` → `IAudioSessionEnumerator` → `IAudioSessionControl2::GetProcessId()`；解决多进程应用找不到发声 PID 的核心痛点 |
| P2 | 麦克风 + loopback 双流同步捕获与混音 | 未开始 | 微信通话"双方声音"必需；涉及双流时钟同步、重采样、混音策略，需单独 SPEC |
| P3 | 录制中实时电平显示 | 未开始 | 解决"录了个寂寞"无反馈问题（K8） |
| 远期 | MP3/Opus 编码输出、GUI、DLL 化引擎 | 未开始 | 原作者 README 中的方向，暂不排期 |

**流程约定**：每个功能严格走 SPEC →（人审）→ PLAN →（人审）→ CODE → REVIEW →（人审）→ VERIFY；SPEC/PLAN 文档存放于 `docs/` 目录（首个功能开发时创建），作为项目资产保留。

## 9. 关键技术备忘

- **进程树语义**：`PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE` 包含目标进程**及其后代**。多进程应用选进程树根通常可行；但若发声进程与所选 PID 不在同一棵树（独立服务拉起/父进程已退出致树断裂）则录到静音。最可靠的目标 PID 是音频会话上报的"直接发声进程"。
- **loopback ≠ 录音全部**：环回只捕获渲染（输出）流，麦克风输入完全不在其中。
- **无声排查**（在 --list 完成前的手动方法）：`Get-CimInstance Win32_Process -Filter "Name LIKE 'XXX%'" | Select ProcessId,ParentProcessId,Name` 找树根 PID 逐个试。
- **音频会话枚举**：系统"音量合成器"的底层 API 即 `IAudioSessionManager2`，活跃会话（AudioSessionStateActive）的 PID 就是正在发声的进程。
