from PIL import Image
import struct
import os

# 1. 打开你的PNG图片并强制转换为纯RGB（丢弃透明通道）
try:
    img = Image.open("input.png").convert("RGB")
except Exception as e:
    print(f"找不到图片或图片损坏: {e}")
    exit()

# 2. 强制缩放到战术终端的带鱼屏分辨率
TARGET_W, TARGET_H = 284, 76
img = img.resize((TARGET_W, TARGET_H), Image.Resampling.LANCZOS)

# 3. 逐个像素转换为 RGB565 的二进制流
bin_data = bytearray()
for y in range(TARGET_H):
    for x in range(TARGET_W):
        r, g, b = img.getpixel((x, y))
        
        # 将 24位 RGB 压缩为 16位 RGB565
        # Red: 5 bits, Green: 6 bits, Blue: 5 bits
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        
        # 打包为两个字节 (小端序 Little Endian)
        bin_data.extend(struct.pack('<H', rgb565))

# 4. 写入最终的 bin 文件
output_file = "standby.bin"
with open(output_file, "wb") as f:
    f.write(bin_data)

print(f"转换成功！已生成 {output_file}，文件大小: {os.path.getsize(output_file)} 字节。")
print(f"如果大小不是严格的 43168 字节，请检查！")