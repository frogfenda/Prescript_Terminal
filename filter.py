import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import os
import json
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, filtfilt

class AudioFilterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("WAV 音频高通滤波器 (去除低频)")
        self.root.geometry("550x550")
        
        # --- 配置加载 ---
        self.config_file = "filter_config.json"
        self.config_data = self.load_config()
        
        self.var_out_dir = tk.StringVar(value=self.config_data.get("output_dir", os.getcwd()))
        self.var_out_name = tk.StringVar(value="")
        
        self.wav_path = None
        self.sample_rate = None
        self.audio_data = None

        self.setup_ui()

    def load_config(self):
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, 'r', encoding='utf-8') as f:
                    return json.load(f)
            except: pass
        return {}

    def save_config(self):
        try:
            with open(self.config_file, 'w', encoding='utf-8') as f:
                json.dump({"output_dir": self.var_out_dir.get()}, f)
        except: pass

    def setup_ui(self):
        # 1. 选择文件
        frame_file = tk.LabelFrame(self.root, text="1. 选择原始 WAV 音频", padx=10, pady=10)
        frame_file.pack(fill="x", padx=10, pady=10)
        
        tk.Button(frame_file, text="加载 .wav 文件", command=self.load_wav).pack(side="left", padx=5)
        self.lbl_file = tk.Label(frame_file, text="未选择文件", fg="gray")
        self.lbl_file.pack(side="left", padx=5)
        
        self.lbl_info = tk.Label(self.root, text="音频参数: 待加载", fg="blue")
        self.lbl_info.pack(fill="x", padx=20)

        # 2. 滤波设置
        frame_filter = tk.LabelFrame(self.root, text="2. 滤波参数设置", padx=10, pady=15)
        frame_filter.pack(fill="x", padx=10, pady=10)

        tk.Label(frame_filter, text="截止频率 (Cutoff Frequency):").grid(row=0, column=0, sticky="e", pady=5)
        self.var_cutoff = tk.IntVar(value=500)
        tk.Entry(frame_filter, textvariable=self.var_cutoff, width=8).grid(row=0, column=1, sticky="w", padx=5)
        tk.Label(frame_filter, text="Hz").grid(row=0, column=2, sticky="w")
        
        tk.Label(frame_filter, text="*说明: 低于此频率的声音将被削弱。小型喇叭建议设为 300~800 Hz", fg="gray", justify="left").grid(row=1, column=0, columnspan=3, sticky="w", pady=5)

        # 3. 输出设置
        frame_out = tk.LabelFrame(self.root, text="3. 输出设置 (将自动转为 16-bit 格式)", padx=10, pady=15)
        frame_out.pack(fill="x", padx=10, pady=10)

        tk.Label(frame_out, text="输出目录:").grid(row=0, column=0, sticky="e", pady=5)
        tk.Entry(frame_out, textvariable=self.var_out_dir, width=35).grid(row=0, column=1, padx=5, sticky="w")
        tk.Button(frame_out, text="浏览...", command=self.choose_out_dir).grid(row=0, column=2, sticky="w")

        tk.Label(frame_out, text="输出文件名 (不含后缀):").grid(row=1, column=0, sticky="e", pady=5)
        tk.Entry(frame_out, textvariable=self.var_out_name, width=25).grid(row=1, column=1, columnspan=2, sticky="w", padx=5)

        # 4. 执行按钮
        tk.Button(self.root, text="应用高通滤波并导出", font=("Arial", 12, "bold"), bg="#FF9800", fg="white", command=self.process_audio).pack(fill="x", padx=10, pady=20, ipady=5)

    def load_wav(self):
        path = filedialog.askopenfilename(filetypes=[("WAV Audio", "*.wav")])
        if path:
            try:
                rate, data = wavfile.read(path)
                self.wav_path = path
                self.sample_rate = rate
                self.audio_data = data
                
                self.lbl_file.config(text=os.path.basename(path))
                
                channels = 1 if len(data.shape) == 1 else data.shape[1]
                ch_str = "单声道" if channels == 1 else f"{channels}声道"
                self.lbl_info.config(text=f"✅ 已加载: {rate}Hz, {data.dtype}, {ch_str}", fg="green")
                
                if not self.var_out_name.get().strip():
                    base = os.path.splitext(os.path.basename(path))[0]
                    self.var_out_name.set(f"{base}_filtered")
            except Exception as e:
                messagebox.showerror("读取错误", f"无法读取该音频文件:\n{e}")

    def choose_out_dir(self):
        d = filedialog.askdirectory(initialdir=self.var_out_dir.get())
        if d:
            self.var_out_dir.set(d)
            self.save_config()

    def butter_highpass(self, cutoff, fs, order=5):
        nyq = 0.5 * fs
        normal_cutoff = cutoff / nyq
        b, a = butter(order, normal_cutoff, btype='high', analog=False)
        return b, a

    def process_audio(self):
        if self.audio_data is None:
            return messagebox.showwarning("警告", "请先加载 WAV 音频！")
            
        out_dir = self.var_out_dir.get()
        if not os.path.isdir(out_dir):
            return messagebox.showerror("错误", "输出目录不存在！")
            
        try:
            cutoff = self.var_cutoff.get()
            if cutoff <= 0 or cutoff >= (self.sample_rate / 2):
                return messagebox.showerror("错误", f"截止频率设置不合理！应在 1 ~ {int(self.sample_rate/2)-1} 之间。")
        except:
            return messagebox.showerror("错误", "截止频率必须是整数！")

        out_name = self.var_out_name.get().strip() or "filtered_audio"
        save_path = os.path.join(out_dir, f"{out_name}.wav")

        try:
            # 1. 生成滤波器参数
            b, a = self.butter_highpass(cutoff, self.sample_rate, order=5)
            
            # 2. 应用滤波 (filtfilt 可以保证相位不发生偏移)
            data = self.audio_data
            if len(data.shape) > 1:
                # 处理多声道
                filtered_data = np.zeros_like(data, dtype=np.float64)
                for i in range(data.shape[1]):
                    filtered_data[:, i] = filtfilt(b, a, data[:, i])
            else:
                # 处理单声道
                filtered_data = filtfilt(b, a, data)
                
            # 3. 强制转换回 16-bit PCM (防止溢出爆音)
            # 先限制范围在 -32768 到 32767
            filtered_data = np.clip(filtered_data, -32768, 32767)
            # 转换为 int16
            final_data = np.int16(filtered_data)
            
            # 4. 导出文件
            wavfile.write(save_path, self.sample_rate, final_data)
            
            self.save_config()
            messagebox.showinfo("成功", f"滤波完成！已导出为 16-bit 格式:\n{save_path}\n\n截止频率: {cutoff} Hz")
            
        except Exception as e:
            messagebox.showerror("处理失败", f"处理音频时发生错误:\n{e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = AudioFilterApp(root)
    root.mainloop()