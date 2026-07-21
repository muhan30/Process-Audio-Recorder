"""
M4A→AAC 格式转换工具
参考 MystiQ (Qt/FFmpeg) 的 FFmpeg 参数逻辑，用 Python subprocess 实现，无需 Qt。
用法: python convert.py input.m4a [output.aac]
      或拖拽 M4A 文件到脚本上
"""
import subprocess
import sys
import os
import shutil

def find_ffmpeg():
    """查找 ffmpeg.exe: 先找脚本同目录, 再找 build/ 目录, 最后 PATH"""
    # 1. 脚本同目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    for name in ["ffmpeg.exe"]:
        path = os.path.join(script_dir, name)
        if os.path.exists(path):
            return path
    # 2. build/ 目录 (开发时)
    build_path = os.path.join(script_dir, "..", "build", "ffmpeg.exe")
    if os.path.exists(build_path):
        return build_path
    # 3. 系统 PATH
    found = shutil.which("ffmpeg")
    if found:
        return found
    return None

def convert_m4a_to_aac(input_path, output_path=None, bitrate="128k"):
    """
    M4A → AAC 转换
    参数参考 MystiQ presets.xml 中 aac 预设:
      ffmpeg -y -i INPUT -vn -acodec aac -ab BITRATE OUTPUT
    """
    if not os.path.exists(input_path):
        print(f"[ERROR] 文件不存在: {input_path}")
        return False

    if not input_path.lower().endswith('.m4a'):
        print(f"[WARN] 输入不是 .m4a 文件: {input_path}")

    if output_path is None:
        base = os.path.splitext(input_path)[0]
        output_path = base + ".aac"

    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        print("[ERROR] 找不到 ffmpeg.exe！")
        print("  请下载 ffmpeg: https://ffmpeg.org/download.html")
        print("  把 ffmpeg.exe 放到脚本同目录或系统 PATH")
        return False

    # 参考 MystiQ FFmpegInterface::getOptionList() 的音频参数
    cmd = [
        ffmpeg,
        "-y",                    # 覆盖已存在的输出文件
        "-i", input_path,        # 输入文件
        "-vn",                   # 不要视频
        "-acodec", "aac",        # AAC 编码器
        "-ab", bitrate,          # 音频码率 (默认 128k)
        output_path
    ]

    print(f"[INFO] 转换: {os.path.basename(input_path)} → {os.path.basename(output_path)}")
    print(f"[CMD] {' '.join(cmd)}")

    try:
        # 参考 MystiQ QProcess 模式: 启动进程 → 读 stderr 获取进度 → 等待完成
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding='utf-8',
            errors='replace',
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
        )

        # 显示进度 (FFmpeg 进度信息输出到 stderr)
        for line in proc.stderr:
            line = line.strip()
            if "time=" in line:
                # 提取时间信息, 类似 MystiQ 的 check_progress()
                # ffmpeg 输出格式: size=  123kB time=00:00:05.12 bitrate= 196.1kbits/s
                parts = line.split()
                for p in parts:
                    if p.startswith("time="):
                        time_str = p[5:]
                        # 只显示时间, 不换行
                        print(f"\r  进度: {time_str}", end="", flush=True)
                        break

        proc.wait(timeout=300)  # 最多等 5 分钟

        if proc.returncode == 0:
            size = os.path.getsize(output_path)
            size_str = f"{size/1024:.0f} KB" if size < 1024*1024 else f"{size/1024/1024:.1f} MB"
            print(f"\r[OK] 完成: {output_path} ({size_str})  ")
            return True
        else:
            print(f"\n[ERROR] FFmpeg 返回错误码 {proc.returncode}")
            return False

    except subprocess.TimeoutExpired:
        proc.kill()
        print(f"\n[ERROR] 转换超时 (5分钟)")
        return False
    except Exception as e:
        print(f"\n[ERROR] {e}")
        return False

def main():
    if len(sys.argv) < 2:
        print("M4A → AAC 格式转换工具")
        print("用法: python convert.py <输入.m4a> [输出.aac]")
        print("  或: 拖拽 .m4a 文件到此脚本上")
        print("\n参数参考 MystiQ FFmpeg presets (ffmpeg -y -i input -vn -acodec aac -ab 128k output)")
        return

    # 支持拖拽多个文件
    success = 0
    fail = 0
    for path in sys.argv[1:]:
        if not os.path.isfile(path):
            print(f"[SKIP] 不是文件: {path}")
            continue
        if convert_m4a_to_aac(path):
            success += 1
        else:
            fail += 1

    if success + fail > 1:
        print(f"\n完成: {success} 成功, {fail} 失败")

    # Windows 拖拽运行时保持窗口
    if sys.platform == 'win32' and len(sys.argv) == 2:
        input("\n按 Enter 关闭...")

if __name__ == "__main__":
    main()
