# PLAN: 低通滤波降噪 + 文件大小显示修正

> 对应 SPEC: `docs/superpowers/specs/2026-07-20-noise-gate-filesize-fix.md`
> 日期: 2026-07-20 | 状态: 待审查

---

## 一、改动文件清单

| # | 文件 | 操作 | 说明 |
|---|------|------|------|
| 1 | `source/M4aSink.h` | 改 | 新增 `m_filePath` 成员；`BytesWritten()` 逻辑不变（声明不改） |
| 2 | `source/M4aSink.cpp` | 改 | `Initialize` 保存路径；`BytesWritten()` 读磁盘 |
| 3 | `source/AudioMixer.h` | 改 | 新增 `BiquadStereo` 结构体 + `SetLowpassCutoff()` + 成员 |
| 4 | `source/AudioMixer.cpp` | 改 | 实现滤波器 + `MixWithLoopback` 中调用 |
| 5 | `source/CaptureEngine.h` | 改 | 新增 `SetLowpassCutoff()` 声明 |
| 6 | `source/CaptureEngine.cpp` | 改 | 实现透传 |
| 7 | `source/GUI/MainWindow.h` | 改 | 新增 `m_lowpassCutoff` 成员 |
| 8 | `source/GUI/MainWindow.cpp` | 改 | LoadSettings/SaveSettings 增加 LowpassCutoff；OnStart 传递 |
| 9 | `source/GUI/SettingsDialog.h` | 改 | `SettingsData` 增加 `lowpassCutoff` 字段 |
| 10 | `source/GUI/SettingsDialog.cpp` | 改 | 窗口放大 + 新增输入框 + INI 读写 |

## 二、逐步实施

---

### 步骤 1：M4aSink —— 文件大小读磁盘

**文件**：`source/M4aSink.h`、`source/M4aSink.cpp`

**M4aSink.h 改动**：
- 在 `private:` 区域新增成员 `std::wstring m_filePath;`
- `BytesWritten()` 声明不变（仍是 `UINT64 BytesWritten() const override`）

**M4aSink.cpp 改动**：
- `Initialize()` 方法中，写入 `m_filePath = filePath;`（在 `BeginWriting()` 之前）
- `BytesWritten()` 实现改为：

```cpp
UINT64 M4aSink::BytesWritten() const
{
    // 读磁盘实际文件大小（M4A 是压缩格式，PCM 输入 ≠ 文件大小）
    if (m_filePath.empty()) return 0;
    struct __stat64 st;
    if (_wstat64(m_filePath.c_str(), &st) == 0)
        return static_cast<UINT64>(st.st_size);
    return 0;
}
```
- 新增 `#include <sys/stat.h>`

**注意**：WAV 的 `WavSink::BytesWritten()` 保持不变（PCM 数据量 = 文件大小，不需要改）。

---

### 步骤 2：AudioMixer —— 二阶巴特沃斯低通滤波器

**文件**：`source/AudioMixer.h`、`source/AudioMixer.cpp`

#### 2a. AudioMixer.h 新增结构体和方法

在 `AudioMixer` 类内部（`private:` 区域之前）新增：

```cpp
// 二阶巴特沃斯低通滤波器（Biquad, Direct Form 1），用于麦克风降噪
struct BiquadStereo
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;  // 归一化系数
    float x1L = 0, x2L = 0, y1L = 0, y2L = 0;       // 左声道状态
    float x1R = 0, x2R = 0, y1R = 0, y2R = 0;       // 右声道状态

    // 按截止频率重新计算系数。fc=0 时直通（不滤波）。
    void SetCutoff(float fcHz, float sampleRate = 44100.0f);

    // 原地处理立体声 16bit PCM（count = INT16 个数，必须为偶数）
    void Process(INT16* samples, size_t count);

    // 清零所有状态（录音开始时调用）
    void Reset();
};
```

在 `public:` 区域新增方法声明：
```cpp
// 设置低通滤波器截止频率（Hz），0 = 关闭
void SetLowpassCutoff(float fcHz);
```

在 `private:` 区域新增成员：
```cpp
BiquadStereo m_filter;                    // 低通滤波器（含系数 + 状态）
std::atomic<float> m_lowpassCutoff{ 0 };  // 截止频率 Hz，0=关闭
```

#### 2b. AudioMixer.cpp 实现

**`BiquadStereo::SetCutoff`**：

```cpp
void AudioMixer::BiquadStereo::SetCutoff(float fcHz, float sampleRate)
{
    Reset();  // 改截止频率必须清状态，否则旧状态导致振荡

    if (fcHz <= 0.0f || fcHz >= sampleRate * 0.49f)
    {
        // 直通模式：系数等效 y[n] = x[n]
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
        return;
    }

    constexpr float PI = 3.14159265f;
    constexpr float Q = 0.70710678f;  // 1/sqrt(2)，巴特沃斯最平坦响应

    float omega = 2.0f * PI * fcHz / sampleRate;
    float cosW = cosf(omega);
    float sinW = sinf(omega);
    float alpha = sinW / (2.0f * Q);

    // 未归一化系数
    float b0_raw = (1.0f - cosW) * 0.5f;
    float b1_raw = 1.0f - cosW;
    float b2_raw = (1.0f - cosW) * 0.5f;
    float a0_raw = 1.0f + alpha;
    float a1_raw = -2.0f * cosW;
    float a2_raw = 1.0f - alpha;

    // 归一化
    b0 = b0_raw / a0_raw;
    b1 = b1_raw / a0_raw;
    b2 = b2_raw / a0_raw;
    a1 = a1_raw / a0_raw;  // 注意：a1 值为负
    a2 = a2_raw / a0_raw;
}
```

**`BiquadStereo::Process`**：

```cpp
void AudioMixer::BiquadStereo::Process(INT16* samples, size_t count)
{
    if (b0 == 1.0f && b1 == 0.0f && b2 == 0.0f && a1 == 0.0f && a2 == 0.0f)
        return;  // 直通，零开销

    for (size_t i = 0; i < count; i += 2)
    {
        float xL = static_cast<float>(samples[i]);
        float xR = static_cast<float>(samples[i + 1]);

        float yL = b0 * xL + b1 * x1L + b2 * x2L - a1 * y1L - a2 * y2L;
        float yR = b0 * xR + b1 * x1R + b2 * x2R - a1 * y1R - a2 * y2R;

        // 饱和钳制到 16bit 范围
        int iL = static_cast<int>(yL);
        int iR = static_cast<int>(yR);
        if (iL < -32768) iL = -32768; else if (iL > 32767) iL = 32767;
        if (iR < -32768) iR = -32768; else if (iR > 32767) iR = 32767;
        samples[i]     = static_cast<INT16>(iL);
        samples[i + 1] = static_cast<INT16>(iR);

        // 状态推移
        x2L = x1L; x1L = xL;
        x2R = x1R; x1R = xR;
        y2L = y1L; y1L = yL;
        y2R = y1R; y1R = yR;
    }
}
```

**`BiquadStereo::Reset`**：
```cpp
void AudioMixer::BiquadStereo::Reset()
{
    x1L = x2L = y1L = y2L = 0.0f;
    x1R = x2R = y1R = y2R = 0.0f;
}
```

**`SetLowpassCutoff`**：
```cpp
void AudioMixer::SetLowpassCutoff(float fcHz)
{
    m_lowpassCutoff = fcHz;
    m_filter.SetCutoff(fcHz);
}
```

**`MixWithLoopback` 改动**（在取 mic 数据后、应用增益前插入滤波）：

在 `MixWithLoopback` 中，`std::copy` 之后、逐样本混音之前，新增：

```cpp
// 低通滤波：滤除麦克风高频噪声（电流嘶声）
if (m_lowpassCutoff.load() > 0.0f)
{
    m_filter.Process(reinterpret_cast<INT16*>(micChunk.data()), micChunk.size() / sizeof(INT16));
}
```

需要 `#include <cmath>` 用于 `cosf`/`sinf`。

---

### 步骤 3：CaptureEngine 透传

**文件**：`source/CaptureEngine.h`、`source/CaptureEngine.cpp`

**CaptureEngine.h**：
在 public 区域新增：
```cpp
// 设置麦克风低通滤波器截止频率（Hz），0 = 关闭
void SetLowpassCutoff(float fcHz);
```

**CaptureEngine.cpp**：
新增实现：
```cpp
void CaptureEngine::SetLowpassCutoff(float fcHz)
{
    m_impl->mixer.SetLowpassCutoff(fcHz);
}
```

---

### 步骤 4：GUI 设置集成

**文件**：`source/GUI/SettingsDialog.h`、`source/GUI/SettingsDialog.cpp`、`source/GUI/MainWindow.h`、`source/GUI/MainWindow.cpp`

#### 4a. SettingsDialog.h

`SettingsData` 新增字段：
```cpp
float lowpassCutoff = 10.0f;  // kHz，0 = 关闭低通滤波
```

#### 4b. SettingsDialog.cpp

**布局调整**：窗口高度 220 → 260。新增一行"低通滤波 (kHz)"输入框，位置在麦克风增益和输出格式之间。

新增全局变量 `static HWND g_hLowpassEdit;`。

**WM_CREATE 改动**：
在"麦克风增益"行之后、"输出格式"之前，插入：
```cpp
// 低通滤波截止频率
y += 38;
CreateWindow(WC_STATIC, L"低通滤波 (kHz), 0=关闭", WS_CHILD | WS_VISIBLE | SS_LEFT,
    14, y + 5, 200, 22, hWnd, nullptr, hi, nullptr);
swprintf_s(buf, L"%.1f", g_pData->lowpassCutoff);
g_hLowpassEdit = CreateWindow(WC_EDIT, buf, WS_CHILD | WS_VISIBLE | WS_BORDER,
    220, y, 60, 26, hWnd, nullptr, hi, nullptr);
SendMessage(g_hLowpassEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
CreateWindow(WC_STATIC, L"kHz（10=默认，0=关闭）", WS_CHILD | WS_VISIBLE | SS_LEFT,
    290, y + 5, 150, 22, hWnd, nullptr, hi, nullptr);

// 输出格式（下移）
y += 46;
// ... 不改，原本就写 y += 46
```

**IDC_SETTINGS_OK 改动**：
- 新增对低通滤波输入框的解析：`0.0` 到 `20.0` 之间，用 `wcstod`
- 赋值 `g_pData->lowpassCutoff`

**窗口尺寸**：`Show()` 中 `CreateWindowEx` 高度 220 → 260

**LoadFromIni** 新增：
```cpp
data.lowpassCutoff = (float)GetPrivateProfileInt(L"Settings", L"LowpassCutoff", 100, iniPath.c_str()) / 10.0f;
// 默认 100 → 10.0 kHz
```

**SaveToIni** 新增：
```cpp
wsprintfW(buf, L"%d", (int)(data.lowpassCutoff * 10));
WritePrivateProfileString(L"Settings", L"LowpassCutoff", buf, iniPath.c_str());
```

#### 4c. MainWindow.h

新增成员：
```cpp
float m_lowpassCutoff = 10.0f;  // kHz, 0 = off
```

#### 4d. MainWindow.cpp

**LoadSettings**：`cfg.lowpassCutoff` 已在 `LoadFromIni` 中读取，新增：
```cpp
m_lowpassCutoff = cfg.lowpassCutoff;
```

**SaveSettings**：新增赋值：
```cpp
cfg.lowpassCutoff = m_lowpassCutoff;
```

**OnStart**：在 `m_engine.SetMicGain(...)` 之后新增：
```cpp
m_engine.SetLowpassCutoff(m_lowpassCutoff * 1000.0f);  // kHz → Hz
```

**OnSettings**：赋值区新增：
```cpp
m_lowpassCutoff = cfg.lowpassCutoff;
```

---

## 三、编译验证

```bash
cmake -B build && cmake --build build
```
- CLI target: `ProcessAudioRecorder.exe`
- GUI target: `ProcessAudioRecorderGUI.exe`
- 预期：零 warning、零 error

---

## 四、对已有模块的影响分析

| 模块 | 影响 | 风险 |
|------|------|------|
| WavSink | 无（不改） | 无 |
| 系统声音路径 | 无（滤波只作用于 micChunk） | 无 |
| CLI | CLI 不经过 AudioMixer 的滤波器（CLI 有自己的混音路径） | 需确认 CLI 编译通过 |
| 已有 Gain 逻辑 | 无（滤波在 gain 之前，线性操作顺序可交换） | 无 |
| 设置对话框 | 高度 220→260，布局下移 | 确认所有控件完整可见 |
| INI 文件 | 新增一个键 `LowpassCutoff` | 旧 INI 无此键时自动取默认值 100（=10.0kHz），向后兼容 |

---

## 五、测试步骤

1. 编译 `cmake -B build && cmake --build build`
2. 打开 GUI，确认设置对话框中低通滤波字段可见（10.0 kHz 默认值）
3. 选一个 PID 开始录音，确认：
   - 不说话时高频嘶声减少
   - 说话声音正常、齿音不丢
4. 将低通滤波设为 0，重新录音确认关闭后行为与旧版一致
5. M4A 录音 30 秒，每 10 秒对比 GUI 显示值和资源管理器实际文件大小
6. WAV 录音确认文件大小显示不受影响
7. 检查 INI 文件中 `LowpassCutoff` 键正确读写
