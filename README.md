# Process Audio Recorder —— 进程隔离录音机

基于 [CingZeoi/AudioLoopbackRecorder](https://github.com/CingZeoi/AudioLoopbackRecorder)（原作者张赐荣，MIT 协议，原文见 [README_ORIGINAL.md](README_ORIGINAL.md)）开发的 Windows 桌面录音工具。

**核心能力**：只录你指定的软件（比如微信通话），其他软件的声音一概不进录音。无需虚拟声卡，完全依靠 Windows 原生接口。

## 为什么需要它

- 录微信通话时，浏览器/音乐/通知声全部排除，只留下通话双方的语音
- 录在线课程/会议时，后台杂音不会混进来
- 比系统"立体声混音"更精准——像"窗口截图"一样选中特定软件

## 怎么用

1. 双击 `ProcessAudioRecorderGUI.exe`（单文件，绿色免安装）
2. 让目标软件出个声（比如接通微信电话），点 **刷新**
3. 在列表中选中要录的软件，勾选"同时录我的麦克风"
4. 点 **开始录音**，说话——看到电平条在跳就对了
5. 通完话点 **停止并保存**，文件在 `recordings\` 文件夹

> 点窗口 ✕ 会缩到右下角托盘继续录音，不中断。

## 点"设置"可以调什么

- **系统声音增益**：对方声音太小？把数字调大（默认 2.0）
- **麦克风增益**：自己声音太小？调到 4.0 试试
- **输出格式**：M4A 压缩（推荐，省空间）/ WAV 无损

改完自动记住，下次不用再调。

## 系统要求

- **Windows 10 2004**（2020年5月更新）或更高版本
- 麦克风权限需开启（Windows 设置 → 隐私 → 麦克风）

## 命令行版本

原来的命令行工具依然可用（`ProcessAudioRecorder.exe`）：

```
ProcessAudioRecorder.exe --list                         # 列出正在发声的进程
ProcessAudioRecorder.exe --pid 1234 --mode 1 --mic on --mic-gain 4 --sys-gain 2 --path D:\rec.m4a
```

所有参数：`--pid` / `--mode`（0=全局 1=包含 2=排除）/ `--mic on|off` / `--mic-gain` / `--sys-gain` / `--format m4a|wav` / `--list` / `--mic-test`

## 技术支持

关于原项目的实现原理、架构细节，见 [README_ORIGINAL.md](README_ORIGINAL.md)。

## 许可

[MIT](LICENSE) © 原作者张赐荣（CingZeoi），本仓库为其 fork 衍生作品。
