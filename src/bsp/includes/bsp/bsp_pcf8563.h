/*
【模块职责】PCF8563 RTC 板级驱动。提供 I2C 探测、时间读写、振荡器启动、闹钟和 CLKOUT 配置能力。
【阅读提示】本文件只暴露 RTC 硬件原语；时间可信度、NTP 写回和手动校时策略留在 sys_time.cpp。
*/
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <time.h>

namespace BSP::Pcf8563
{
    static constexpr uint8_t DEFAULT_ADDRESS = 0x51;

    enum class ClockOut : uint8_t
    {
        Disabled = 0x00,
        Hz32768 = 0x80,
        Hz1024 = 0x81,
        Hz32 = 0x82,
        Hz1 = 0x83,
    };

    // 【函数说明】初始化 PCF8563 所在 I2C 总线，探测芯片，并设置安全默认状态。
    bool Begin(TwoWire &wire = Wire1, uint8_t address = DEFAULT_ADDRESS);

    // 【函数说明】返回最近一次 Begin 是否完成并保持可用状态。
    bool IsReady();

    // 【函数说明】通过 I2C 地址应答判断 PCF8563 是否在线。
    bool IsPresent();

    // 【函数说明】读取 RTC 时间到 struct tm；RTC 时间无效或字段非法时返回 false。
    bool ReadTime(struct tm *out_info);

    // 【函数说明】把 struct tm 时间写入 RTC，并清除秒寄存器的 VL 无效时间标志。
    bool WriteTime(const struct tm &info);

    // 【函数说明】清除 STOP/测试位，让 PCF8563 按外部 32.768kHz 晶振继续计时。
    bool StartClock();

    // 【函数说明】按指定本地时间配置分钟/小时/日期闹钟，星期闹钟保持禁用。
    bool ConfigureAlarm(const struct tm &info);

    // 【函数说明】禁用闹钟并清除中断状态。
    bool ClearAlarm();

    // 【函数说明】配置 PCF8563 CLKOUT 输出频率。
    bool ConfigureClockOutput(ClockOut output);
}
