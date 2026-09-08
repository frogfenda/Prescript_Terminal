/*
【模块职责】MMC5603NJ 三轴磁力计板级驱动。负责器件探测、连续测量配置、20位原始值解码、
微特斯拉换算以及休眠/唤醒生命周期。
【能力边界】本层只提供传感器坐标中的测量事实；安装轴映射、硬铁/软铁校准、磁干扰判断和
航向融合属于SYS层。MMC5603控制寄存器为只写，configurationWritten表示配置事务全部应答，
不表示已经从芯片回读控制位。
【总线约束】当前主板与LSM6DSV、PCF8563、TM6605共用Wire1。所有接口只允许Arduino主任务
同步调用，不在本层重新初始化共享I2C总线。
*/
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace BSP::Mmc5603
{
    static constexpr uint8_t DEFAULT_ADDRESS = 0x30;
    static constexpr uint8_t FIRST_ADDRESS = 0x30;
    static constexpr uint8_t LAST_ADDRESS = 0x37;
    static constexpr uint8_t PRODUCT_ID_VALUE = 0x10;

    enum class Bandwidth : uint8_t
    {
        Ms6_6 = 0,
        Ms3_5 = 1,
        Ms2_0 = 2,
        Ms1_2 = 3,
    };

    enum class PeriodicSet : uint8_t
    {
        Every1 = 0,
        Every25 = 1,
        Every75 = 2,
        Every100 = 3,
        Every250 = 4,
        Every500 = 5,
        Every1000 = 6,
        Every2000 = 7,
    };

    struct Config
    {
        uint8_t outputRateHz = 100;
        Bandwidth bandwidth = Bandwidth::Ms3_5;
        bool automaticSetReset = true;
        bool periodicSet = true;
        PeriodicSet periodicSetInterval = PeriodicSet::Every100;
    };

    struct Status
    {
        bool temperatureReady = false;
        bool dataReady = false;
        bool selfTestSignal = false;
        bool otpLoaded = false;
    };

    struct Reading
    {
        Status status;
        // 芯片输出为无符号20位；对外raw值已减去零场中心524288，便于日志和校准处理。
        int32_t xRaw = 0;
        int32_t yRaw = 0;
        int32_t zRaw = 0;
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
        OtpNotReady,
        BusError,
        PoweredDown,
    };

    /** 最近一次探测和采样留下的只读诊断快照；控制寄存器只记录期望写值。 */
    struct Diagnostics
    {
        uint8_t requestedAddress = 0;
        uint8_t detectedAddress = 0;
        bool addressAcknowledged = false;
        bool productIdValid = false;
        uint8_t productId = 0;
        bool statusValid = false;
        uint8_t status = 0;
        bool otpLoaded = false;
        bool configurationWritten = false;
        uint8_t expectedOdr = 0;
        uint8_t expectedCtrl0 = 0;
        uint8_t expectedCtrl1 = 0;
        uint8_t expectedCtrl2 = 0;
    };

    /** address=0时扫描MMC5603允许的0x30～0x37地址范围，并用0x39产品ID确认器件。 */
    bool Begin(TwoWire &wire = Wire1, uint8_t address = 0);
    bool Begin(TwoWire &wire, uint8_t address, const Config &config);
    bool Configure(const Config &config);
    bool GetConfig(Config *out);

    bool Reset();
    bool PowerDown();
    bool Wakeup();
    bool IsPoweredDown();
    bool IsReady();
    bool ConfigurationWritten();
    bool IsPresent(uint8_t address = 0);
    uint8_t Address();
    const char *TypeName();
    const char *ErrorName(Error error);
    Error LastError();
    bool GetDiagnostics(Diagnostics *out);
    TwoWire *Bus();

    bool ReadStatus(Status *out);

    /**
     * 先读状态；仅在dataReady时读取0x00～0x08并组合20位XYZ。返回true表示I2C事务成功，
     * dataReady=false只是当前没有新样本，不是错误。
     */
    bool Read(Reading *out);
    bool ReadRaw(Reading *out);

    /** 独立SET/RESET命令只供诊断和后续高精度零偏流程使用，不改变连续测量配置。 */
    bool PerformSet();
    bool PerformReset();
}
