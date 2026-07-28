/*
【模块职责】编译期开启的隔离硬件诊断入口。测试模式会阻止正常 APP 启动，避免屏幕、网络和后台任务
干扰 I2C、串口时序或传感器数据。
【使用约束】默认所有测试均关闭；PRESCRIPT_TM6605_BOOT_TEST 和 PRESCRIPT_IMU_CAPTURE_TEST
不应同时启用。IMU 测试委托给独立脱线采集模块，正常固件不会挂载采集目录或产生采样文件。
*/
#include "sys/sys_boot_test.h"

#include <Arduino.h>
#include <Wire.h>

#include "bsp/bsp_pins.h"
#include "bsp/bsp_tm6605.h"
#include "sys/sys_constants.h"
#include "sys/sys_imu_capture.h"
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

    constexpr BSP::Tm6605::Effect TEST_EFFECTS[] = {
        BSP::Tm6605::Effect::SharpClick,
        BSP::Tm6605::Effect::LightBump,
        BSP::Tm6605::Effect::DoubleClick,
        BSP::Tm6605::Effect::MediumAlert,
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

}

namespace SysBootTest
{
    bool Enabled()
    {
        return PRESCRIPT_TM6605_BOOT_TEST != 0 || PRESCRIPT_IMU_CAPTURE_TEST != 0;
    }

    bool AllowsMscBoot()
    {
        // 脱线采集文件存放在 FATFS；按住侧键重启后必须能进入现有独占 MSC 会话导出。
        return PRESCRIPT_IMU_CAPTURE_TEST != 0;
    }

    void Setup()
    {
        setCpuFrequencyMhz(PrescriptConst::CPU_RUNTIME_MHZ);
        Serial.begin(115200);
        delay(500);

        if (PRESCRIPT_IMU_CAPTURE_TEST != 0)
        {
            SysImuCapture::Setup();
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
            SysImuCapture::Loop();
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
