"""生成纯黑雪花图标 app.ico（16/32/48/256 px 四个尺寸）"""
from PIL import Image, ImageDraw
import math

def draw_snowflake(draw, cx, cy, size, color):
    """在 draw 上绘制六瓣雪花"""
    lw = max(1, int(size * 0.08))
    tip_r = max(1, int(lw * 1.2))
    for i in range(6):
        angle = i * math.pi / 3
        sin_a, cos_a = math.sin(angle), math.cos(angle)
        # 主干端点
        tip_x = cx + cos_a * size * 0.42
        tip_y = cy - sin_a * size * 0.42
        draw.line([(cx, cy), (tip_x, tip_y)], fill=color, width=lw)
        # 左枝
        branch_y = cy - sin_a * size * 0.22
        branch_x = cx + cos_a * size * 0.22
        left_x = branch_x - cos_a * size * 0.14 - sin_a * size * 0.14 * 0.3
        left_y = branch_y + sin_a * size * 0.14 - cos_a * size * 0.14 * 0.3
        draw.line([(branch_x, branch_y), (left_x, left_y)], fill=color, width=lw)
        # 右枝
        right_x = branch_x + cos_a * size * 0.14 + sin_a * size * 0.14 * 0.3
        right_y = branch_y - sin_a * size * 0.14 + cos_a * size * 0.14 * 0.3
        draw.line([(branch_x, branch_y), (right_x, right_y)], fill=color, width=lw)
        # 端点圆
        draw.ellipse([tip_x - tip_r, tip_y - tip_r, tip_x + tip_r, tip_y + tip_r], fill=color)
    # 中心圆
    center_r = max(1, int(lw * 1.5))
    draw.ellipse([cx - center_r, cy - center_r, cx + center_r, cy + center_r], fill=color)

def make_icon(size):
    """生成一个纯黑雪花图标"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    margin = size * 0.1
    r = size * 0.2
    # 纯黑圆角背景
    draw.rounded_rectangle(
        [margin, margin, size - margin, size - margin],
        radius=int(r), fill=(26, 26, 46, 255)
    )
    # 青蓝雪花
    cx, cy = size / 2, size / 2
    snow_size = size * 0.78
    draw_snowflake(draw, cx, cy, snow_size, (144, 202, 249, 255))
    return img

# 生成 256x256，保存时用 sizes 参数自动包含 16/32/48/256 四个尺寸
img256 = make_icon(256)
img256.save('source/GUI/app.ico', format='ICO', sizes=[(16,16),(32,32),(48,48),(256,256)])
import os
print(f"Generated source/GUI/app.ico ({os.path.getsize('source/GUI/app.ico')} bytes)")
