/*
【模块职责】PCF8563 RTC 板级驱动。
【能力边界】本层只负责 I2C、寄存器、BCD 日期时间、闹钟和 CLKOUT 等硬件原语；
时间来源优先级、NTP 写回、手动校时和 UI 状态由 sys_time 统一管理。
【硬件约束】扩展板把 INT# 接到 GPIO2；BSP 负责电气输入、寄存器标志和原始电平，
HAL 只负责把该引脚登记为 Light Sleep 唤醒源。CLKOUT 不作为正常运行时钟源。
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
     * PCF8563 单槽闹钟的比较条件。
     * 本项目始终比较分钟和小时；日期、星期可独立启用。日常闹钟关闭日期/星期，
     * 当月日程与跨月检查点启用日期、关闭星期。
     */
    struct AlarmConfig
    {
        uint8_t minute;
        uint8_t hour;
        bool match_day;
        uint8_t day;
        bool match_weekday;
        uint8_t weekday;
    };

    /**
     * 初始化 PCF8563 所在 I2C 总线并探测芯片。
     * 初始化会启动 RTC、清除旧中断状态、禁用未使用的 CLKOUT，并关闭倒计时器以降低备用电池消耗。
     * 该函数在正常启动中由 SysTime_Init 调用；返回 false 表示芯片无应答或默认寄存器配置失败。
     */
    bool Begin(TwoWire &wire = Wire1, uint8_t address = DEFAULT_ADDRESS);

    /** 返回驱动是否已经完成初始化且最近一次关键 I2C 操作成功。 */
    bool IsReady();

    /**
     * 返回 Begin() 成功恢复低功耗默认值的代号；该过程会清空硬件闹钟。
     * 上层可据此让自己的 RTC 槽缓存失效，避免重连后误以为旧计划仍在芯片中。
     */
    uint32_t GetAlarmResetGeneration();

    /** 通过当前 I2C 地址应答判断芯片是否在线；本函数不要求 Begin 已成功。 */
    bool IsPresent();

    /**
     * 把扩展板 RTC INT# 配置为带上拉输入。
     * 本函数只建立 GPIO 电气状态，不注册中断回调，也不配置 ESP32 Light Sleep 唤醒源；
     * Begin() 会自动调用，HAL 在每次睡眠前独立登记该 GPIO 的低电平唤醒。
     */
    void InitializeInterruptPin();

    /**
     * 返回 RTC INT# 当前是否有效。PCF8563 INT# 为开漏低有效，因此 GPIO2 读到 LOW 时返回 true。
     * 调用前应先执行 InitializeInterruptPin() 或 Begin()；尚未初始化时安全返回 false。
     */
    bool IsInterruptAsserted();

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
     * 配置单槽闹钟并清除旧 AF 后启用 AIE。
     * PCF8563 闹钟不包含月份和年份，上层必须用当月条件或月初检查点避免提前触发。
     */
    bool ConfigureAlarm(const AlarmConfig &config);

    /**
     * 只清除闹钟 AF 标志，保留当前 AIE 和倒计时器相关位。
     * RTC INT# 为锁存低电平，唤醒后必须确认该标志，否则下一次 Light Sleep 会立即返回。
     */
    bool AcknowledgeAlarm();

    /** 禁用四个闹钟比较项并清除 AF；不会误清倒计时器的 TF 标志。 */
    bool ClearAlarm();

    /**
     * 配置 CLKOUT 输出。当前板型不使用 CLKOUT，正常运行应保持 Disabled；
     * 其他频率仅供外部探针测量晶振误差时使用。
     */
    bool ConfigureClockOutput(ClockOut output);
}
