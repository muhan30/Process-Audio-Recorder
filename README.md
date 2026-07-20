# Process Audio Recorder / 进程隔离录音机

[English](#english) | [中文](#中文)

---

## English

A Windows desktop recording tool based on [CingZeoi/AudioLoopbackRecorder](https://github.com/CingZeoi/AudioLoopbackRecorder) (original author: 张赐荣/Cirong Zhang, MIT License).

**Core capability**: record audio from a specific application (e.g., a WeChat call), while completely excluding sounds from all other apps. No virtual audio cable needed — built entirely on native Windows APIs.

### Why

- Recording a WeChat call? Browser videos, music players, and notification sounds are all excluded. Only the call audio enters the recording.
- Recording an online class or meeting? Background noise from other apps stays out.
- More precise than system "Stereo Mix" — like taking a screenshot of a single window instead of the entire desktop.

### Download

Download the latest version from **[GitHub Releases](https://github.com/muhan30/Process-Audio-Recorder/releases)** (732 KB, single file, no installation required).

### Quick Start

1. Double-click `ProcessAudioRecorderGUI.exe`.
2. **WeChat auto-record**: Check "微信通话自动录音" and make/receive a call — recording starts and stops automatically.
3. **Manual mode**: Start audio in your target app, click **刷新** (Refresh), select the app, and click **开始录音** (Start Recording).
4. Click **停止并保存** (Stop & Save) or hang up the call. Files are saved in `recordings\<app name>\`.

> Pressing ✕ minimizes to the system tray — recording continues uninterrupted.
> The recording history list at the bottom shows all past recordings — double-click to play.

### Settings

- **系统声音增益** (System audio gain): target app audio too quiet? Turn it up (default 2.0×, range 0.1–8.0).
- **麦克风增益** (Mic gain): your voice too quiet? Try 4.0× (range 0.1–8.0).
- **输出格式** (Output format): M4A compressed (recommended, saves space) / WAV lossless.
- **低通滤波** (Low-pass filter): removes high-frequency hiss from the microphone (default 10.0 kHz, range 0–20 kHz).
- **降噪强度** (Noise gate): silences the mic when you're not speaking to eliminate background hum (default 5, range 0–10, 0 = off).

Settings are automatically remembered across restarts.

### File Organization

Recordings are automatically saved into subfolders by app name under `recordings\`, e.g. `recordings\Weixin\2026-07-20 Weixin.exe 14-30-00.m4a`.

### System Requirements

- **Windows 10 version 2004** (May 2020 update) or later
- Microphone access must be enabled (Windows Settings → Privacy → Microphone)

### Command-Line Version

The original CLI tool is also available (`ProcessAudioRecorder.exe`):

```
ProcessAudioRecorder.exe --list                    # list apps producing audio
ProcessAudioRecorder.exe --pid 1234 --mode 1 --mic on --mic-gain 4 --sys-gain 2 --path D:\rec.m4a
```

Full options: `--pid` / `--mode` (0=global, 1=include, 2=exclude) / `--mic on|off` / `--mic-gain` / `--sys-gain` / `--format m4a|wav` / `--list` / `--mic-test`

---

## 中文

基于 [CingZeoi/AudioLoopbackRecorder](https://github.com/CingZeoi/AudioLoopbackRecorder)（原作者张赐荣，MIT 协议）开发的 Windows 桌面录音工具。

**核心能力**：只录你指定的软件（比如微信通话）的声音，其他软件一概不进。无需虚拟声卡，完全依靠 Windows 原生接口。

### 为什么需要它

- 录微信通话时，浏览器/音乐/通知声全部排除，只留下通话双方的语音
- 录在线课程/会议时，后台杂音不会混进来
- 比系统"立体声混音"更精准——像"窗口截图"一样选中特定软件

### 下载

从 **[GitHub Releases](https://github.com/muhan30/Process-Audio-Recorder/releases)** 下载最新版本（732 KB，单文件，无需安装）。

### 怎么用

1. 双击 `ProcessAudioRecorderGUI.exe`
2. **微信自动录音**：勾上"微信通话自动录音"，接/打电话即自动开始，挂断自动停止
3. **手动模式**：让目标软件出个声，点 **刷新**，选中软件，点 **开始录音**
4. 通完话点 **停止并保存**，文件保存在 `recordings\软件名\`

> 点窗口 ✕ 会缩到右下角托盘继续录音，不中断。
> 底部录音历史列表显示所有历史录音，双击即可播放。

### 设置

- **系统声音增益**：对方声音太小？把数字调大（默认 2.0 倍，范围 0.1–8.0）
- **麦克风增益**：自己声音太小？调到 4.0 倍试试（范围 0.1–8.0）
- **输出格式**：M4A 压缩（推荐，省空间）/ WAV 无损
- **低通滤波**：滤掉麦克风的高频电流嘶声（默认 10.0 kHz，0=关闭，范围 0–20 kHz）
- **降噪强度**：不说话时自动静音，消除全频段底噪（默认 5，0=关闭，10=最强）

改完自动记住，下次不用再调。

### 文件组织

录音文件自动按软件名分文件夹存放，如 `recordings\Weixin\2026-07-20 Weixin.exe 14-30-00.m4a`。

### 系统要求

- **Windows 10 2004**（2020年5月更新）或更高版本
- 麦克风权限需开启（Windows 设置 → 隐私 → 麦克风）

### 命令行版本

原来的命令行工具依然可用（`ProcessAudioRecorder.exe`）：

```
ProcessAudioRecorder.exe --list                         # 列出正在发声的进程
ProcessAudioRecorder.exe --pid 1234 --mode 1 --mic on --mic-gain 4 --sys-gain 2 --path D:\rec.m4a
```

所有参数：`--pid` / `--mode`（0=全局 1=包含 2=排除）/ `--mic on|off` / `--mic-gain` / `--sys-gain` / `--format m4a|wav` / `--list` / `--mic-test`

---

## License / 许可

[MIT](LICENSE) © 原作者张赐荣（CingZeoi），本仓库为其 fork 衍生作品。

This repository is a fork of the original work by 张赐荣 (CingZeoi) under the MIT License.
