/*
【模块职责】W25N01GV 1Gbit SPI NAND 板级驱动。负责独立 SPI3 总线初始化、JEDEC 身份确认、
状态寄存器访问、页缓存读、整页编程、块擦除、片内 ECC 结果解码和坏块标记/LUT只读查询。
【能力边界】本芯片是原始 NAND，不是可直接挂载的 NOR/FFat 分区。坏块表、逻辑到物理映射、
磨损均衡、掉电一致性、文件或对象索引必须由后续 SYS 存储层负责；BSP 不伪造块设备语义。
【安全约束】上电默认保持整片写保护。ProgramPage()/EraseBlock() 只有在调用者显式执行
SetArrayWriteProtected(false) 后才允许进行；初始化和诊断绝不擦除、编程或改写坏块 LUT。
【总线约束】NV3007 屏幕占用 SPI2_HOST；当前 NAND 固定使用 Arduino HSPI（ESP32-S3 的 SPI3）
和 GPIO39～42。接口为同步调用，不能从中断使用，也不能由多个任务并发访问。
*/
#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace BSP::W25n01
{
    static constexpr uint8_t MANUFACTURER_ID = 0xEF;
    static constexpr uint16_t DEVICE_ID = 0xAA21;

    static constexpr uint16_t PAGE_DATA_SIZE = 2048;
    static constexpr uint16_t PAGE_SPARE_SIZE = 64;
    static constexpr uint16_t PAGE_TOTAL_SIZE = PAGE_DATA_SIZE + PAGE_SPARE_SIZE;
    static constexpr uint16_t PAGES_PER_BLOCK = 64;
    static constexpr uint16_t BLOCK_COUNT = 1024;
    static constexpr uint32_t PAGE_COUNT = 65536UL;
    static constexpr uint32_t CAPACITY_BYTES = 128UL * 1024UL * 1024UL;
    static constexpr size_t BBM_LUT_ENTRY_COUNT = 20;
    static constexpr uint32_t DEFAULT_SPI_FREQUENCY_HZ = 20000000UL;

    struct JedecId
    {
        uint8_t manufacturer = 0;
        uint8_t deviceHigh = 0;
        uint8_t deviceLow = 0;

        bool matchesW25n01gv() const
        {
            return manufacturer == MANUFACTURER_ID &&
                   deviceHigh == static_cast<uint8_t>(DEVICE_ID >> 8) &&
                   deviceLow == static_cast<uint8_t>(DEVICE_ID & 0xFF);
        }
    };

    enum class EccStatus : uint8_t
    {
        Clean,
        CorrectedOneBit,
        Uncorrectable,
        MultipleUncorrectable,
        Unknown,
    };

    /** 三个状态寄存器的一致快照；字段均直接由芯片位定义解码，不模拟软件状态。 */
    struct Status
    {
        uint8_t protectionRaw = 0;
        uint8_t configurationRaw = 0;
        uint8_t operationRaw = 0;

        bool arrayWriteProtected = true;
        bool otpMode = false;
        bool eccEnabled = false;
        bool bufferReadMode = false;
        bool lutFull = false;
        EccStatus ecc = EccStatus::Unknown;
        bool programFailed = false;
        bool eraseFailed = false;
        bool writeEnableLatch = false;
        bool busy = false;
    };

    /** 片内 BBM LUT 的一项；BSP只读出事实，不主动建立或失效任何映射。 */
    struct BadBlockLink
    {
        bool enabled = false;
        bool invalid = false;
        uint16_t logicalBlock = 0;
        uint16_t physicalBlock = 0;
    };

    enum class Error : uint8_t
    {
        None,
        InvalidArgument,
        NotInitialized,
        DeviceNotFound,
        IdentityMismatch,
        BusyTimeout,
        ConfigurationVerifyFailed,
        ArrayWriteProtected,
        WriteEnableFailed,
        ProgramFailed,
        EraseFailed,
        EccUncorrectable,
    };

    /** 最近一次初始化/操作留下的诊断事实，供启动日志和测试页面读取。 */
    struct Diagnostics
    {
        bool busStarted = false;
        bool initialized = false;
        bool identityValid = false;
        JedecId id;
        uint32_t spiFrequencyHz = 0;
        Status status;
        uint16_t lastPage = 0;
        uint16_t lastBlock = 0;
    };

    /**
     * 初始化独占的 SPI3 总线，等待已有 NAND 操作结束后软复位，验证 EF AA 21，确保片内 ECC
     * 与 Buffer Read 模式开启。函数不解除阵列写保护，不读写主阵列，也不扫描坏块。
     */
    bool Begin(uint32_t frequencyHz = DEFAULT_SPI_FREQUENCY_HZ);

    /** 等待空闲后执行软复位并恢复 ECC + Buffer Read 配置；不会强行中断擦写。 */
    bool Reset();

    bool IsReady();
    bool IsPresent();
    SPIClass *Bus();
    uint32_t FrequencyHz();
    Error LastError();
    const char *ErrorName(Error error);
    const char *EccStatusName(EccStatus status);
    bool GetDiagnostics(Diagnostics *out);

    bool ReadJedecId(JedecId *out);
    bool ReadStatus(Status *out);
    bool WaitReady(uint32_t timeoutMs = 20);

    /**
     * 设置阵列块保护位。protectedState=false 只是允许后续写命令，函数自身不修改主阵列；
     * 每次编程/擦除仍由 BSP 单独发送 WREN。建议存储层只在短事务期间解锁，并在结束后重新保护。
     */
    bool SetArrayWriteProtected(bool protectedState);

    /**
     * 从指定页主数据区读取任意连续片段。片内 ECC 检出不可纠正错误时返回 false，且不会把
     * 不可信数据交给调用者；一位纠正仍返回 true，并通过 eccStatus/Diagnostics 报告。
     */
    bool ReadPage(uint16_t page,
                  uint16_t column,
                  uint8_t *data,
                  size_t length,
                  EccStatus *eccStatus = nullptr);

    /**
     * 把恰好 2048 字节写入一个已擦除页。整页接口刻意避免随意部分编程破坏 ECC；同一块内页地址
     * 必须由上层按从低到高顺序编程。调用前还必须完成全盘出厂坏块扫描并维护逻辑映射。
     */
    bool ProgramPage(uint16_t page, const uint8_t *data, size_t length = PAGE_DATA_SIZE);

    /** 擦除一个 128KiB 物理块。BSP不替上层判断该块是否已登记为坏块。 */
    bool EraseBlock(uint16_t block);

    /**
     * 读取物理块第一页的出厂坏块标记：主区Byte0和Spare Byte0..1任一非FF即为坏块。
     * 该判断只适用于首次编程前；主区Byte0被用户数据写过以后不能再重建原始出厂标记。
     */
    bool ReadFactoryBadBlockMarker(uint16_t block, bool *isBad);

    /** 一次读出芯片内固定20项BBM LUT；未使用项也会返回，状态由enabled/invalid表示。 */
    bool ReadBadBlockLut(BadBlockLink *entries, size_t entryCount = BBM_LUT_ENTRY_COUNT);

    /** 输出一行中文启动诊断；不会访问主阵列或触发写操作。 */
    void PrintDiagnostics();
}
