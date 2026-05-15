# ==============================================================================================================================
# 这是可以把图片转化为没有 heads 的直接二进制 bin 文件的脚本。
# 额外集成：音频 WAV 转 C 数组、图形化 map.txt 字符集转 bdfconv 可识别的 Unicode map。
# ==============================================================================================================================

import tkinter as tk
from tkinter import filedialog, colorchooser, messagebox, ttk
from PIL import Image, ImageTk
import struct
import os
import json
import wave
import re


class ResourceConverterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32-S3 UI、音频与字库资源转换工具")
        self.root.geometry("700x840")
        
        # --- 配置文件加载 ---
        self.config_file = "converter_config.json"
        self.config_data = self.load_config()
        
        # --- 全局变量与保存状态 ---
        self.var_out_dir = tk.StringVar(value=self.config_data.get("output_dir", os.getcwd()))
        self.var_img_name = tk.StringVar(value=self.config_data.get("img_name", ""))
        self.var_array_name = tk.StringVar(value=self.config_data.get("audio_name", ""))

        # --- 字库 map 转换相关保存状态 ---
        self.var_map_output_path = tk.StringVar(value=self.config_data.get("map_output_path", ""))
        self.var_map_encoding = tk.StringVar(value=self.config_data.get("map_encoding", "自动识别"))
        self.var_map_include_ascii = tk.BooleanVar(value=self.config_data.get("map_include_ascii", True))
        self.var_map_sort_unicode = tk.BooleanVar(value=self.config_data.get("map_sort_unicode", True))
        self.var_map_keep_existing = tk.BooleanVar(value=self.config_data.get("map_keep_existing", True))

        # --- 内部组件变量 ---
        self.image_path = None
        self.original_image = None
        self.preview_image = None
        self.bg_color = (0, 0, 0)
        self.wav_path = None
        self.map_input_path = None
        self.last_map_output_text = ""

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
                "audio_name": self.var_array_name.get(),
                "map_output_path": self.var_map_output_path.get(),
                "map_encoding": self.var_map_encoding.get(),
                "map_include_ascii": self.var_map_include_ascii.get(),
                "map_sort_unicode": self.var_map_sort_unicode.get(),
                "map_keep_existing": self.var_map_keep_existing.get(),
            }
            with open(self.config_file, 'w', encoding='utf-8') as f:
                json.dump(config, f, ensure_ascii=False, indent=2)
        except:
            pass

    def setup_ui(self):
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill="both", expand=True, padx=10, pady=10)

        self.tab_image = ttk.Frame(self.notebook)
        self.tab_audio = ttk.Frame(self.notebook)
        self.tab_font_map = ttk.Frame(self.notebook)
        
        self.notebook.add(self.tab_image, text="📺 图片转 RGB565 (.bin)")
        self.notebook.add(self.tab_audio, text="🎵 音频转 C 数组 (.h)")
        self.notebook.add(self.tab_font_map, text="🔤 字库 Map 转 Unicode")

        self.build_image_tab()
        self.build_audio_tab()
        self.build_font_map_tab()
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
        if not self.original_image:
            return None
        try:
            cw, ch = self.var_width.get(), self.var_height.get()
        except tk.TclError:
            return None 

        fit = self.var_fit.get()
        canvas = Image.new("RGBA", (cw, ch), self.bg_color + (255,))
        img = self.original_image.copy()
        iw, ih = img.size

        if fit == "拉伸铺满":
            img = img.resize((cw, ch), Image.Resampling.LANCZOS)
        elif fit == "等比例缩放 (居中)":
            r = min(cw / iw, ch / ih)
            img = img.resize((max(1, int(iw * r)), max(1, int(ih * r))), Image.Resampling.LANCZOS)
            
        px, py = 0, 0
        if "居中" in fit:
            px, py = (cw - img.width) // 2, (ch - img.height) // 2

        canvas.paste(img, (px, py), mask=img)
        return canvas.convert("RGB")

    def update_preview(self):
        if not self.original_image:
            return
        try:
            processed = self.process_image()
            if processed:
                preview = processed.copy()
                preview.thumbnail((300, 300), Image.Resampling.LANCZOS)
                self.preview_image = ImageTk.PhotoImage(preview)
                self.lbl_preview.config(image=self.preview_image, text="", width=preview.width, height=preview.height)
        except:
            pass 

    def export_bin(self):
        if not self.original_image:
            return messagebox.showwarning("警告", "请先加载图片！")
            
        out_dir = self.var_out_dir.get()
        if not os.path.isdir(out_dir):
            return messagebox.showerror("错误", "输出目录不存在！")
        
        img_name = self.var_img_name.get().strip()
        if not img_name:
            img_name = "image_data"
        
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
            except Exception:
                self.lbl_wav_info.config(text="无法读取 WAV 参数格式", fg="red")

    def export_wav(self):
        if not self.wav_path:
            return messagebox.showwarning("警告", "请先加载 WAV 音频！")
        
        out_dir = self.var_out_dir.get()
        if not os.path.isdir(out_dir):
            return messagebox.showerror("错误", "输出目录不存在！")

        array_name = self.var_array_name.get().strip()
        if not array_name:
            array_name = "sound_data"

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

    # ==========================================
    #              字库 Map 转换模块
    # ==========================================
    def build_font_map_tab(self):
        parent = self.tab_font_map

        frame_file = tk.LabelFrame(parent, text="1. 选择原始字符文件 / map.txt", padx=10, pady=10)
        frame_file.pack(fill="x", padx=10, pady=10)
        tk.Button(frame_file, text="加载字符文件", command=self.load_map_file).pack(side="left", padx=5)
        self.lbl_map_path = tk.Label(frame_file, text="未选择任何 map 文件", fg="gray")
        self.lbl_map_path.pack(side="left", padx=5)

        frame_settings = tk.LabelFrame(parent, text="2. 转换设置", padx=10, pady=10)
        frame_settings.pack(fill="x", padx=10, pady=5)

        tk.Label(frame_settings, text="输入编码:").grid(row=0, column=0, sticky="e", pady=5)
        ttk.Combobox(
            frame_settings,
            textvariable=self.var_map_encoding,
            values=["自动识别", "UTF-8", "GBK/ANSI", "UTF-16"],
            state="readonly",
            width=16,
        ).grid(row=0, column=1, sticky="w", padx=5)

        tk.Checkbutton(
            frame_settings,
            text="自动加入 ASCII 可显示字符 32-126",
            variable=self.var_map_include_ascii,
        ).grid(row=1, column=1, sticky="w", padx=5, pady=2)

        tk.Checkbutton(
            frame_settings,
            text="按 Unicode 编码排序并去重",
            variable=self.var_map_sort_unicode,
        ).grid(row=2, column=1, sticky="w", padx=5, pady=2)

        tk.Checkbutton(
            frame_settings,
            text="保留原文件中已有的 bdfconv 行，例如 $6307、32-126",
            variable=self.var_map_keep_existing,
        ).grid(row=3, column=1, sticky="w", padx=5, pady=2)

        tk.Label(frame_settings, text="输出路径:").grid(row=4, column=0, sticky="e", pady=5)
        tk.Entry(frame_settings, textvariable=self.var_map_output_path, width=52).grid(row=4, column=1, sticky="we", padx=5)
        tk.Button(frame_settings, text="选择...", command=self.choose_map_output_path).grid(row=4, column=2, sticky="w", padx=5)
        frame_settings.columnconfigure(1, weight=1)

        frame_help = tk.LabelFrame(parent, text="3. 说明", padx=10, pady=8)
        frame_help.pack(fill="x", padx=10, pady=5)
        help_text = (
            "输入可以直接写：指令终端等待执行\n"
            "输出会变成 bdfconv 可识别的单行 map：32-126, $6307, $4EE4...\n"
            "注意：bdfconv 的 -M map 文件应使用逗号+空格分隔，不能一行一个字符。\n"
            "导出的 map.txt 可直接用于：bdfconv.exe -v -f 1 -M map.txt 字体.bdf -o 字体.h -n 字体名"
        )
        tk.Label(frame_help, text=help_text, justify="left", fg="#444444").pack(anchor="w")

        frame_preview = tk.LabelFrame(parent, text="4. 转换预览", padx=10, pady=5)
        frame_preview.pack(fill="both", expand=True, padx=10, pady=5)

        self.txt_map_preview = tk.Text(frame_preview, height=14, wrap="none")
        y_scroll = tk.Scrollbar(frame_preview, orient="vertical", command=self.txt_map_preview.yview)
        x_scroll = tk.Scrollbar(frame_preview, orient="horizontal", command=self.txt_map_preview.xview)
        self.txt_map_preview.configure(yscrollcommand=y_scroll.set, xscrollcommand=x_scroll.set)
        self.txt_map_preview.grid(row=0, column=0, sticky="nsew")
        y_scroll.grid(row=0, column=1, sticky="ns")
        x_scroll.grid(row=1, column=0, sticky="ew")
        frame_preview.rowconfigure(0, weight=1)
        frame_preview.columnconfigure(0, weight=1)

        btn_frame = tk.Frame(parent)
        btn_frame.pack(fill="x", padx=10, pady=10)
        tk.Button(btn_frame, text="预览转换结果", font=("Arial", 11, "bold"), command=self.preview_unicode_map).pack(side="left", fill="x", expand=True, padx=(0, 5), ipady=5)
        tk.Button(btn_frame, text="导出 Unicode map.txt", font=("Arial", 11, "bold"), bg="#6A5ACD", fg="white", command=self.export_unicode_map).pack(side="left", fill="x", expand=True, padx=(5, 0), ipady=5)

    def load_map_file(self):
        file_path = filedialog.askopenfilename(filetypes=[("Text / Map Files", "*.txt;*.map"), ("All Files", "*.*")])
        if file_path:
            self.map_input_path = file_path
            self.lbl_map_path.config(text=os.path.basename(file_path))

            # 如果还没有设置输出路径，则根据输入文件名自动生成一个 Unicode map 输出路径。
            if not self.var_map_output_path.get().strip():
                base = os.path.splitext(os.path.basename(file_path))[0]
                self.var_map_output_path.set(os.path.join(self.var_out_dir.get(), f"{base}_unicode_map.txt"))

            self.preview_unicode_map()

    def choose_map_output_path(self):
        initialdir = self.var_out_dir.get() if os.path.isdir(self.var_out_dir.get()) else os.getcwd()
        initialfile = "map_unicode.txt"
        current_path = self.var_map_output_path.get().strip()
        if current_path:
            initialdir = os.path.dirname(current_path) or initialdir
            initialfile = os.path.basename(current_path) or initialfile

        save_path = filedialog.asksaveasfilename(
            title="选择 Unicode map 输出路径",
            initialdir=initialdir,
            initialfile=initialfile,
            defaultextension=".txt",
            filetypes=[("Text Files", "*.txt"), ("Map Files", "*.map"), ("All Files", "*.*")],
        )
        if save_path:
            self.var_map_output_path.set(save_path)
            self.save_config()

    def read_map_source_text(self):
        if not self.map_input_path:
            raise ValueError("请先选择原始字符文件 / map.txt")

        selected_encoding = self.var_map_encoding.get()
        if selected_encoding == "UTF-8":
            encodings = ["utf-8-sig"]
        elif selected_encoding == "GBK/ANSI":
            encodings = ["gbk", "cp936"]
        elif selected_encoding == "UTF-16":
            encodings = ["utf-16"]
        else:
            # Windows 下很多“ANSI”中文 txt 是 GBK；UTF-8-SIG 能处理带 BOM 的 UTF-8。
            encodings = ["utf-8-sig", "utf-16", "gbk", "cp936", "big5"]

        last_error = None
        for enc in encodings:
            try:
                with open(self.map_input_path, "r", encoding=enc) as f:
                    return f.read(), enc
            except UnicodeDecodeError as e:
                last_error = e
            except Exception as e:
                last_error = e
                break

        raise ValueError(f"无法读取字符文件，请尝试手动选择 UTF-8 或 GBK/ANSI。错误：{last_error}")

    @staticmethod
    def is_existing_bdfconv_map_line(line):
        """识别已经是 bdfconv map 语法的行，避免把 $6307 误拆成普通字符。"""
        s = line.strip()
        if not s:
            return False
        # 支持：32、32-126、$6307、$4E00-$9FFF
        dec = r"\d+"
        hexx = r"\$[0-9A-Fa-f]{1,6}"
        pattern = rf"^({dec}|{hexx})(\s*-\s*({dec}|{hexx}))?$"
        return re.match(pattern, s) is not None

    def convert_text_to_unicode_map(self, source_text):
        unicode_points = set()
        existing_map_lines = []

        # 逐行处理：已有 bdfconv 行直接保留；普通文字按字符转 Unicode 码位。
        for line in source_text.splitlines():
            stripped = line.strip()
            if self.var_map_keep_existing.get() and self.is_existing_bdfconv_map_line(stripped):
                existing_map_lines.append(re.sub(r"\s+", "", stripped).upper())
                continue

            for ch in line:
                cp = ord(ch)

                # 跳过控制字符；普通空格由 32-126 覆盖，全角空格等非 ASCII 空白仍可保留。
                if cp in (9, 10, 13):
                    continue
                if cp == 32:
                    continue
                if cp <= 126:
                    # ASCII 部分由 32-126 统一覆盖；如果用户关闭该选项，则逐字加入。
                    if not self.var_map_include_ascii.get():
                        unicode_points.add(cp)
                    continue

                unicode_points.add(cp)

        if self.var_map_sort_unicode.get():
            codepoints = sorted(unicode_points)
        else:
            # Python set 无法保留原始顺序；这里重新按原文扫描生成稳定顺序。
            seen = set()
            codepoints = []
            for line in source_text.splitlines():
                stripped = line.strip()
                if self.var_map_keep_existing.get() and self.is_existing_bdfconv_map_line(stripped):
                    continue
                for ch in line:
                    cp = ord(ch)
                    if cp in (9, 10, 13, 32):
                        continue
                    if cp <= 126 and self.var_map_include_ascii.get():
                        continue
                    if cp not in seen:
                        seen.add(cp)
                        codepoints.append(cp)

        output_items = []
        if self.var_map_include_ascii.get():
            output_items.append("32-126")

        # bdfconv 的 -M 文件不是“一行一个字符”，而是一个 map 命令串；
        # 这里统一输出为：32-126, $6307, $4EE4 这种逗号+空格分隔的单行格式。
        used_items = set(output_items)
        for line in existing_map_lines:
            if line not in used_items:
                output_items.append(line)
                used_items.add(line)

        for cp in codepoints:
            item = f"${cp:04X}"
            if item not in used_items:
                output_items.append(item)
                used_items.add(item)

        stats = {
            "source_chars": len(source_text),
            "unicode_chars": len(codepoints),
            "existing_lines": len(existing_map_lines),
            "output_items": len(output_items),
        }
        return ", ".join(output_items) + "\n", stats

    def preview_unicode_map(self):
        if not self.map_input_path:
            return messagebox.showwarning("警告", "请先加载原始字符文件 / map.txt！")

        try:
            source_text, encoding_used = self.read_map_source_text()
            output_text, stats = self.convert_text_to_unicode_map(source_text)
            self.last_map_output_text = output_text

            lines = output_text.splitlines()
            preview_lines = lines[:300]
            if len(lines) > 300:
                preview_lines.append(f"... 预览仅显示前 300 行，完整输出共 {len(lines)} 行 ...")

            info = (
                f"读取编码: {encoding_used}\n"
                f"源文件字符数: {stats['source_chars']}\n"
                f"新转换 Unicode 字符数: {stats['unicode_chars']}\n"
                f"保留原有 bdfconv 项数: {stats['existing_lines']}\n"
                f"输出 map 项数: {stats['output_items']}\n"
                "----------------------------------------\n"
            )
            self.txt_map_preview.delete("1.0", tk.END)
            self.txt_map_preview.insert(tk.END, info + "\n".join(preview_lines))
            self.save_config()
        except Exception as e:
            messagebox.showerror("错误", f"预览失败:\n{e}")

    def export_unicode_map(self):
        if not self.map_input_path:
            return messagebox.showwarning("警告", "请先加载原始字符文件 / map.txt！")

        save_path = self.var_map_output_path.get().strip()
        if not save_path:
            base = os.path.splitext(os.path.basename(self.map_input_path))[0]
            save_path = os.path.join(self.var_out_dir.get(), f"{base}_unicode_map.txt")
            self.var_map_output_path.set(save_path)

        out_dir = os.path.dirname(save_path) or os.getcwd()
        if not os.path.isdir(out_dir):
            return messagebox.showerror("错误", "输出目录不存在！")

        try:
            source_text, encoding_used = self.read_map_source_text()
            output_text, stats = self.convert_text_to_unicode_map(source_text)

            # bdfconv map 文件建议保持纯 ASCII：$XXXX 与 32-126 都是 ASCII。
            with open(save_path, "w", encoding="ascii", newline="\n") as f:
                f.write(output_text)

            self.last_map_output_text = output_text
            self.save_config()
            self.preview_unicode_map()
            messagebox.showinfo(
                "成功",
                "Unicode map 已生成:\n"
                f"{save_path}\n\n"
                f"读取编码: {encoding_used}\n"
                f"输出 map 项数: {stats['output_items']}\n"
                f"新转换 Unicode 字符数: {stats['unicode_chars']}\n\n"
                "可用于 bdfconv 的 -M 参数。"
            )
        except Exception as e:
            messagebox.showerror("错误", f"导出失败:\n{e}")


if __name__ == "__main__":
    root = tk.Tk()
    app = ResourceConverterApp(root)
    root.mainloop()
