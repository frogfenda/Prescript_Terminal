/*
【模块职责】LSM6DSV 六轴 IMU 板级驱动接口，负责共享 I2C 总线上的芯片识别、寄存器配置、
六轴/温度采样、FIFO 原始数据访问以及传感器自身的 Power-down、唤醒和软复位恢复。
【能力边界】本层只返回芯片状态、原始值和物理量；坐标安装矩阵、姿态解算、手势和 APP 语义
属于 SYS/APP 层，不得写入 BSP。
【线程约束】Wire1 还与 RTC、TM6605 和磁力计共用。所有接口均执行同步 I2C 事务，只能由同一
任务串行调用，不能从中断或另一个核心并发访问。
【硬件约束】当前主板把 SA0 接地、CS 上拉，固定使用 0x6A 的 I2C 模式；INT1/INT2 未接 ESP32，
因此数据就绪和 FIFO 状态都只能轮询，不能依赖 IMU 唤醒主控。
*/
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace BSP::Lsm6dsv
{
    static constexpr uint8_t ADDRESS_LOW = 0x6A;
    static constexpr uint8_t ADDRESS_HIGH = 0x6B;
    static constexpr uint8_t DEFAULT_ADDRESS = ADDRESS_LOW;
    static constexpr uint8_t WHO_AM_I_VALUE = 0x70;

    /** 加速度计/陀螺仪 ODR 枚举；并非每个测量单元都支持其中的全部档位。 */
    enum class OutputDataRate : uint8_t
    {
        PowerDown,
        Hz1_875,
        Hz7_5,
        Hz15,
        Hz30,
        Hz60,
        Hz120,
        Hz240,
        Hz480,
        Hz960,
        Hz1920,
        Hz3840,
        Hz7680,
    };

    /** 加速度满量程。范围越大越不容易削顶，范围越小分辨率越高。 */
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
        Dps4000,
    };

    /**
     * 主输出通道配置。默认值即当前 SysMotion 的 120 Hz、±16 g、±2000 dps 采集契约。
     * 加速度计和陀螺仪可独立 Power-down；陀螺仪主输出不接受 1.875 Hz。
     */
    struct Config
    {
        OutputDataRate accelRate = OutputDataRate::Hz120;
        OutputDataRate gyroRate = OutputDataRate::Hz120;
        AccelRange accelRange = AccelRange::G16;
        GyroRange gyroRange = GyroRange::Dps2000;
    };

    /** STATUS_REG 的三个数据就绪位；false 只表示没有新样本，不表示 I2C 失败。 */
    struct DataReady
    {
        bool accel = false;
        bool gyro = false;
        bool temperature = false;
    };

    /**
     * 一次连续寄存器事务得到的完整输出。
     * ready 表示读取开始时各通道是否有新数据；即使某项为 false，对应数值仍是最近寄存器值。
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

    /** FIFO 标签值是 FIFO_DATA_OUT_TAG[7:3] 解码后的传感器编号。 */
    enum class FifoTag : uint8_t
    {
        Empty = 0x00,
        Gyroscope = 0x01,
        Accelerometer = 0x02,
        Temperature = 0x03,
        Timestamp = 0x04,
        ConfigChange = 0x05,
        GameRotationVector = 0x13,
        GyroscopeBias = 0x16,
        GravityVector = 0x17,
        Unknown = 0xFF,
    };

    /**
     * FIFO 配置。watermarkWords 的单位是一组“1 字节标签 + 6 字节数据”，有效范围 1..255。
     * 温度批处理只支持 PowerDown、1.875 Hz、15 Hz 和 60 Hz；其他组合会返回 InvalidConfig。
     */
    struct FifoConfig
    {
        bool enabled = false;
        uint8_t watermarkWords = 16;
        OutputDataRate accelBatchRate = OutputDataRate::Hz120;
        OutputDataRate gyroBatchRate = OutputDataRate::Hz120;
        OutputDataRate temperatureBatchRate = OutputDataRate::PowerDown;
        bool continuous = true;
    };

    /**
     * FIFO 状态快照。读取会按芯片语义清除 overrunLatched，其他状态位不由 BSP 模拟保持。
     */
    struct FifoStatus
    {
        uint16_t unreadWords = 0;
        bool watermark = false;
        bool overrun = false;
        bool full = false;
        bool overrunLatched = false;
    };

    /** FIFO 中一个 7 字节 word 的解码结果；x/y/z 保留原始补码，由调用者依据 tag 决定含义。 */
    struct FifoWord
    {
        FifoTag tag = FifoTag::Empty;
        uint8_t tagValue = 0;
        uint8_t counter = 0;
        int16_t x = 0;
        int16_t y = 0;
        int16_t z = 0;
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
        FifoDisabled,
        FifoEmpty,
    };

    /**
     * 使用默认运行配置初始化 LSM6DSV。address=0 时依次探测 0x6A/0x6B；当前板应固定传 0x6A。
     * 函数会执行软复位、关闭 FIFO、写入并回读关键寄存器，最长阻塞约 130 ms。
     */
    bool Begin(TwoWire &wire = Wire1, uint8_t address = DEFAULT_ADDRESS);

    /** 与 Begin 相同，但由调用者指定 ODR 和量程；不支持的组合返回 false。 */
    bool Begin(TwoWire &wire, uint8_t address, const Config &config);

    /** 更新主输出通道 ODR/量程并回读校验；从停机恢复时会等待陀螺仪稳定，不能逐帧调用。 */
    bool Configure(const Config &config);

    /** 复制最后一次成功保存的主输出配置；out 为空或尚未初始化时返回 false。 */
    bool GetConfig(Config *out);

    /** 执行软件复位并恢复最后一次主输出与 FIFO 配置，用于共享 I2C 异常后的显式恢复。 */
    bool Reset();

    /** 关闭两种测量单元并临时旁路 FIFO，保留配置供 Wakeup() 恢复。 */
    bool PowerDown();

    /** 从 PowerDown() 恢复最后保存的主输出与 FIFO 配置。 */
    bool Wakeup();

    /** 返回驱动是否已完成初始化且最近关键总线事务成功。 */
    bool IsReady();

    /** 返回两个测量单元是否都处于 Power-down；该状态下 I2C 仍可访问。 */
    bool IsPoweredDown();

    /** 读取 WHO_AM_I 判断设备是否存在；address=0 时探测 0x6A/0x6B，不改变当前绑定地址。 */
    bool IsPresent(uint8_t address = 0);

    /** 返回 Begin 最终选择的七位 I2C 地址；尚未选择时为 0。 */
    uint8_t Address();

    /** 返回当前绑定的 I2C 总线，仅用于诊断共享总线归属，不转移所有权。 */
    TwoWire *Bus();

    /** 返回最近一次操作结果；读取本值不会清除错误。 */
    Error LastError();

    /** 单独读取 STATUS_REG；返回 false 表示参数、驱动状态或 I2C 事务失败。 */
    bool ReadStatus(DataReady *out);

    /**
     * 从 STATUS_REG 连续读取温度、角速度和加速度，并按当前量程换算为 °C、dps 和 g。
     * 两种测量单元均 Power-down 时返回 false，防止上层误用停机前残留寄存器。
     */
    bool Read(Reading *out);

    /** 配置或关闭 FIFO，并回读 FIFO_CTRL1..4 校验；当前无中断连线，只能配合轮询接口使用。 */
    bool ConfigureFifo(const FifoConfig &config);

    /** 复制最后一次成功保存的 FIFO 配置。 */
    bool GetFifoConfig(FifoConfig *out);

    /** 读取未读 word 数和水位/溢出状态；读取成功不代表 FIFO 中一定有数据。 */
    bool ReadFifoStatus(FifoStatus *out);

    /** 读取并弹出一个 FIFO word；空 FIFO 返回 false 且 LastError() 为 FifoEmpty。 */
    bool ReadFifoWord(FifoWord *out);

    /** 清空 FIFO；若 FIFO 已启用，会在旁路后恢复保存的批处理配置。 */
    bool ResetFifo();
}
