# SPEC: 会话事件监控 + 进程树去重 + 静音自动暂停

> 版本: 1.0 | 日期: 2026-07-20 | 状态: 自审中
> 参考: `references/win-capture-audio-main/`

---

## 一、需求

### 需求 1：会话事件监控（替代轮询）

**目标**：用 `IAudioSessionNotification` 事件驱动代替 1 秒定时器轮询刷新会话列表。

**参考实现**：`win-capture-audio/src/session-monitor.cpp`
- `IMMDeviceEnumerator::RegisterEndpointNotificationCallback` → 监听设备增删
- `IAudioSessionManager2::RegisterSessionNotification(IAudioSessionNotification*)` → 监听会话增删
- `OnSessionCreated` → 立即刷新列表
- `OnSessionDisconnected` → 立即刷新列表

**验收标准**：
- 新进程开始发声 → 列表立即更新（不用等 1 秒）
- 进程停止发声 → 列表立即更新
- 1 秒定时器保留作为兜底（音频设备热插拔等边缘情况）
- 不影响现有录音功能

### 需求 2：进程树去重

**目标**：`includeTree=true` 时避免重复捕获父进程和子进程的音频。

**参考实现**：`win-capture-audio/src/audio-capture.cpp:63-100`
- 获取所有目标 PID 的父进程关系（`CreateToolhelp32Snapshot`）
- 如果父进程也在捕获列表中 → 子进程音频已被父进程的 `includeTree` 覆盖 → 只录父进程
- 只对"根"进程（其父进程不在捕获列表中的进程）创建捕获

**验收标准**：
- `includeTree=true` 时，父子进程同在列表 → 只录父进程
- `includeTree=false` 时，行为不变（只录指定 PID）
- 不影响现有单进程录音场景

### 需求 3：静音检测自动暂停

**目标**：录音中双方都长时间静音时自动停止录音，省存储空间。出声后自动恢复（需重新触发自动录音）。

**原理**：系统声音和麦克风电平同时持续为 0（或极低）超过 N 秒 → 判定为静音 → 停止录音。

**验收标准**：
- 录音中系统声音和麦克风电平同时为 0 持续超过 60 秒 → 自动停止
- 静音超时可在设置中调整（0 = 关闭自动暂停，范围 30-300 秒）
- 仅对自动录音生效（微信通话自动录音），手动录音不受影响
- 停止后不自动重开——需等下次通话触发

---

## 二、涉及模块

| 模块 | 改动 | 说明 |
|------|------|------|
| `source/AudioSessionLister.cpp` | 改 | 新增 `RegisterSessionNotification` 事件监听 |
| `source/AudioSessionLister.h` | 改 | 新增 `SessionNotificationClient` COM 类 |
| `source/CaptureEngine.cpp` | 改 | Start 前调用去重 |
| `source/GUI/MainWindow.h` | 改 | 新增静音检测成员 |
| `source/GUI/MainWindow.cpp` | 改 | 静音检测逻辑 + 设置项 |

## 三、实现概要

### 3.1 会话事件监控

- 新增 `SessionNotificationClient` 类实现 `IAudioSessionNotification`
- 在 `AudioSessionLister` 初始化时注册到默认渲染设备的 `IAudioSessionManager2`
- `OnSessionCreated`: 通知 GUI 刷新列表（通过回调/消息）
- 给 `SessionList` 添加 `RefreshAsync()` 方法，收到事件后延迟 ~100ms 刷新（防抖动）

### 3.2 进程树去重

- 在 `CaptureEngine::Start()` 中，如果 `includeTree=true`：
  1. 获取目标 PID 的所有祖先 PID
  2. 检查会话列表中是否有目标 PID 的父进程也在发声
  3. 如果有 → 使用父 PID 代替目标 PID（或直接跳过子 PID）
- 核心函数：`GetProcessParent()` → `CreateToolhelp32Snapshot` → 找父 PID

### 3.3 静音检测

- 在 `CheckAutoRecord` 中增加静音计数器
- `UpdateStatus` 提供 `systemLevel` 和 `micLevel`
- `systemLevel==0 && micLevel==0` 持续 N 秒 → 触发 `OnStop()`
- 仅当 `m_autoRecording==true` 时检测
