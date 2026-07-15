/*
【模块职责】PCF8563 RTC 板级驱动。
【能力边界】本层只负责 I2C、寄存器、BCD 日期时间、闹钟和 CLKOUT 等硬件原语；
时间来源优先级、NTP 写回、手动校时和 UI 状态由 sys_time 统一管理。
【硬件约束】V4B 的 INT# 与 CLKOUT 均未连接到 ESP32，因此闹钟只能设置/查询芯片状态，
不能直接唤醒当前主板；CLKOUT 仅适合生产校准时临时启用。
*/
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <time.h>

namespace BSP::Pcf8563
{
    static constexpr uint8_t DEFAULT_ADDRESS = 0x51;

    /** PCF8563 CLKOUT_control 寄存器支持的输出模式。 */
    enum class ClockOut : uint8_t
    {
        Disabled = 0x00,
        Hz32768 = 0x80,
        Hz1024 = 0x81,
        Hz32 = 0x82,
        Hz1 = 0x83,
    };

    /**
     * 读取 RTC 时间时的细分结果。
     * SYS 层需要区分备用电池低压、I2C 通信失败和寄存器内容非法，不能把它们都当成普通 false。
     */
    enum class TimeReadResult : uint8_t
    {
        Ok,
        InvalidArgument,
        NotInitialized,
        BusError,
        VoltageLow,
        InvalidData,
    };

    /**
     * 初始化 PCF8563 所在 I2C 总线并探测芯片。
     * 初始化会启动 RTC、清除旧中断状态、禁用未接线的 CLKOUT，并关闭倒计时器以降低备用电池消耗。
     * 该函数在正常启动中由 SysTime_Init 调用；返回 false 表示芯片无应答或默认寄存器配置失败。
     */
    bool Begin(TwoWire &wire = Wire1, uint8_t address = DEFAULT_ADDRESS);

    /** 返回驱动是否已经完成初始化且最近一次关键 I2C 操作成功。 */
    bool IsReady();

    /** 通过当前 I2C 地址应答判断芯片是否在线；本函数不要求 Begin 已成功。 */
    bool IsPresent();

    /**
     * 一次性读取秒到年七个寄存器并转换为本地时间 struct tm。
     * 返回 VoltageLow 表示 VL 标志置位，备用电池或晶振曾不足以保证时间可信；此时不会写出 out_info。
     * PCF8563 只保存两位年份，本项目的产品日期范围固定解释为 2000～2099 年。
     */
    TimeReadResult ReadTime(struct tm *out_info);

    /**
     * 一次性把 2000～2099 年的本地时间写入 RTC，并清除秒寄存器的 VL 标志。
     * 星期由年月日重新计算，不依赖调用方是否正确填写 tm_wday。
     */
    bool WriteTime(const struct tm &info);

    /** 清除 TEST/STOP 位，让 RTC 按外部 32.768 kHz 晶振继续计时。 */
    bool StartClock();

    /**
     * 配置“分钟 + 小时 + 日期”匹配闹钟，星期保持禁用。
     * PCF8563 闹钟不包含年月，因此该条件会按月重复；V4B 的 INT# 未接线，不能据此唤醒 ESP32。
     */
    bool ConfigureAlarm(const struct tm &info);

    /** 禁用四个闹钟比较项并清除 AF；不会误清倒计时器的 TF 标志。 */
    bool ClearAlarm();

    /**
     * 配置 CLKOUT 输出。V4B 的 CLKOUT 未接线，正常运行应保持 Disabled；
     * 其他频率仅供外部探针测量晶振误差时使用。
     */
    bool ConfigureClockOutput(ClockOut output);
}
