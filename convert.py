import tkinter as tk
from tkinter import filedialog, colorchooser, messagebox, ttk
from PIL import Image, ImageTk
import struct
import os
import json
import wave

class ResourceConverterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32-S3 UI与音频资源转换工具")
        self.root.geometry("620x780")
        
        # --- 配置文件加载 ---
        self.config_file = "converter_config.json"
        self.config_data = self.load_config()
        
        # --- 全局变量与保存状态 ---
        self.var_out_dir = tk.StringVar(value=self.config_data.get("output_dir", os.getcwd()))
        self.var_img_name = tk.StringVar(value=self.config_data.get("img_name", ""))
        self.var_array_name = tk.StringVar(value=self.config_data.get("audio_name", ""))

        # --- 内部组件变量 ---
        self.image_path = None
        self.original_image = None
        self.preview_image = None
        self.bg_color = (0, 0, 0)
        self.wav_path = None

        self.setup_ui()

    def load_config(self):
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, 'r', encoding='utf-8') as f:
                    return json.load(f)
            except:
                pass
        return {}

    def save_config(self):
        try:
            config = {
                "output_dir": self.var_out_dir.get(),
                "img_name": self.var_img_name.get(),
                "audio_name": self.var_array_name.get()
            }
            with open(self.config_file, 'w', encoding='utf-8') as f:
                json.dump(config, f)
        except:
            pass

    def setup_ui(self):
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill="both", expand=True, padx=10, pady=10)

        self.tab_image = ttk.Frame(self.notebook)
        self.tab_audio = ttk.Frame(self.notebook)
        
        self.notebook.add(self.tab_image, text="📺 图片转 RGB565 (.bin)")
        self.notebook.add(self.tab_audio, text="🎵 音频转 C 数组 (.h)")

        self.build_image_tab()
        self.build_audio_tab()
        self.build_shared_bottom()

    def build_shared_bottom(self):
        frame_export = tk.LabelFrame(self.root, text="📁 全局输出目录", padx=10, pady=10)
        frame_export.pack(fill="x", side="bottom", padx=10, pady=10)

        entry_out_dir = tk.Entry(frame_export, textvariable=self.var_out_dir, width=50)
        entry_out_dir.pack(side="left", padx=5, fill="x", expand=True)
        tk.Button(frame_export, text="浏览...", command=self.choose_out_dir).pack(side="left", padx=5)

    def choose_out_dir(self):
        d = filedialog.askdirectory(title="选择输出目录", initialdir=self.var_out_dir.get())
        if d:
            self.var_out_dir.set(d)
            self.save_config()

    # ==========================================
    #               图片转换模块
    # ==========================================
    def build_image_tab(self):
        parent = self.tab_image

        frame_file = tk.LabelFrame(parent, text="1. 选择图片", padx=10, pady=5)
        frame_file.pack(fill="x", padx=10, pady=5)
        tk.Button(frame_file, text="加载图片", command=self.load_image).pack(side="left", padx=5)
        self.lbl_img_path = tk.Label(frame_file, text="未选择任何图片", fg="gray")
        self.lbl_img_path.pack(side="left", padx=5)

        frame_settings = tk.LabelFrame(parent, text="2. 画布与布局设置", padx=10, pady=5)
        frame_settings.pack(fill="x", padx=10, pady=5)

        tk.Label(frame_settings, text="宽度 (px):").grid(row=0, column=0, sticky="e", pady=5)
        self.var_width = tk.IntVar(value=240)
        tk.Entry(frame_settings, textvariable=self.var_width, width=8).grid(row=0, column=1, sticky="w")

        tk.Label(frame_settings, text="高度 (px):").grid(row=0, column=2, sticky="e", pady=5, padx=(10, 0))
        self.var_height = tk.IntVar(value=240)
        tk.Entry(frame_settings, textvariable=self.var_height, width=8).grid(row=0, column=3, sticky="w")

        tk.Label(frame_settings, text="背景颜色:").grid(row=1, column=0, sticky="e", pady=5)
        self.btn_color = tk.Button(frame_settings, bg="#000000", width=6, command=self.choose_color)
        self.btn_color.grid(row=1, column=1, sticky="w")

        tk.Label(frame_settings, text="适配模式:").grid(row=2, column=0, sticky="e", pady=5)
        self.var_fit = tk.StringVar(value="等比例缩放 (居中)")
        ttk.Combobox(frame_settings, textvariable=self.var_fit, values=["保持原尺寸 (居中)", "保持原尺寸 (左上角)", "等比例缩放 (居中)", "拉伸铺满"], state="readonly", width=18).grid(row=2, column=1, columnspan=2, sticky="w")

        tk.Label(frame_settings, text="字节序:").grid(row=3, column=0, sticky="e", pady=5)
        self.var_endian = tk.StringVar(value="大端序 (Big-Endian)")
        ttk.Combobox(frame_settings, textvariable=self.var_endian, values=["大端序 (Big-Endian)", "小端序 (Little-Endian)"], state="readonly", width=18).grid(row=3, column=1, columnspan=2, sticky="w")

        tk.Label(frame_settings, text="输出文件名 (不含后缀):").grid(row=4, column=0, sticky="e", pady=5)
        tk.Entry(frame_settings, textvariable=self.var_img_name, width=18).grid(row=4, column=1, columnspan=2, sticky="w")

        tk.Button(frame_settings, text="更新预览", command=self.update_preview).grid(row=5, column=0, columnspan=4, pady=5)

        frame_preview = tk.LabelFrame(parent, text="3. 效果预览", padx=10, pady=5)
        frame_preview.pack(fill="both", expand=True, padx=10, pady=5)
        self.lbl_preview = tk.Label(frame_preview, text="预览区域", bg="gray")
        self.lbl_preview.pack(expand=True)

        tk.Button(parent, text="导出 RGB565 .bin", font=("Arial", 12, "bold"), bg="#4CAF50", fg="white", command=self.export_bin).pack(fill="x", padx=10, pady=10, ipady=5)

    def load_image(self):
        file_path = filedialog.askopenfilename(filetypes=[("Image Files", "*.png;*.jpg;*.jpeg;*.bmp")])
        if file_path:
            self.image_path = file_path
            self.original_image = Image.open(file_path).convert("RGBA")
            self.lbl_img_path.config(text=os.path.basename(file_path))
            self.var_width.set(self.original_image.width)
            self.var_height.set(self.original_image.height)
            
            # 如果名字框为空，自动填入原始文件名
            if not self.var_img_name.get().strip():
                self.var_img_name.set(os.path.splitext(os.path.basename(file_path))[0])
                
            self.update_preview()

    def choose_color(self):
        color = colorchooser.askcolor(title="选择画布背景色", initialcolor=self.bg_color)
        if color[0]:
            self.bg_color = tuple(map(int, color[0]))
            self.btn_color.config(bg="#{:02x}{:02x}{:02x}".format(*self.bg_color))
            self.update_preview()

    def process_image(self):
        if not self.original_image: return None
        try:
            cw, ch = self.var_width.get(), self.var_height.get()
        except tk.TclError: return None 

        fit = self.var_fit.get()
        canvas = Image.new("RGBA", (cw, ch), self.bg_color + (255,))
        img = self.original_image.copy()
        iw, ih = img.size

        if fit == "拉伸铺满": img = img.resize((cw, ch), Image.Resampling.LANCZOS)
        elif fit == "等比例缩放 (居中)":
            r = min(cw / iw, ch / ih)
            img = img.resize((max(1, int(iw * r)), max(1, int(ih * r))), Image.Resampling.LANCZOS)
            
        px, py = 0, 0
        if "居中" in fit:
            px, py = (cw - img.width) // 2, (ch - img.height) // 2

        canvas.paste(img, (px, py), mask=img)
        return canvas.convert("RGB")

    def update_preview(self):
        if not self.original_image: return
        try:
            processed = self.process_image()
            if processed:
                preview = processed.copy()
                preview.thumbnail((300, 300), Image.Resampling.LANCZOS)
                self.preview_image = ImageTk.PhotoImage(preview)
                self.lbl_preview.config(image=self.preview_image, text="", width=preview.width, height=preview.height)
        except: pass 

    def export_bin(self):
        if not self.original_image:
            return messagebox.showwarning("警告", "请先加载图片！")
            
        out_dir = self.var_out_dir.get()
        if not os.path.isdir(out_dir): return messagebox.showerror("错误", "输出目录不存在！")
        
        img_name = self.var_img_name.get().strip()
        if not img_name: img_name = "image_data"
        
        self.save_config()
        save_path = os.path.join(out_dir, f"{img_name}.bin")

        try:
            final_img = self.process_image()
            pixels = final_img.load()
            w, h = final_img.size
            is_be = "Big-Endian" in self.var_endian.get()

            with open(save_path, "wb") as f:
                for y in range(h):
                    for x in range(w):
                        r, g, b = pixels[x, y]
                        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                        f.write(struct.pack(">H" if is_be else "<H", rgb565))

            messagebox.showinfo("成功", f"文件已生成:\n{save_path}\n尺寸: {w}x{h}")
        except Exception as e:
            messagebox.showerror("错误", f"导出失败:\n{e}")

    # ==========================================
    #               音频转换模块
    # ==========================================
    def build_audio_tab(self):
        parent = self.tab_audio

        frame_file = tk.LabelFrame(parent, text="1. 选择 WAV 音频", padx=10, pady=10)
        frame_file.pack(fill="x", padx=10, pady=10)
        tk.Button(frame_file, text="加载 .wav 文件", command=self.load_wav).pack(side="left", padx=5)
        self.lbl_wav_path = tk.Label(frame_file, text="未选择任何音频", fg="gray")
        self.lbl_wav_path.pack(side="left", padx=5)

        self.lbl_wav_info = tk.Label(parent, text="音频参数: 待加载", fg="blue", font=("Arial", 10))
        self.lbl_wav_info.pack(fill="x", padx=15, pady=5)

        frame_settings = tk.LabelFrame(parent, text="2. C 数组与文件设置", padx=10, pady=10)
        frame_settings.pack(fill="x", padx=10, pady=10)

        tk.Label(frame_settings, text="文件名与数组名 (不含后缀):").grid(row=0, column=0, sticky="e", pady=5)
        tk.Entry(frame_settings, textvariable=self.var_array_name, width=30).grid(row=0, column=1, sticky="w", padx=5)
        tk.Label(frame_settings, text="*将作为.h文件名和C语言变量名", fg="gray").grid(row=1, column=1, sticky="w", padx=5)

        tk.Button(parent, text="导出 C 数组 (.h)", font=("Arial", 12, "bold"), bg="#008CBA", fg="white", command=self.export_wav).pack(fill="x", padx=10, pady=20, ipady=5)

    def load_wav(self):
        file_path = filedialog.askopenfilename(filetypes=[("WAV Audio", "*.wav")])
        if file_path:
            self.wav_path = file_path
            self.lbl_wav_path.config(text=os.path.basename(file_path))
            
            # 如果名字框为空，自动提取整理后的名字
            if not self.var_array_name.get().strip():
                base_name = os.path.splitext(os.path.basename(file_path))[0]
                clean_name = "".join(c if c.isalnum() else "_" for c in base_name)
                self.var_array_name.set(f"sound_{clean_name}")

            try:
                with wave.open(file_path, 'rb') as w:
                    rate = w.getframerate()
                    bits = w.getsampwidth() * 8
                    channels = w.getnchannels()
                    ch_str = "双声道" if channels == 2 else "单声道"
                    info = f"{rate}Hz, {bits}-bit, {ch_str}"
                    
                    if rate != 16000 or bits != 16 or channels != 2:
                        self.lbl_wav_info.config(text=f"⚠️ 参数警告: {info} (建议使用 16kHz, 16-bit, 双声道)", fg="red")
                    else:
                        self.lbl_wav_info.config(text=f"✅ 参数正确: {info}", fg="green")
            except Exception as e:
                self.lbl_wav_info.config(text="无法读取 WAV 参数格式", fg="red")

    def export_wav(self):
        if not self.wav_path:
            return messagebox.showwarning("警告", "请先加载 WAV 音频！")
        
        out_dir = self.var_out_dir.get()
        if not os.path.isdir(out_dir): return messagebox.showerror("错误", "输出目录不存在！")

        array_name = self.var_array_name.get().strip()
        if not array_name: array_name = "sound_data"

        save_path = os.path.join(out_dir, f"{array_name}.h")

        try:
            with wave.open(self.wav_path, 'rb') as w:
                if w.getframerate() != 16000 or w.getsampwidth() != 2 or w.getnchannels() != 2:
                    messagebox.showerror("格式错误", f"为防止单片机爆音，强制要求音频必须是：\n16000Hz, 16-bit, 双声道 (Stereo)\n\n当前参数: {w.getframerate()}Hz, {w.getsampwidth()*8}-bit, {w.getnchannels()}声道")
                    return
                raw_data = w.readframes(w.getnframes())

            with open(save_path, 'w') as f:
                f.write(f"// Auto-generated from {os.path.basename(self.wav_path)}\n")
                f.write("#pragma once\n")
                f.write("#include <Arduino.h>\n\n")
                f.write(f"const uint8_t {array_name}[] PROGMEM = {{\n")
                
                hex_list = [f"0x{b:02X}" for b in raw_data]
                for i in range(0, len(hex_list), 16):
                    f.write("    " + ", ".join(hex_list[i:i+16]) + ",\n")
                    
                f.write("};\n")
                f.write(f"const uint32_t {array_name}_len = {len(raw_data)};\n")

            self.save_config()
            messagebox.showinfo("成功", f"C 数组已生成:\n{save_path}\n占用 Flash 大小: {len(raw_data) / 1024:.2f} KB")
        except Exception as e:
            messagebox.showerror("错误", f"转换失败:\n{e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = ResourceConverterApp(root)
    root.mainloop()