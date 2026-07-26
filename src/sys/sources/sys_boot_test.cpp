/*
【模块职责】编译期开启的隔离硬件诊断入口。测试模式会阻止正常 APP 启动，避免屏幕、网络和后台任务
干扰 I2C、串口时序或传感器数据。
【使用约束】默认所有测试均关闭；PRESCRIPT_TM6605_BOOT_TEST 和 PRESCRIPT_IMU_CAPTURE_TEST
不应同时启用。IMU 采集只在显式测试固件中高频输出 CSV，正常固件不会产生采样刷屏。
*/
#include "sys/sys_boot_test.h"

#include <Arduino.h>
#include <Wire.h>

#include "bsp/bsp_pins.h"
#include "bsp/bsp_tm6605.h"
#include "sys/sys_constants.h"
#include "sys/sys_motion.h"

#ifndef PRESCRIPT_TM6605_BOOT_TEST
#define PRESCRIPT_TM6605_BOOT_TEST 0
#endif

#ifndef PRESCRIPT_IMU_CAPTURE_TEST
#define PRESCRIPT_IMU_CAPTURE_TEST 0
#endif

namespace
{
    uint32_t s_tm6605_last_play_ms = 0;
    uint8_t s_tm6605_effect_index = 0;
    uint32_t s_imu_last_sequence = 0;
    uint8_t s_imu_capture_label = 0;
    uint32_t s_imu_capture_started_ms = 0;
    uint32_t s_imu_capture_duration_ms = 0;

    constexpr BSP::Tm6605::Effect TEST_EFFECTS[] = {
        BSP::Tm6605::Effect::SharpClick,
        BSP::Tm6605::Effect::LightBump,
        BSP::Tm6605::Effect::DoubleClick,
        BSP::Tm6605::Effect::MediumAlert,
    };

    struct ImuCaptureLabel
    {
        char command;
        uint8_t id;
        const char *name;
        uint32_t duration_ms;
    };

    /*
     * 六个静止方向用于反推出芯片轴与机身方向；其余动作同时覆盖目标手势和日常误触样本。
     * 标签只属于采集文件，不会直接成为最终动作阈值或 APP 协议。
     */
    constexpr ImuCaptureLabel IMU_CAPTURE_LABELS[] = {
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
        /*
         * 业力应用的两个候选敲击动作只负责采集原始波形，不在这里预设轴向、
         * 符号或力度阈值。C 为设备一条长边朝向敲击面，D 为反向长边朝向敲击面；
         * 具体识别特征必须根据真实 V4B 的正负样本离线分析后再写入 SysGesture。
         * 20 秒足够在一次串口会话中记录轻、中、重多组重复，同时保留动作前后的静止段。
         */
        {'C', 12, "业力长边A敲击", 20000},
        {'D', 13, "业力长边B反向敲击", 20000},
    };

    const char *EffectName(uint8_t effect)
    {
        switch (effect)
        {
        case BSP::Tm6605::Effect::SharpClick:
            return "锐利单击";
        case BSP::Tm6605::Effect::LightBump:
            return "轻触";
        case BSP::Tm6605::Effect::DoubleClick:
            return "双击";
        case BSP::Tm6605::Effect::MediumAlert:
            return "中等警报";
        default:
            return "未知效果";
        }
    }

    const char *I2CDeviceName(uint8_t address)
    {
        switch (address)
        {
        case 0x2C:
            return "QMC5883P";
        case BSP::Tm6605::DEFAULT_ADDRESS:
            return "TM6605";
        case 0x51:
            return "PCF8563";
        case 0x6A:
            return "LSM6DSL";
        default:
            return "未知设备";
        }
    }

    void ScanI2C()
    {
        uint8_t found = 0;
        Serial.printf("[I2C测试] 开始扫描：SCL=%d，SDA=%d。\n", BSP::Pins::I2C_SCL, BSP::Pins::I2C_SDA);

        for (uint8_t address = 0x08; address <= 0x77; ++address)
        {
            Wire1.beginTransmission(address);
            uint8_t err = Wire1.endTransmission();
            if (err == 0)
            {
                ++found;
                Serial.printf("[I2C测试] 发现地址 0x%02X：%s。\n", address, I2CDeviceName(address));
            }
            delay(2);
        }

        Serial.printf("[I2C测试] 扫描完成，共发现 %u 个设备。\n", found);
    }

    void PlayNextTm6605Effect(bool force)
    {
        const uint8_t effect = static_cast<uint8_t>(TEST_EFFECTS[s_tm6605_effect_index]);
        const uint16_t duration = BSP::Tm6605::EffectDurationMs(effect);

        Serial.printf("[TM6605测试] 地址=0x%02X，效果=%s，编号=%u，时长=%ums。\n",
                      BSP::Tm6605::Address(),
                      EffectName(effect),
                      effect,
                      duration);

        bool ok = BSP::Tm6605::PlayEffect(effect, force);
        Serial.println(ok ? "[TM6605测试] 播放成功。" : "[TM6605测试] 播放失败或设备忙。");

        s_tm6605_effect_index = (s_tm6605_effect_index + 1) % (sizeof(TEST_EFFECTS) / sizeof(TEST_EFFECTS[0]));
        s_tm6605_last_play_ms = millis();
    }

    void PrintImuCaptureHelp()
    {
        Serial.println("[IMU采集] 串口发送单个字符切换标签，发送 0 暂停记录：");
        for (const auto &label : IMU_CAPTURE_LABELS)
            Serial.printf("[IMU采集] %c = 标签%u，%s，自动记录 %lus。\n",
                          label.command,
                          label.id,
                          label.name,
                          (unsigned long)(label.duration_ms / 1000));
        Serial.println("[IMU采集] CSV 表头如下，建议把完整串口输出保存为 UTF-8 文本：");
        Serial.println("数据,序号,微秒,标签,ax_raw,ay_raw,az_raw,gx_raw,gy_raw,gz_raw,temp_raw");
    }

    void HandleImuCaptureCommand()
    {
        while (Serial.available() > 0)
        {
            char command = (char)Serial.read();
            if (command >= 'a' && command <= 'z')
                command = (char)(command - 'a' + 'A');

            if (command == '0')
            {
                s_imu_capture_label = 0;
                s_imu_capture_duration_ms = 0;
                Serial.println("[IMU采集] 已暂停记录，传感器仍保持采样。");
                continue;
            }

            for (const auto &label : IMU_CAPTURE_LABELS)
            {
                if (command != label.command)
                    continue;

                s_imu_capture_label = label.id;
                s_imu_capture_started_ms = millis();
                s_imu_capture_duration_ms = label.duration_ms;
                Serial.printf("[IMU采集] 开始标签%u：%s；将在 %lus 后自动暂停。\n",
                              label.id,
                              label.name,
                              (unsigned long)(label.duration_ms / 1000));
                break;
            }
        }
    }

    void SetupImuCapture()
    {
        Serial.println("\n=== LSM6DSL 动作数据采集 ===");
        Serial.println("[IMU采集] 正常 APP 已停用，本固件只运行运动采样和串口输出。");

        if (!SysMotion_Init())
        {
            Serial.println("[IMU采集-错误] LSM6DSL 尚未就绪；服务会继续低频尝试恢复。");
        }
        PrintImuCaptureHelp();
    }

    void LoopImuCapture()
    {
        HandleImuCaptureCommand();

        /*
         * 自动结束每个标签，避免 104 Hz 串口输出造成控制字符排队，进而把不同动作混入同一标签。
         * 到时只停止输出，SysMotion 仍持续更新，下一标签可以立即开始而不需要重新初始化 IMU。
         */
        if (s_imu_capture_label != 0 &&
            millis() - s_imu_capture_started_ms >= s_imu_capture_duration_ms)
        {
            Serial.printf("[IMU采集] 标签%u记录完成，已自动暂停。\n", s_imu_capture_label);
            s_imu_capture_label = 0;
            s_imu_capture_duration_ms = 0;
        }

        if (!SysMotion_Update() || s_imu_capture_label == 0)
        {
            delay(1);
            return;
        }

        SysMotionSample sample = {};
        if (!SysMotion_GetLatest(&sample) || sample.sequence == s_imu_last_sequence)
            return;
        s_imu_last_sequence = sample.sequence;

        /*
         * 高频采集只输出紧凑整数，确保 115200 波特率能覆盖约 104 Hz 样本。
         * 量程固定为 ±8 g、±2000 dps，离线分析分别乘 0.000244 和 0.070 即得到 g、dps。
         */
        Serial.printf("数据,%lu,%lu,%u,%d,%d,%d,%d,%d,%d,%d\n",
                      (unsigned long)sample.sequence,
                      (unsigned long)sample.timestamp_us,
                      s_imu_capture_label,
                      sample.ax_raw,
                      sample.ay_raw,
                      sample.az_raw,
                      sample.gx_raw,
                      sample.gy_raw,
                      sample.gz_raw,
                      sample.temperature_raw);
    }
}

namespace SysBootTest
{
    bool Enabled()
    {
        return PRESCRIPT_TM6605_BOOT_TEST != 0 || PRESCRIPT_IMU_CAPTURE_TEST != 0;
    }

    void Setup()
    {
        setCpuFrequencyMhz(PrescriptConst::CPU_RUNTIME_MHZ);
        Serial.begin(115200);
        delay(500);

        if (PRESCRIPT_IMU_CAPTURE_TEST != 0)
        {
            SetupImuCapture();
            return;
        }

        Serial.println("\n=== TM6605 启动测试 ===");
        Serial.println("[启动测试] 正常 APP 已停用。");

        Wire1.begin(BSP::Pins::I2C_SDA, BSP::Pins::I2C_SCL);
        Wire1.setClock(100000);
        Wire1.setTimeOut(20);

        ScanI2C();

        if (!BSP::Tm6605::Begin(Wire1, BSP::Tm6605::DEFAULT_ADDRESS))
        {
            Serial.println("[TM6605测试] 设备未就绪，不发送播放命令。");
            return;
        }

        Serial.println("[TM6605测试] 设备已就绪，开始发送首个效果。");
        PlayNextTm6605Effect(true);
    }

    void Loop()
    {
        if (PRESCRIPT_IMU_CAPTURE_TEST != 0)
        {
            LoopImuCapture();
            return;
        }

        if (!BSP::Tm6605::IsReady())
        {
            delay(1000);
            return;
        }

        if (millis() - s_tm6605_last_play_ms >= 2000)
            PlayNextTm6605Effect(false);

        delay(10);
    }
}
