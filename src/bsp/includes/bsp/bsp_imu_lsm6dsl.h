/*
【模块职责】LSM6DSL 六轴 IMU 板级驱动接口，负责共享 I2C 总线上的芯片识别、寄存器配置、
六轴/温度采样以及传感器自身的 Power-down、唤醒和软复位恢复。
【能力边界】本层只返回物理量和数据就绪状态；摇动方向、菜单滚动、换武器、姿态解算和流体模拟
属于可复用的 SYS 算法或 APP 业务，不得写入 BSP。
【线程约束】Wire1 还与 RTC、TM6605 和磁力计共用。所有接口都执行同步 I2C 事务，必须由同一任务
串行调用，不能从中断或另一个核心并发访问。
【硬件约束】V4B 的 INT1/INT2 未接 ESP32，因此本驱动只提供轮询采样，不能用 IMU 中断唤醒主控。
*/
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace BSP::Lsm6dsl
{
    static constexpr uint8_t ADDRESS_LOW = 0x6A;
    static constexpr uint8_t ADDRESS_HIGH = 0x6B;
    static constexpr uint8_t DEFAULT_ADDRESS = ADDRESS_LOW;

    /** 加速度计和陀螺仪共用的输出数据率；PowerDown 只关闭对应传感器，不影响 I2C。 */
    enum class OutputDataRate : uint8_t
    {
        PowerDown,
        Hz12_5,
        Hz26,
        Hz52,
        Hz104,
        Hz208,
        Hz416,
        Hz833,
        Hz1660,
        Hz3330,
        Hz6660,
    };

    /** 加速度满量程。范围越小灵敏度越高，范围越大越不容易在快速摇动时饱和。 */
    enum class AccelRange : uint8_t
    {
        G2,
        G4,
        G8,
        G16,
    };

    /** 陀螺仪满量程，单位为 degree per second。 */
    enum class GyroRange : uint8_t
    {
        Dps125,
        Dps250,
        Dps500,
        Dps1000,
        Dps2000,
    };

    /**
     * 传感器运行配置。默认值保持旧驱动的 104 Hz、±2 g、±250 dps 行为。
     * 两个传感器可以使用不同 ODR，也可以把其中一个单独设为 PowerDown。
     */
    struct Config
    {
        OutputDataRate accelRate = OutputDataRate::Hz104;
        OutputDataRate gyroRate = OutputDataRate::Hz104;
        AccelRange accelRange = AccelRange::G2;
        GyroRange gyroRange = GyroRange::Dps250;
    };

    /** STATUS_REG 的三个数据就绪位；false 只表示没有新样本，不表示 I2C 失败。 */
    struct DataReady
    {
        bool accel = false;
        bool gyro = false;
        bool temperature = false;
    };

    /**
     * 一次连续寄存器事务得到的完整样本。
     * ready 表示读取开始时各输出是否包含新数据；即使某项为 false，对应数值仍是芯片保存的最近样本。
     */
    struct Reading
    {
        DataReady ready;

        int16_t axRaw = 0;
        int16_t ayRaw = 0;
        int16_t azRaw = 0;
        int16_t gxRaw = 0;
        int16_t gyRaw = 0;
        int16_t gzRaw = 0;
        int16_t temperatureRaw = 0;

        float axG = 0.0f;
        float ayG = 0.0f;
        float azG = 0.0f;
        float gxDps = 0.0f;
        float gyDps = 0.0f;
        float gzDps = 0.0f;
        float temperatureC = 0.0f;
    };

    /** 最近一次驱动操作的失败原因；成功操作会恢复为 None。 */
    enum class Error : uint8_t
    {
        None,
        InvalidArgument,
        InvalidConfig,
        NotInitialized,
        DeviceNotFound,
        IdentityMismatch,
        BusError,
        ResetTimeout,
        RegisterVerifyFailed,
        PoweredDown,
    };

    /**
     * 使用默认运行配置初始化 LSM6DSL。
     * V4B 必须使用 Wire1/GPIO18/GPIO17；默认地址 0x6A 来自代码与实际接线约定。
     * address=0 时才会依次探测 0x6A/0x6B。函数包含软复位和最长约 35 ms 的上电稳定等待。
     */
    bool Begin(TwoWire &wire = Wire1, uint8_t address = DEFAULT_ADDRESS);

    /** 与 Begin 相同，但允许调用者一次性指定 ODR 和量程；非法枚举值会返回 false。 */
    bool Begin(TwoWire &wire, uint8_t address, const Config &config);

    /**
     * 在设备已初始化时更新 ODR 和量程，并回读关键寄存器确认写入成功。
     * 从 Power-down 切回采样时会等待手册规定的 35 ms turn-on 时间，不能在每帧循环中调用。
     */
    bool Configure(const Config &config);

    /** 复制最后一次成功的运行配置；out 为空或尚未初始化时返回 false。 */
    bool GetConfig(Config *out);

    /**
     * 执行芯片软件复位并恢复最后一次成功配置，用于共享 I2C 异常后的显式恢复。
     * 本函数不重新创建 Wire 对象，但会进行多次短事务和最长 100 ms 的复位完成轮询。
     */
    bool Reset();

    /** 把加速度计和陀螺仪 ODR 都置零，保留配置供 Wakeup() 恢复。 */
    bool PowerDown();

    /** 从 PowerDown() 恢复最后一次成功配置；恢复采样包含约 35 ms 稳定等待。 */
    bool Wakeup();

    /** 返回芯片是否已初始化、最近关键事务成功且可继续访问。 */
    bool IsReady();

    /** 返回两个测量单元当前是否均处于 Power-down；I2C 在该状态下仍可访问。 */
    bool IsPoweredDown();

    /**
     * 读取 WHO_AM_I 判断指定地址是否为 LSM6DSL，不要求 Begin 成功。
     * address=0 时探测 0x6A 和 0x6B；该查询不会改变当前运行地址和就绪状态。
     */
    bool IsPresent(uint8_t address = 0);

    /** 返回 Begin 最终选择的七位 I2C 地址；尚未选择地址时返回 0。 */
    uint8_t Address();

    /** 返回当前绑定的 I2C 总线，供 SYS 层诊断共享总线归属，不转移所有权。 */
    TwoWire *Bus();

    /** 返回最近一次操作结果；读取该值不会清除错误。 */
    Error LastError();

    /** 单独读取 STATUS_REG；返回 false 表示参数、状态或 I2C 事务失败。 */
    bool ReadStatus(DataReady *out);

    /**
     * 从 STATUS_REG 到加速度输出执行一次连续读取，同时得到新鲜度、温度、角速度和加速度。
     * PowerDown 状态返回 false，防止上层把停机前残留寄存器误认为当前运动数据。
     */
    bool Read(Reading *out);
}
