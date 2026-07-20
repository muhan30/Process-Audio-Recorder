# SPEC: 自动刷新自动选中 + 录音历史列表

> 版本: 1.0 | 日期: 2026-07-20 | 状态: 自审中

---

## 一、背景

当前每次录音前需手动：打开 APP → 点刷新 → 在列表中找微信 → 选中 → 开始录音。流程繁琐。录音后去文件夹翻文件也不方便。

## 二、需求

### 需求 1：自动刷新 + 自动选中

**目标**：启动 APP 后自动刷新，若上次录音的进程还在列表中则自动选中，一键开始录音。

**原理**：
- 你在列表中选中某个进程 → 自动保存进程名到 INI
- 下次启动 → 自动刷新列表 → 找到同名进程 → 高亮选中
- PID 变没关系，只按名字匹配

**验收标准**：
- APP 启动后自动执行一次刷新（不再显示空列表）
- 如果 INI 中记录的进程存在于列表中，自动选中该行
- 用户手动选其他进程时，自动更新 INI 记录
- 如果进程未启动（找不到），不做任何选中，等用户自己选
- INI 键名：`[Settings] LastProcess`

### 需求 2：录音历史列表

**目标**：在 GUI 中直接看到所有历史录音，双击即可播放，不用去文件夹翻。

**原理**：
- 扫描 `recordings/` 下所有子文件夹
- 在主窗口下半部分显示一个列表
- 每次启动和录音停止后自动刷新

**验收标准**：
- 主窗口底部新增一个列表区域，显示历史录音
- 列：文件名、日期、大小
- 按时间倒序排列（最新的在上面）
- 双击某行 → 用系统默认播放器打开文件
- 列表高度可随窗口缩放自动调整
- 最多显示最近 50 条

---

## 三、涉及模块

| 模块 | 改动 | 说明 |
|------|------|------|
| `source/GUI/MainWindow.cpp` | 改 | OnRefresh 自动选中；选中时保存 LastProcess；新增历史列表 + 双击打开 |
| `source/GUI/MainWindow.h` | 改 | 新增历史列表 HWND + 扫描方法 |
| `source/GUI/SettingsDialog.cpp` | 不改 | — |
| `source/GUI/SessionList.cpp` | 改 | 新增 `SelectByProcessName()` 方法 |

## 四、实现概要

### 自动选中

- `SessionList::SelectByProcessName(name)` — 遍历列表，找到匹配的行，`ListView_SetSelection` 选中
- `MainWindow::OnRefresh` — 刷新后调用 `SelectByProcessName(m_lastProcess)`
- 用户手动选中 → `WM_NOTIFY` / `LVN_ITEMCHANGED` → 保存 `LastProcess` 到 INI
- `LoadSettings` 读取 `LastProcess`；`SaveSettings` 写入

### 录音历史

- 新建 `RecordingHistoryList` 类（类似 `SessionList` 的轻量封装）
- `Scan()` 扫描 `recordings/` 递归所有 `.m4a` `.wav`，读文件时间+大小
- 双缓冲排序：最新在上
- `WM_LBUTTONDBLCLK` → `ShellExecute("open", filePath)`

### 窗口布局

- 上方：原有控件不变（列表、按钮、设置等）
- 下方：新增历史列表（初高 120px）
- 总高度 440 → 560
- WM_SIZE 时自动调整历史列表高度
