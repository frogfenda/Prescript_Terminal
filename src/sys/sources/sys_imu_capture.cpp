/*
【模块职责】实现 IMU 脱线采集的交互状态机和 FATFS CSV 写入。
【数据契约】CSV 前十一列保持旧串口采集格式不变，末尾只追加会话编号和 fresh 标志，已有分析工具
仍可按原列读取；每次记录使用独立文件，避免一次异常覆盖之前的有效数据。
【时序约束】记录期间只把固定长度原始样本复制到 PSRAM，不刷新全屏、不写 FAT，也不格式化 CSV；
动作结束后才统一生成文本并落盘，避免 QSPI、Flash 和逐帧 vsnprintf 阻塞 104Hz 采样。
*/
#include "sys/sys_imu_capture.h"

#include <Arduino.h>
#include <FFat.h>
#include <stdarg.h>

#include "hal/hal.h"
#include "hal/hal_fat_storage.h"
#include "sys/sys_motion.h"

namespace
{
    constexpr char CAPTURE_ROOT[] = "/Resources/_capture";
    constexpr char CAPTURE_DIR[] = "/Resources/_capture/imu";
    constexpr uint32_t COUNTDOWN_MS = 3000;
    // 成功页至少保留1.5秒；期间吞掉大幅动作给机械旋钮留下的残余脉冲，避免下一帧立刻覆盖反馈。
    constexpr uint32_t RESULT_INPUT_GUARD_MS = 1500;
    // 最长20秒约产生2100个样本；384KiB固定记录池留有充分余量，并且只占用外部PSRAM。
    constexpr size_t CAPTURE_BUFFER_BYTES = 384U * 1024U;
    constexpr uint16_t MAX_SESSION_INDEX = 9999;

    enum CaptureFreshFlag : uint8_t
    {
        CaptureFreshAccel = 1U << 0,
        CaptureFreshGyro = 1U << 1,
        CaptureFreshTemperature = 1U << 2,
    };

    /**
     * 记录期使用的紧凑定长样本。这里只保存分析器真正需要的原始字段；物理量浮点值可根据 CSV
     * 元数据中的量程离线换算，因此不复制 SysMotionSample 内的 ImuSample 和温度浮点值。
     * 固定长度写入不做字符串扫描、整数转十进制或可变长度内存复制，使每帧工作量稳定。
     */
    struct ImuCaptureRecord
    {
        uint32_t sequence;
        uint32_t timestamp_us;
        int16_t ax_raw;
        int16_t ay_raw;
        int16_t az_raw;
        int16_t gx_raw;
        int16_t gy_raw;
        int16_t gz_raw;
        int16_t temperature_raw;
        uint8_t fresh_flags;
    };

    constexpr size_t CAPTURE_RECORD_CAPACITY = CAPTURE_BUFFER_BYTES / sizeof(ImuCaptureRecord);

    struct ImuCaptureLabel
    {
        char command;
        uint8_t id;
        const char *name;
        uint32_t duration_ms;
    };

    /*
     * 旧标签继续保留，便于把新动作与菜单、业力和普通持握数据放在同一套采集工具中回归。
     * E～J 只描述用户动作语义，不预设 LSM6DSL 的轴、符号或阈值；这些结论必须由脱线实测得出。
     */
    constexpr ImuCaptureLabel LABELS[] = {
        {'1', 1, "屏幕正面朝上静止", 4000},
        {'2', 2, "屏幕正面朝下静止", 4000},
        {'3', 3, "机身左侧朝下静止", 4000},
        {'4', 4, "机身右侧朝下静止", 4000},
        {'5', 5, "机身顶部朝下静止", 4000},
        {'6', 6, "机身底部朝下静止", 4000},
        {'7', 7, "预期向上滚动的摇动", 12000},
        {'8', 8, "预期向下滚动的摇动", 12000},
        {'9', 9, "换武器动作", 12000},
        {'A', 10, "正常手持和无意晃动", 20000},
        {'B', 11, "流体应用中的倾斜和转动", 15000},
        {'C', 12, "业力长边A敲击", 20000},
        {'D', 13, "业力长边B反向敲击", 20000},
        {'E', 14, "双蛇杖横斩", 15000},
        {'F', 15, "双蛇杖竖斩", 15000},
        {'G', 16, "双蛇杖斜向斩A", 15000},
        {'H', 17, "双蛇杖斜向斩B", 15000},
        {'I', 18, "双蛇杖突刺", 15000},
        {'J', 19, "双蛇杖上挑", 15000},
    };
    constexpr int LABEL_COUNT = sizeof(LABELS) / sizeof(LABELS[0]);

    enum class CaptureState : uint8_t
    {
        Idle,
        Countdown,
        Recording,
        Saving,
        Result,
        Error,
    };

    CaptureState s_state = CaptureState::Error;
    int s_selected_index = 0;
    uint32_t s_state_started_ms = 0;
    uint32_t s_record_started_ms = 0;
    uint32_t s_last_sequence = 0;
    uint32_t s_sample_count = 0;
    uint16_t s_session_index = 0;
    bool s_storage_ready = false;
    fs::File s_file;
    char s_file_path[96] = {};
    ImuCaptureRecord *s_capture_records = nullptr;
    SysMotionAcquisitionConfig s_acquisition_config = {};
    String s_status_detail;

    const ImuCaptureLabel &SelectedLabel()
    {
        return LABELS[s_selected_index];
    }

    void DrawCentered(const char *text, int y, HALFontRole role, uint16_t color)
    {
        const int width = HAL_Get_Text_Width_Font(text, role);
        HAL_Screen_ShowLine_Font((HAL_Get_Screen_Width() - width) / 2, y, text, role, color);
    }

    void DrawUi()
    {
        HAL_Sprite_Clear();
        DrawCentered("IMU 脱线采集", 5, HAL_FONT_TITLE, TFT_CYAN);

        const ImuCaptureLabel &label = SelectedLabel();
        char line[160];
        snprintf(line, sizeof(line), "%c / 标签%u  %s", label.command, label.id, label.name);
        DrawCentered(line, 35, HAL_FONT_BODY, TFT_WHITE);

        switch (s_state)
        {
        case CaptureState::Idle:
        case CaptureState::Result:
            if (s_state == CaptureState::Result)
            {
                DrawCentered(s_status_detail.c_str(), 67, HAL_FONT_BODY, TFT_GREEN);
            }
            else
            {
                DrawCentered("旋钮选择，主键开始", 67, HAL_FONT_BODY, TFT_GREEN);
            }
            DrawCentered("侧键可提前停止；重启按住侧键导出", 104, HAL_FONT_SMALL, TFT_YELLOW);
            break;

        case CaptureState::Countdown:
        {
            const uint32_t elapsed = millis() - s_state_started_ms;
            const uint32_t remaining = elapsed >= COUNTDOWN_MS ? 0 : COUNTDOWN_MS - elapsed;
            snprintf(line, sizeof(line), "%lu", (unsigned long)((remaining + 999) / 1000));
            DrawCentered(line, 66, HAL_FONT_TITLE, TFT_YELLOW);
            DrawCentered("请松开按键并准备动作", 104, HAL_FONT_SMALL, TFT_WHITE);
            break;
        }

        case CaptureState::Recording:
            snprintf(line, sizeof(line), "正在记录 %lus，请连续完成动作",
                     (unsigned long)(label.duration_ms / 1000));
            DrawCentered(line, 67, HAL_FONT_BODY, TFT_RED);
            DrawCentered("记录期间屏幕不刷新，以保证采样连续", 104, HAL_FONT_SMALL, TFT_YELLOW);
            break;

        case CaptureState::Saving:
            DrawCentered("动作记录完成，正在写入FAT", 67, HAL_FONT_BODY, TFT_YELLOW);
            DrawCentered("请勿断电", 104, HAL_FONT_SMALL, TFT_RED);
            break;

        case CaptureState::Error:
            DrawCentered("采集不可用", 67, HAL_FONT_BODY, TFT_RED);
            DrawCentered(s_status_detail.c_str(), 104, HAL_FONT_SMALL, TFT_YELLOW);
            break;
        }

        HAL_Screen_Update();
    }

    bool EnsureCaptureDirectory()
    {
        if (!FFat.exists("/Resources") && !FFat.mkdir("/Resources"))
            return false;
        if (!FFat.exists(CAPTURE_ROOT) && !FFat.mkdir(CAPTURE_ROOT))
            return false;
        if (!FFat.exists(CAPTURE_DIR) && !FFat.mkdir(CAPTURE_DIR))
            return false;
        return true;
    }

    /**
     * 仅在 Recording 已结束后调用。格式化和 FAT 写入故意留在保存阶段，不能从 UpdateRecording
     * 复用本接口，否则会重新把不确定耗时带回实时采样路径。
     */
    bool WriteCsvLine(const char *format, ...)
    {
        char row[224];
        va_list args;
        va_start(args, format);
        const int length = vsnprintf(row, sizeof(row), format, args);
        va_end(args);

        if (length <= 0 || static_cast<size_t>(length) >= sizeof(row))
            return false;
        if (!s_file)
            return false;
        return s_file.write(reinterpret_cast<const uint8_t *>(row), static_cast<size_t>(length)) ==
               static_cast<size_t>(length);
    }

    bool SaveCaptureCsv(const ImuCaptureLabel &label)
    {
        if (!s_file || !s_capture_records || s_sample_count == 0)
            return false;

        /*
         * 机器数据行前十一列严格兼容旧格式。追加字段全部放在末尾，避免已有脚本按固定索引解析时错位。
         * 元数据行不以“数据,”开头，因此旧分析器会自然忽略。
         */
        if (!WriteCsvLine("元数据,格式版本,2\n") ||
            !WriteCsvLine("元数据,标签,%u,%s\n", label.id, label.name) ||
            !WriteCsvLine("元数据,会话,%u\n", s_session_index) ||
            !WriteCsvLine("元数据,输出频率Hz,%u\n", s_acquisition_config.output_rate_hz) ||
            !WriteCsvLine("元数据,加速度量程g,%u\n", s_acquisition_config.accel_range_g) ||
            !WriteCsvLine("元数据,陀螺仪量程dps,%u\n", s_acquisition_config.gyro_range_dps) ||
            !WriteCsvLine("数据,序号,微秒,标签,ax_raw,ay_raw,az_raw,gx_raw,gy_raw,gz_raw,temp_raw,会话,accel_fresh,gyro_fresh,temp_fresh\n"))
        {
            return false;
        }

        for (uint32_t index = 0; index < s_sample_count; ++index)
        {
            const ImuCaptureRecord &record = s_capture_records[index];
            if (!WriteCsvLine("数据,%lu,%lu,%u,%d,%d,%d,%d,%d,%d,%d,%u,%u,%u,%u\n",
                              (unsigned long)record.sequence,
                              (unsigned long)record.timestamp_us,
                              label.id,
                              record.ax_raw, record.ay_raw, record.az_raw,
                              record.gx_raw, record.gy_raw, record.gz_raw,
                              record.temperature_raw,
                              s_session_index,
                              (record.fresh_flags & CaptureFreshAccel) ? 1U : 0U,
                              (record.fresh_flags & CaptureFreshGyro) ? 1U : 0U,
                              (record.fresh_flags & CaptureFreshTemperature) ? 1U : 0U))
            {
                return false;
            }
        }
        return true;
    }

    bool ChooseNewFile(const ImuCaptureLabel &label)
    {
        for (uint16_t session = 1; session <= MAX_SESSION_INDEX; ++session)
        {
            snprintf(s_file_path, sizeof(s_file_path), "%s/label%02u_session%04u.csv",
                     CAPTURE_DIR, label.id, session);
            if (!FFat.exists(s_file_path))
            {
                s_session_index = session;
                return true;
            }
        }
        return false;
    }

    void SetError(const char *detail)
    {
        if (s_file)
        {
            s_file.close();
        }
        s_sample_count = 0;
        s_state = CaptureState::Error;
        s_status_detail = detail ? detail : "未知错误";
        Serial.printf("[IMU采集-错误] %s。\n", s_status_detail.c_str());
        DrawUi();
    }

    bool BeginRecording()
    {
        const ImuCaptureLabel &label = SelectedLabel();
        if (!s_storage_ready || !ChooseNewFile(label))
        {
            SetError("无法分配新的采集文件");
            return false;
        }

        s_file = FFat.open(s_file_path, FILE_WRITE);
        if (!s_file || s_file.isDirectory())
        {
            SetError("无法创建采集文件");
            return false;
        }

        s_sample_count = 0;
        s_last_sequence = 0;
        s_record_started_ms = millis();

        if (!SysMotion_GetAcquisitionConfig(&s_acquisition_config))
        {
            SetError("无法读取IMU采集配置");
            return false;
        }

        s_state = CaptureState::Recording;
        Serial.printf("[IMU采集] 开始脱线记录：标签=%u，文件=%s，时长=%lus。\n",
                      label.id, s_file_path, (unsigned long)(label.duration_ms / 1000));
        DrawUi();
        return true;
    }

    void FinishRecording(bool stoppedByUser)
    {
        const uint8_t finishedLabel = SelectedLabel().id;
        const ImuCaptureLabel &label = SelectedLabel();
        s_state = CaptureState::Saving;
        DrawUi();
        const bool ok = SaveCaptureCsv(label);
        if (s_file)
        {
            s_file.flush();
            s_file.close();
        }

        if (!ok)
        {
            SetError("结束记录时写入FAT失败");
            return;
        }

        s_state = CaptureState::Result;
        s_state_started_ms = millis();
        char detail[128];
        snprintf(detail, sizeof(detail), "%s：%lu个样本 / 会话%u",
                 stoppedByUser ? "已提前停止" : "记录完成",
                 (unsigned long)s_sample_count, s_session_index);
        s_status_detail = detail;
        Serial.printf("[IMU采集] 标签%u记录完成：样本=%lu，文件=%s。\n",
                      finishedLabel, (unsigned long)s_sample_count, s_file_path);
        DrawUi();
    }

    void StartCountdown(int labelIndex)
    {
        if (!s_storage_ready || s_state == CaptureState::Recording)
            return;
        if (labelIndex >= 0 && labelIndex < LABEL_COUNT)
            s_selected_index = labelIndex;
        s_state = CaptureState::Countdown;
        s_state_started_ms = millis();
        s_status_detail = "";
        Serial.printf("[IMU采集] 标签%u将在3秒后开始记录。\n", SelectedLabel().id);
        DrawUi();
    }

    void StopOrCancel()
    {
        if (s_state == CaptureState::Recording)
        {
            FinishRecording(true);
        }
        else if (s_state == CaptureState::Countdown)
        {
            s_state = CaptureState::Idle;
            Serial.println("[IMU采集] 已取消本次倒计时。");
            DrawUi();
        }
    }

    void HandleIdleInput()
    {
        const int delta = HAL_Get_Knob_Delta();
        if (delta != 0)
        {
            int selected = (s_selected_index + delta) % LABEL_COUNT;
            if (selected < 0)
                selected += LABEL_COUNT;
            s_selected_index = selected;
            s_state = CaptureState::Idle;
            s_status_detail = "";
            DrawUi();
        }

        if (HAL_Get_Btn_Main_Event() == BTN_SHORT)
            StartCountdown(s_selected_index);
        if (HAL_Get_Btn2_Event() == BTN_SHORT)
            StopOrCancel();
    }

    void HoldResultFeedback()
    {
        /*
         * 设备进行大幅斩击时，机械旋钮可能因惯性或握持碰撞在中断计数器里留下增量。
         * 旧逻辑把 Result 与 Idle 放在同一输入分支，成功页画完后的下一帧就会消费该增量、
         * 清空成功文案并重画空闲页。文件已经正确关闭，所以表现为“FAT成功但没有反馈”。
         * 这里在最短展示期内持续读取并丢弃残余旋钮量，同时推进两个按键引擎；展示期结束后，
         * 只有用户新产生的完整输入才会选择标签或开始下一次记录。
         */
        (void)SysMotion_Update();
        (void)HAL_Get_Knob_Delta();
        (void)HAL_Get_Btn_Main_Event();
        (void)HAL_Get_Btn2_Event();
        delay(1);
    }

    void UpdateCountdown()
    {
        static uint32_t lastShownSecond = UINT32_MAX;
        const uint32_t elapsed = millis() - s_state_started_ms;
        const uint32_t remainingSecond = elapsed >= COUNTDOWN_MS ? 0 : (COUNTDOWN_MS - elapsed + 999) / 1000;
        if (remainingSecond != lastShownSecond)
        {
            lastShownSecond = remainingSecond;
            DrawUi();
        }
        (void)HAL_Get_Btn_Main_Event();
        if (HAL_Get_Btn2_Event() == BTN_SHORT)
        {
            StopOrCancel();
            return;
        }
        if (elapsed >= COUNTDOWN_MS)
        {
            lastShownSecond = UINT32_MAX;
            (void)BeginRecording();
        }
    }

    void UpdateRecording()
    {
        (void)HAL_Get_Btn_Main_Event();
        if (HAL_Get_Btn2_Event() == BTN_SHORT)
        {
            FinishRecording(true);
            return;
        }

        const ImuCaptureLabel &label = SelectedLabel();
        if (millis() - s_record_started_ms >= label.duration_ms)
        {
            FinishRecording(false);
            return;
        }

        if (!SysMotion_Update())
        {
            delay(1);
            return;
        }

        SysMotionSample sample = {};
        if (!SysMotion_GetLatest(&sample) || sample.sequence == s_last_sequence)
            return;
        s_last_sequence = sample.sequence;

        if (!s_capture_records || s_sample_count >= CAPTURE_RECORD_CAPACITY)
        {
            SetError("定长采样缓冲已满");
            return;
        }

        ImuCaptureRecord &record = s_capture_records[s_sample_count];
        record.sequence = sample.sequence;
        record.timestamp_us = sample.timestamp_us;
        record.ax_raw = sample.ax_raw;
        record.ay_raw = sample.ay_raw;
        record.az_raw = sample.az_raw;
        record.gx_raw = sample.gx_raw;
        record.gy_raw = sample.gy_raw;
        record.gz_raw = sample.gz_raw;
        record.temperature_raw = sample.temperature_raw;
        record.fresh_flags = (sample.accel_fresh ? CaptureFreshAccel : 0U) |
                             (sample.gyro_fresh ? CaptureFreshGyro : 0U) |
                             (sample.temperature_fresh ? CaptureFreshTemperature : 0U);
        ++s_sample_count;

    }
}

void SysImuCapture::Setup()
{
    Serial.println("\n=== LSM6DSL 脱线动作数据采集 ===");
    Serial.println("[IMU采集] 正常APP已停用；旋钮选标签，主键开始，侧键提前停止。");

    HAL_Init();
    s_capture_records = static_cast<ImuCaptureRecord *>(ps_malloc(CAPTURE_BUFFER_BYTES));
    if (!s_capture_records)
    {
        s_status_detail = "PSRAM采集缓冲申请失败";
        s_state = CaptureState::Error;
        Serial.printf("[IMU采集-错误] PSRAM采集缓冲申请失败：需要=%u字节。\n",
                      (unsigned)CAPTURE_BUFFER_BYTES);
        DrawUi();
        return;
    }

    s_storage_ready = HAL::FatStorage::MountForEsp() && EnsureCaptureDirectory();
    if (!s_storage_ready)
    {
        s_status_detail = "FATFS挂载或目录创建失败";
        s_state = CaptureState::Error;
        Serial.println("[IMU采集-错误] FATFS挂载或目录创建失败；不会自动格式化。");
        DrawUi();
        return;
    }

    if (!SysMotion_Init())
        Serial.println("[IMU采集-警告] LSM6DSL尚未就绪，采样服务会继续尝试恢复。");

    s_state = CaptureState::Idle;
    s_status_detail = "";
    DrawUi();

    Serial.println("[IMU采集] 纯脱线记录不启动USB；请使用旋钮和实体按键操作。");
    for (const auto &label : LABELS)
        Serial.printf("[IMU采集] %c=标签%u，%s，%lus。\n",
                      label.command, label.id, label.name,
                      (unsigned long)(label.duration_ms / 1000));
}

void SysImuCapture::Loop()
{
    switch (s_state)
    {
    case CaptureState::Idle:
        /* 空闲期间仍推进 SysMotion，使离线设备可以按原服务策略恢复，但不保存任何样本。 */
        (void)SysMotion_Update();
        HandleIdleInput();
        break;
    case CaptureState::Result:
        if (millis() - s_state_started_ms < RESULT_INPUT_GUARD_MS)
            HoldResultFeedback();
        else
        {
            (void)SysMotion_Update();
            HandleIdleInput();
        }
        break;
    case CaptureState::Countdown:
        (void)SysMotion_Update();
        UpdateCountdown();
        break;
    case CaptureState::Recording:
        UpdateRecording();
        break;
    case CaptureState::Saving:
        // 保存过程在 FinishRecording 内同步完成，本分支只防御未来状态扩展时的意外重入。
        delay(1);
        break;
    case CaptureState::Error:
        (void)HAL_Get_Btn_Main_Event();
        (void)HAL_Get_Btn2_Event();
        delay(10);
        break;
    }
}
