import os
import time
import threading
import numpy as np
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

from pydub import AudioSegment

# 1. 自动处理 Python 3.13+ 兼容性与 FFmpeg 路径
try:
    import audioop_lts
except ImportError:
    pass

try:
    import static_ffmpeg
    static_ffmpeg.add_paths()
except ImportError:
    pass

# 2. 检查 Pygame 支持
try:
    import pygame
    HAS_PYGAME = True
except ImportError:
    HAS_PYGAME = False


class AudioConverterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("MP3 转 WAV 批量转换与可视化剪辑工具")
        self.root.geometry("850x830")
        self.root.resizable(False, False)

        if HAS_PYGAME:
            pygame.mixer.init()

        # 数据存储与状态变量
        self.file_list = []
        self.current_selected_idx = None
        self.dragging_handle = None  # 当前拖拽的游标 ('start' 或 'end')
        
        # 试听播放与进度游标变量
        self.is_playing = False
        self.play_start_time = 0.0
        self.preview_temp_path = os.path.join(os.getcwd(), "_temp_preview.wav")

        self._build_ui()

    def _build_ui(self):
        # ================= 1. 文件选择区域 =================
        file_frame = ttk.LabelFrame(self.root, text="文件列表 (点击文件进行波形查看与剪辑)", padding=10)
        file_frame.pack(fill="x", padx=10, pady=5)

        self.listbox = tk.Listbox(file_frame, selectmode=tk.SINGLE, height=5)
        scrollbar = ttk.Scrollbar(file_frame, orient="vertical", command=self.listbox.yview)
        self.listbox.configure(yscrollcommand=scrollbar.set)
        
        self.listbox.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.listbox.bind("<<ListboxSelect>>", self.on_file_select)

        btn_file_frame = ttk.Frame(file_frame)
        btn_file_frame.pack(fill="x", pady=(5, 0))

        ttk.Button(btn_file_frame, text="添加 MP3", command=self.add_files).pack(side="left", padx=3)
        ttk.Button(btn_file_frame, text="移除选中", command=self.remove_file).pack(side="left", padx=3)
        ttk.Button(btn_file_frame, text="清空列表", command=self.clear_files).pack(side="left", padx=3)
        ttk.Button(btn_file_frame, text="修改输出文件名", command=self.rename_selected_file).pack(side="right", padx=3)

        # ================= 2. 可视化剪辑与波形图区域 =================
        clip_frame = ttk.LabelFrame(self.root, text="波形图与剪辑控制", padding=10)
        clip_frame.pack(fill="both", expand=True, padx=10, pady=5)

        # 状态标签（显示当前剪辑是否应用）
        self.app_status_label = ttk.Label(
            clip_frame, 
            text="剪辑状态：[未加载文件]", 
            font=("Microsoft YaHei", 10, "bold"),
            foreground="gray"
        )
        self.app_status_label.pack(side="top", anchor="w", pady=(0, 5))

        # Matplotlib 绘图画布
        self.fig = Figure(figsize=(8, 2.2), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.fig.tight_layout()

        self.canvas = FigureCanvasTkAgg(self.fig, master=clip_frame)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)

        # 绑定鼠标拖拽游标事件
        self.canvas.mpl_connect("button_press_event", self.on_mouse_press)
        self.canvas.mpl_connect("motion_notify_event", self.on_mouse_drag)
        self.canvas.mpl_connect("button_release_event", self.on_mouse_release)

        # 时间输入框与播放控制
        ctrl_frame = ttk.Frame(clip_frame)
        ctrl_frame.pack(fill="x", pady=5)

        ttk.Label(ctrl_frame, text="起点 (s):").pack(side="left", padx=(5, 2))
        self.start_time_var = tk.StringVar(value="0.00")
        self.start_entry = ttk.Entry(ctrl_frame, textvariable=self.start_time_var, width=8)
        self.start_entry.pack(side="left", padx=2)
        self.start_entry.bind("<KeyRelease>", lambda e: self.set_clip_applied_status(False))

        ttk.Label(ctrl_frame, text="终点 (s):").pack(side="left", padx=(10, 2))
        self.end_time_var = tk.StringVar(value="0.00")
        self.end_entry = ttk.Entry(ctrl_frame, textvariable=self.end_time_var, width=8)
        self.end_entry.pack(side="left", padx=2)
        self.end_entry.bind("<KeyRelease>", lambda e: self.set_clip_applied_status(False))

        ttk.Button(ctrl_frame, text="应用当前剪辑", command=self.apply_clip_times).pack(side="left", padx=10)

        ttk.Separator(ctrl_frame, orient="vertical").pack(side="left", fill="y", padx=10)
        self.play_btn = ttk.Button(ctrl_frame, text="▶ 播放剪辑片段", command=self.play_preview)
        self.play_btn.pack(side="left", padx=3)
        self.stop_btn = ttk.Button(ctrl_frame, text="■ 停止播放", command=self.stop_preview)
        self.stop_btn.pack(side="left", padx=3)

        self.clip_info_label = ttk.Label(ctrl_frame, text="总时长: 0.0s", foreground="blue")
        self.clip_info_label.pack(side="right", padx=5)

        # ================= 3. 参数配置区域 =================
        param_frame = ttk.LabelFrame(self.root, text="音频输出配置", padding=10)
        param_frame.pack(fill="x", padx=10, pady=5)

        ttk.Label(param_frame, text="采样率:").grid(row=0, column=0, sticky="w", padx=5, pady=3)
        self.sr_combo = ttk.Combobox(param_frame, values=["8000", "16000", "22050", "32000", "44100", "48000"], width=10)
        self.sr_combo.set("44100")
        self.sr_combo.grid(row=0, column=1, padx=5, pady=3)

        ttk.Label(param_frame, text="声道:").grid(row=0, column=2, sticky="w", padx=5, pady=3)
        self.channel_combo = ttk.Combobox(param_frame, values=["单声道 (1)", "双声道/立体声 (2)"], width=15)
        self.channel_combo.set("双声道/立体声 (2)")
        self.channel_combo.grid(row=0, column=3, padx=5, pady=3)

        ttk.Label(param_frame, text="位深:").grid(row=0, column=4, sticky="w", padx=5, pady=3)
        self.bit_combo = ttk.Combobox(param_frame, values=["8-bit", "16-bit", "24-bit", "32-bit"], width=10)
        self.bit_combo.set("16-bit")
        self.bit_combo.grid(row=0, column=5, padx=5, pady=3)

        ttk.Label(param_frame, text="输出目录:").grid(row=1, column=0, sticky="w", padx=5, pady=3)
        self.out_dir_var = tk.StringVar(value=os.path.abspath("./output"))
        ttk.Entry(param_frame, textvariable=self.out_dir_var, width=35).grid(row=1, column=1, columnspan=3, sticky="ew", padx=5, pady=3)
        ttk.Button(param_frame, text="选择目录", command=self.select_output_dir).grid(row=1, column=4, columnspan=2, padx=5, pady=3)

        # ================= 4. 操作与状态区域 =================
        action_frame = ttk.Frame(self.root, padding=10)
        action_frame.pack(fill="x", padx=10, pady=5)

        self.start_btn = ttk.Button(action_frame, text="开始按剪辑区域批量转换", command=self.start_conversion)
        self.start_btn.pack(side="top", fill="x", pady=5)

        self.status_label = ttk.Label(action_frame, text="就绪：请添加文件", foreground="gray")
        self.status_label.pack(side="left")

    # ================= 播放与进度游标更新 =================

    def update_play_cursor(self):
        """轮询更新黑色播放进度游标"""
        if not self.is_playing or self.current_selected_idx is None:
            if hasattr(self, "line_play"):
                self.line_play.set_visible(False)
                self.canvas.draw_idle()
            return

        item = self.file_list[self.current_selected_idx]
        
        # 判断音频是否播放完毕
        if HAS_PYGAME and not pygame.mixer.music.get_busy():
            self.stop_preview()
            return

        elapsed = time.time() - self.play_start_time
        current_pos = item["start_sec"] + elapsed

        if current_pos >= item["end_sec"]:
            self.stop_preview()
            return

        # 更新黑色游标位置
        self.line_play.set_xdata([current_pos, current_pos])
        self.line_play.set_visible(True)
        self.canvas.draw_idle()

        # 每 50ms 刷新一次游标位置
        self.root.after(50, self.update_play_cursor)

    def stop_preview(self):
        """停止播放并彻底释放文件锁定"""
        self.is_playing = False
        if HAS_PYGAME:
            if pygame.mixer.music.get_busy():
                pygame.mixer.music.stop()
            # 释放对 WAV 文件的锁定占用
            try:
                pygame.mixer.music.unload()
            except AttributeError:
                pass

        if hasattr(self, "line_play"):
            self.line_play.set_visible(False)
            self.canvas.draw_idle()

        self.status_label.config(text="已停止播放", foreground="gray")

    def play_preview(self):
        """导出当前剪辑的临时文件并播放"""
        if self.current_selected_idx is None:
            return
        item = self.file_list[self.current_selected_idx]

        # 先完全停止当前播放并释放占用
        self.stop_preview()
        self.status_label.config(text="正在生成试听音频...", foreground="blue")

        def _prepare_and_play():
            try:
                # 安全清理旧临时文件，如果被占用则自动生成新文件名
                if os.path.exists(self.preview_temp_path):
                    try:
                        os.remove(self.preview_temp_path)
                    except PermissionError:
                        self.preview_temp_path = os.path.join(
                            os.getcwd(), f"_temp_preview_{int(time.time()*1000)}.wav"
                        )

                audio = AudioSegment.from_file(item["path"], format="mp3")
                start_ms = int(item["start_sec"] * 1000)
                end_ms = int(item["end_sec"] * 1000)
                clip = audio[start_ms:end_ms]

                clip.export(self.preview_temp_path, format="wav")

                if HAS_PYGAME:
                    pygame.mixer.music.load(self.preview_temp_path)
                    pygame.mixer.music.play()

                self.is_playing = True
                self.play_start_time = time.time()

                self.root.after(0, lambda: self.status_label.config(text="▶ 正在播放试听...", foreground="green"))
                self.root.after(0, self.update_play_cursor)
            except Exception as e:
                print("试听生成失败:", e)
                self.root.after(0, lambda: self.status_label.config(text=f"试听失败: {e}", foreground="red"))

        threading.Thread(target=_prepare_and_play, daemon=True).start()

    # ================= 游标拖拽与剪辑状态 =================

    def set_clip_applied_status(self, is_applied):
        if self.current_selected_idx is None:
            self.app_status_label.config(text="剪辑状态：[未选择文件]", foreground="gray")
            return

        item = self.file_list[self.current_selected_idx]
        item["is_applied"] = is_applied

        if is_applied:
            self.app_status_label.config(
                text=f"剪辑状态：[已应用]  (片段: {item['start_sec']:.2f}s -> {item['end_sec']:.2f}s)",
                foreground="green"
            )
        else:
            self.app_status_label.config(
                text=f"剪辑状态：[未应用/修改中]  (请点击“应用当前剪辑”)",
                foreground="#d9534f"
            )

    def on_mouse_press(self, event):
        """仅在精准抓取游标线时响应拖拽"""
        if event.inaxes != self.ax or self.current_selected_idx is None:
            return
        
        item = self.file_list[self.current_selected_idx]
        x_click = event.xdata
        if x_click is None:
            return

        x_range = self.ax.get_xlim()[1] - self.ax.get_xlim()[0]
        tolerance = x_range * 0.03  # 3% 的拖拽捕捉半径

        dist_start = abs(x_click - item["start_sec"])
        dist_end = abs(x_click - item["end_sec"])

        if dist_start < tolerance and dist_start <= dist_end:
            self.dragging_handle = 'start'
        elif dist_end < tolerance:
            self.dragging_handle = 'end'
        else:
            self.dragging_handle = None

    def on_mouse_drag(self, event):
        """鼠标拖拽游标"""
        if not self.dragging_handle or event.inaxes != self.ax or self.current_selected_idx is None:
            return
        
        item = self.file_list[self.current_selected_idx]
        x_drag = max(0.0, min(event.xdata, item["duration"]))

        if self.dragging_handle == 'start':
            item["start_sec"] = min(x_drag, item["end_sec"] - 0.1)
            self.start_time_var.set(f"{item['start_sec']:.2f}")
        elif self.dragging_handle == 'end':
            item["end_sec"] = max(x_drag, item["start_sec"] + 0.1)
            self.end_time_var.set(f"{item['end_sec']:.2f}")

        self.set_clip_applied_status(False)
        self.refresh_current_waveform()

    def on_mouse_release(self, event):
        self.dragging_handle = None

    def apply_clip_times(self):
        if self.current_selected_idx is None:
            return
        item = self.file_list[self.current_selected_idx]
        
        try:
            start_val = float(self.start_time_var.get())
            end_val = float(self.end_time_var.get())
        except ValueError:
            messagebox.showwarning("警告", "请输入有效数字！")
            return

        if start_val < 0:
            start_val = 0.0
        if end_val > item["duration"] or end_val == 0.0:
            end_val = item["duration"]
        if start_val >= end_val:
            messagebox.showwarning("警告", "起始时间必须小于结束时间！")
            return

        item["start_sec"] = start_val
        item["end_sec"] = end_val
        
        self.refresh_current_waveform()
        self.refresh_listbox()
        self.set_clip_applied_status(True)

    # ================= 波形渲染与批量转换 =================

    def add_files(self):
        files = filedialog.askopenfilenames(
            title="选择 MP3 文件",
            filetypes=[("MP3 音频文件", "*.mp3"), ("所有文件", "*.*")]
        )
        for f in files:
            if not any(item['path'] == f for item in self.file_list):
                base_name = os.path.splitext(os.path.basename(f))[0] + ".wav"
                self.file_list.append({
                    "path": f,
                    "out_name": base_name,
                    "status": "未转换",
                    "start_sec": 0.0,
                    "end_sec": 0.0,
                    "duration": 0.0,
                    "is_applied": True
                })
        self.refresh_listbox()
        if self.file_list and self.current_selected_idx is None:
            self.listbox.selection_set(0)
            self.on_file_select(None)

    def on_file_select(self, event):
        sel = self.listbox.curselection()
        if not sel:
            return
        self.stop_preview()
        idx = sel[0]
        self.current_selected_idx = idx
        item = self.file_list[idx]

        self.status_label.config(text=f"正在加载波形数据: {os.path.basename(item['path'])}...", foreground="blue")
        threading.Thread(target=self._load_waveform_data, args=(item, idx), daemon=True).start()

    def _load_waveform_data(self, item, idx):
        try:
            audio = AudioSegment.from_file(item["path"], format="mp3")
            duration = len(audio) / 1000.0

            item["duration"] = duration
            if item["end_sec"] == 0.0 or item["end_sec"] > duration:
                item["end_sec"] = duration

            samples = np.array(audio.get_array_of_samples())
            if audio.channels == 2:
                samples = samples[::2]

            downsample_factor = max(1, len(samples) // 2000)
            samples_ds = samples[::downsample_factor]
            times_ds = np.linspace(0, duration, len(samples_ds))

            self.root.after(0, lambda: self._update_waveform_ui(item, times_ds, samples_ds))
        except Exception as e:
            print("波形加载失败:", e)
            self.root.after(0, lambda: self.status_label.config(text="波形加载失败", foreground="red"))

    def _update_waveform_ui(self, item, times, samples):
        self.ax.clear()
        self.ax.plot(times, samples, color="#2b5c8f", alpha=0.6, linewidth=0.8)
        self.ax.set_xlim(0, item["duration"])
        self.ax.set_yticks([])
        self.ax.set_xlabel("时间 (秒)", fontsize=9)

        # 绿色起点游标 与 红色终点游标
        self.line_start = self.ax.axvline(item["start_sec"], color="#28a745", linestyle="-", linewidth=2)
        self.line_end = self.ax.axvline(item["end_sec"], color="#dc3545", linestyle="-", linewidth=2)
        
        # 黑色播放进度游标 (默认隐藏)
        self.line_play = self.ax.axvline(item["start_sec"], color="#71e3ff", linestyle="--", linewidth=2.5, visible=False)

        [p.remove() for p in self.ax.patches]
        self.ax.axvspan(item["start_sec"], item["end_sec"], color="#ffc107", alpha=0.2)

        self.canvas.draw()

        self.start_time_var.set(f"{item['start_sec']:.2f}")
        self.end_time_var.set(f"{item['end_sec']:.2f}")
        self.clip_info_label.config(text=f"总时长: {item['duration']:.2f}s")
        self.set_clip_applied_status(item.get("is_applied", True))
        self.status_label.config(text="就绪", foreground="gray")

    def refresh_current_waveform(self):
        if self.current_selected_idx is None:
            return
        item = self.file_list[self.current_selected_idx]
        
        self.line_start.set_xdata([item["start_sec"], item["start_sec"]])
        self.line_end.set_xdata([item["end_sec"], item["end_sec"]])
        
        [p.remove() for p in self.ax.patches]
        self.ax.axvspan(item["start_sec"], item["end_sec"], color="#ffc107", alpha=0.2)
        
        self.canvas.draw_idle()

    def remove_file(self):
        self.stop_preview()
        sel = self.listbox.curselection()
        if sel:
            idx = sel[0]
            del self.file_list[idx]
            self.current_selected_idx = None
            self.ax.clear()
            self.canvas.draw()
            self.set_clip_applied_status(False)
            self.refresh_listbox()

    def clear_files(self):
        self.stop_preview()
        self.file_list.clear()
        self.current_selected_idx = None
        self.ax.clear()
        self.canvas.draw()
        self.set_clip_applied_status(False)
        self.refresh_listbox()

    def rename_selected_file(self):
        sel = self.listbox.curselection()
        if not sel:
            messagebox.showwarning("提示", "请先选中一个文件！")
            return
        idx = sel[0]
        current_name = self.file_list[idx]["out_name"]

        dialog = tk.Toplevel(self.root)
        dialog.title("修改输出文件名")
        dialog.geometry("300x120")
        
        ttk.Label(dialog, text="请输入新的文件名 (.wav):").pack(pady=10)
        entry = ttk.Entry(dialog, width=30)
        entry.insert(0, current_name)
        entry.pack(pady=5)

        def save():
            new_name = entry.get().strip()
            if new_name:
                if not new_name.lower().endswith(".wav"):
                    new_name += ".wav"
                self.file_list[idx]["out_name"] = new_name
                self.refresh_listbox()
                dialog.destroy()

        ttk.Button(dialog, text="确定", command=save).pack(pady=5)

    def select_output_dir(self):
        path = filedialog.askdirectory(title="选择保存目录")
        if path:
            self.out_dir_var.set(path)

    def refresh_listbox(self):
        self.listbox.delete(0, tk.END)
        for item in self.file_list:
            clip_str = f"[{item['start_sec']:.1f}s - {item['end_sec']:.1f}s]" if item["duration"] > 0 else ""
            display_str = f"[{item['status']}] {os.path.basename(item['path'])} {clip_str} -> {item['out_name']}"
            self.listbox.insert(tk.END, display_str)

    def start_conversion(self):
        self.stop_preview()
        if not self.file_list:
            messagebox.showwarning("警告", "请先添加 MP3 文件！")
            return

        out_dir = self.out_dir_var.get()
        if not os.path.exists(out_dir):
            os.makedirs(out_dir, exist_ok=True)

        self.start_btn.config(state="disabled")
        threading.Thread(target=self._run_conversion, daemon=True).start()

    def _run_conversion(self):
        sample_rate = int(self.sr_combo.get())
        channels = 1 if "1" in self.channel_combo.get() else 2
        
        bit_map = {"8-bit": 1, "16-bit": 2, "24-bit": 3, "32-bit": 4}
        sample_width = bit_map.get(self.bit_combo.get(), 2)

        out_dir = self.out_dir_var.get()

        for item in self.file_list:
            item["status"] = "转换中..."
            self.root.after(0, self.refresh_listbox)

            try:
                audio = AudioSegment.from_file(item["path"], format="mp3")
                start_ms = int(item["start_sec"] * 1000)
                end_ms = int(item["end_sec"] * 1000) if item["end_sec"] > 0 else len(audio)
                
                audio_clipped = audio[start_ms:end_ms]

                audio_clipped = audio_clipped.set_frame_rate(sample_rate)
                audio_clipped = audio_clipped.set_channels(channels)
                audio_clipped = audio_clipped.set_sample_width(sample_width)

                save_path = os.path.join(out_dir, item["out_name"])
                audio_clipped.export(save_path, format="wav")
                
                item["status"] = "已完成"
            except Exception as e:
                item["status"] = "失败"
                print(f"转换错误 [{item['path']}]: {e}")

            self.root.after(0, self.refresh_listbox)

        self.root.after(0, lambda: self.status_label.config(text="全部任务转换完成！", foreground="green"))
        self.root.after(0, lambda: messagebox.showinfo("完成", "所有文件转换及剪辑完成！"))
        self.root.after(0, lambda: self.start_btn.config(state="normal"))


if __name__ == "__main__":
    root = tk.Tk()
    app = AudioConverterApp(root)
    root.mainloop()