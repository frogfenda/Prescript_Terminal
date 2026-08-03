/*
【模块职责】QMC5883P/QMC5883L 三轴磁力计板级驱动，负责器件识别、寄存器配置、状态读取、
原始值到微特斯拉换算以及 Suspend/Wakeup/Reset 生命周期。
【能力边界】本层只报告传感器坐标中的测量事实；机身轴映射、硬铁/软铁校准、磁干扰判断、航向
和姿态融合均属于 SYS 层。
【总线约束】V4B 与 LSM6DSL、PCF8563、TM6605 共用 Wire1。所有接口均为同步短事务，只能由
Arduino 主任务串行调用，不能从中断或另一个核心访问。
*/
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace BSP::Qmc5883
{
    static constexpr uint8_t ADDRESS_QMC5883L = 0x0D;
    static constexpr uint8_t ADDRESS_QMC5883P = 0x2C;
    static constexpr uint8_t CHIP_ID_QMC5883P = 0x80;

    enum class Type : uint8_t
    {
        None,
        Qmc5883l,
        Qmc5883p,
    };

    /** QMC5883P 的量程；QMC5883L 兼容分支固定使用旧版 ±8G 配置。 */
    enum class Range : uint8_t
    {
        G30,
        G12,
        G8,
        G2,
    };

    enum class OutputDataRate : uint8_t
    {
        Hz10,
        Hz50,
        Hz100,
        Hz200,
    };

    enum class Oversampling : uint8_t
    {
        X8,
        X4,
        X2,
        X1,
    };

    struct Config
    {
        Range range = Range::G8;
        OutputDataRate outputRate = OutputDataRate::Hz50;
        Oversampling oversampling1 = Oversampling::X8;
        Oversampling oversampling2 = Oversampling::X8;
    };

    struct Status
    {
        bool dataReady = false;
        bool overflow = false;
    };

    struct Reading
    {
        Status status;
        int16_t xRaw = 0;
        int16_t yRaw = 0;
        int16_t zRaw = 0;
        float xUt = 0.0f;
        float yUt = 0.0f;
        float zUt = 0.0f;
    };

    enum class Error : uint8_t
    {
        None,
        InvalidArgument,
        InvalidConfig,
        NotInitialized,
        DeviceNotFound,
        IdentityMismatch,
        BusError,
        RegisterVerifyFailed,
        PoweredDown,
    };

    /**
     * 最近一次初始化/采样留下的只读诊断快照。
     * valid位用于区分“寄存器值恰好为0”和“本次I2C读取没有成功”；上层只能展示，不能据此
     * 绕过驱动直接访问共享Wire1。
     */
    struct Diagnostics
    {
        uint8_t requestedAddress = 0;
        bool addressAcknowledged = false;
        bool chipIdValid = false;
        uint8_t chipId = 0;
        bool axisSignValid = false;
        uint8_t axisSign = 0;
        bool ctrl1Valid = false;
        uint8_t ctrl1 = 0;
        uint8_t expectedCtrl1 = 0;
        bool ctrl1Matches = false;
        bool ctrl2Valid = false;
        uint8_t ctrl2 = 0;
        uint8_t expectedCtrl2 = 0;
        bool ctrl2Matches = false;
        bool statusValid = false;
        uint8_t status = 0;
    };

    /**
     * 在调用者已经初始化的 I2C 总线上识别并启动磁力计。address=0 时优先验证 V4B 的 P 型，
     * 再兼容探测 L 型；本函数不会重新 begin() 共享 Wire1。
     */
    bool Begin(TwoWire &wire = Wire1, uint8_t address = 0);
    bool Begin(TwoWire &wire, uint8_t address, const Config &config);

    /** 重新写入当前运行配置并回读确认；P 型配置在模式切换前会显式进入 Suspend。 */
    bool Configure(const Config &config);
    bool GetConfig(Config *out);

    /** 软复位并恢复最后一次成功配置。 */
    bool Reset();
    bool PowerDown();
    bool Wakeup();
    bool IsPoweredDown();

    bool IsReady();
    /** CTRL2量程位是否在当前芯片上成功回读一致；false时上层不得把uT换算用于融合/校准。 */
    bool RangeConfigurationVerified();
    bool IsPresent(uint8_t address = 0);
    uint8_t Address();
    Type SensorType();
    const char *TypeName(Type type);
    const char *TypeName();
    const char *ErrorName(Error error);
    Error LastError();
    bool GetDiagnostics(Diagnostics *out);
    TwoWire *Bus();

    /**
     * 先读取状态寄存器，再在 DRDY 时读取同一帧六字节输出。返回 true 表示 I2C 事务成功；
     * status.dataReady=false 只是当前没有新样本。overflow=true 的值仅供诊断，不能用于融合。
     */
    bool Read(Reading *out);

    /** 兼容旧调用者：只在取得一帧新鲜且未溢出的数据时返回 true。 */
    bool ReadRaw(Reading *out);
}
