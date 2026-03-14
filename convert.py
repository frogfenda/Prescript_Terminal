import tkinter as tk
from tkinter import filedialog, colorchooser, messagebox, ttk
from PIL import Image, ImageTk
import struct
import os

class RGB565ConverterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("图片转 RGB565 .bin 工具")
        self.root.geometry("600x650")
        
        # 内部变量
        self.image_path = None
        self.original_image = None
        self.preview_image = None
        self.bg_color = (0, 0, 0) # 默认黑色背景

        self.setup_ui()

    def setup_ui(self):
        # --- 1. 图片加载区域 ---
        frame_file = tk.LabelFrame(self.root, text="1. 选择图片", padx=10, pady=10)
        frame_file.pack(fill="x", padx=10, pady=5)

        self.btn_load = tk.Button(frame_file, text="加载图片", command=self.load_image)
        self.btn_load.pack(side="left", padx=5)

        self.lbl_path = tk.Label(frame_file, text="未选择任何图片", fg="gray")
        self.lbl_path.pack(side="left", padx=5)

        # --- 2. 画布与适配设置 ---
        frame_settings = tk.LabelFrame(self.root, text="2. 画布与布局设置", padx=10, pady=10)
        frame_settings.pack(fill="x", padx=10, pady=5)

        # 画布尺寸
        tk.Label(frame_settings, text="画布宽度 (px):").grid(row=0, column=0, sticky="e", pady=5)
        self.var_width = tk.IntVar(value=240)
        tk.Entry(frame_settings, textvariable=self.var_width, width=8).grid(row=0, column=1, sticky="w")

        tk.Label(frame_settings, text="画布高度 (px):").grid(row=0, column=2, sticky="e", pady=5, padx=(10, 0))
        self.var_height = tk.IntVar(value=240)
        tk.Entry(frame_settings, textvariable=self.var_height, width=8).grid(row=0, column=3, sticky="w")

        # 画布颜色
        tk.Label(frame_settings, text="背景颜色:").grid(row=1, column=0, sticky="e", pady=5)
        self.btn_color = tk.Button(frame_settings, bg="#000000", width=6, command=self.choose_color)
        self.btn_color.grid(row=1, column=1, sticky="w")

        # 适配模式
        tk.Label(frame_settings, text="适配模式:").grid(row=2, column=0, sticky="e", pady=5)
        self.var_fit = tk.StringVar(value="等比例缩放 (居中)")
        fit_options = ["保持原尺寸 (居中)", "保持原尺寸 (左上角)", "等比例缩放 (居中)", "拉伸铺满"]
        ttk.Combobox(frame_settings, textvariable=self.var_fit, values=fit_options, state="readonly", width=18).grid(row=2, column=1, columnspan=2, sticky="w")

        # 字节序设置 (针对不同单片机/屏幕)
        tk.Label(frame_settings, text="字节序 (Endian):").grid(row=3, column=0, sticky="e", pady=5)
        self.var_endian = tk.StringVar(value="大端序 (Big-Endian)")
        endian_options = ["大端序 (Big-Endian)", "小端序 (Little-Endian)"]
        ttk.Combobox(frame_settings, textvariable=self.var_endian, values=endian_options, state="readonly", width=18).grid(row=3, column=1, columnspan=2, sticky="w")

        # 预览按钮
        tk.Button(frame_settings, text="更新预览", command=self.update_preview).grid(row=4, column=0, columnspan=4, pady=10)

        # --- 3. 预览区域 ---
        frame_preview = tk.LabelFrame(self.root, text="3. 效果预览", padx=10, pady=10)
        frame_preview.pack(fill="both", expand=True, padx=10, pady=5)

        self.lbl_preview = tk.Label(frame_preview, text="预览区域", bg="gray", width=40, height=10)
        self.lbl_preview.pack(expand=True)

        # --- 4. 导出区域 ---
        frame_export = tk.Frame(self.root)
        frame_export.pack(fill="x", padx=10, pady=10)

        self.btn_export = tk.Button(frame_export, text="转换为 RGB565 .bin 并保存", font=("Arial", 12, "bold"), bg="#4CAF50", fg="white", command=self.export_bin)
        self.btn_export.pack(fill="x", ipady=5)

    def load_image(self):
        file_path = filedialog.askopenfilename(filetypes=[("Image Files", "*.png;*.jpg;*.jpeg;*.bmp")])
        if file_path:
            self.image_path = file_path
            self.original_image = Image.open(file_path).convert("RGBA")
            self.lbl_path.config(text=os.path.basename(file_path))
            # 自动读取原图尺寸作为默认画布尺寸
            self.var_width.set(self.original_image.width)
            self.var_height.set(self.original_image.height)
            self.update_preview()

    def choose_color(self):
        color = colorchooser.askcolor(title="选择画布背景色", initialcolor=self.bg_color)
        if color[0]:
            self.bg_color = tuple(map(int, color[0]))
            # 将 RGB 转换为 hex 显示在按钮上
            hex_color = "#{:02x}{:02x}{:02x}".format(*self.bg_color)
            self.btn_color.config(bg=hex_color)
            self.update_preview()

    def process_image(self):
        """核心处理逻辑：生成带有背景和正确适配的图片"""
        if not self.original_image:
            return None

        try:
            # 增加异常处理，防止输入框被清空时报错
            canvas_w = self.var_width.get()
            canvas_h = self.var_height.get()
        except tk.TclError:
            return None 

        fit_mode = self.var_fit.get()

        # 创建背景画布
        canvas = Image.new("RGBA", (canvas_w, canvas_h), self.bg_color + (255,))
        img = self.original_image.copy()
        img_w, img_h = img.size

        # 处理缩放
        if fit_mode == "拉伸铺满":
            img = img.resize((canvas_w, canvas_h), Image.Resampling.LANCZOS)
        elif fit_mode == "等比例缩放 (居中)":
            # 【修复点1】手动计算等比例缩放，这样既支持缩小，也支持小图自动放大
            ratio = min(canvas_w / img_w, canvas_h / img_h)
            new_w = max(1, int(img_w * ratio))
            new_h = max(1, int(img_h * ratio))
            img = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
            
        # 处理位置 (居中或左上角)
        paste_x, paste_y = 0, 0
        if "居中" in fit_mode:
            paste_x = (canvas_w - img.width) // 2
            paste_y = (canvas_h - img.height) // 2

        # 将图片粘贴到画布上 (使用自身作为 mask 确保带透明度的 PNG 正常显示)
        canvas.paste(img, (paste_x, paste_y), mask=img)
        
        # 转换为纯 RGB 模式供最终生成使用
        return canvas.convert("RGB")

    def update_preview(self):
        if not self.original_image:
            return

        try:
            processed_img = self.process_image()
            if processed_img:
                # 为了不让预览撑爆窗口，限制预览最大尺寸为 300x300
                preview = processed_img.copy()
                preview.thumbnail((300, 300), Image.Resampling.LANCZOS)
                
                self.preview_image = ImageTk.PhotoImage(preview)
                
                # 【修复点2】显式传入图片计算出的真实像素宽高，覆盖掉 Tkinter 初始的文本单位宽高
                self.lbl_preview.config(
                    image=self.preview_image, 
                    text="", 
                    width=preview.width, 
                    height=preview.height
                )
        except Exception as e:
            pass # 预览时的微小错误静默处理，防止弹窗打断操作
    def export_bin(self):
        if not self.original_image:
            messagebox.showwarning("警告", "请先加载一张图片！")
            return

        save_path = filedialog.asksaveasfilename(defaultextension=".bin", filetypes=[("Binary Files", "*.bin")], title="保存为")
        if not save_path:
            return

        try:
            final_img = self.process_image()
            pixels = final_img.load()
            width, height = final_img.size
            is_big_endian = "Big-Endian" in self.var_endian.get()

            with open(save_path, "wb") as f:
                for y in range(height):
                    for x in range(width):
                        r, g, b = pixels[x, y]
                        # 转换为 RGB565 (R:5, G:6, B:5)
                        # r & 0xF8: 取高5位; g & 0xFC: 取高6位; b >> 3: 取高5位
                        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                        
                        if is_big_endian:
                            # 写入大端序 (高字节在前)
                            f.write(struct.pack(">H", rgb565))
                        else:
                            # 写入小端序 (低字节在前)
                            f.write(struct.pack("<H", rgb565))

            messagebox.showinfo("成功", f"文件已成功保存至:\n{save_path}\n尺寸: {width}x{height}")
        except Exception as e:
            messagebox.showerror("错误", f"导出时发生错误:\n{e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = RGB565ConverterApp(root)
    root.mainloop()