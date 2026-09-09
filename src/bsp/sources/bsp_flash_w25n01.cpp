/*
【模块职责】实现 W25N01GV 的单线 SPI NAND 基础访问，命令和状态位依据 Winbond
W25N01GV Rev.R（2023-07-03）数据手册。
【实现策略】使用 SPI Mode 0、MSB first、20MHz 保守起步；屏幕独占 SPI2，本驱动独占 SPI3。
页读取严格执行“Page Data Read→轮询BUSY→Buffer Read→检查ECC”，写入严格执行
“WREN→整页Load→Program Execute→轮询P-FAIL”，块擦除同样检查E-FAIL。
【错误策略】本SPI API没有逐事务ACK，因此通过JEDEC身份、配置回读、BUSY/WEL/P-FAIL/E-FAIL和ECC
建立可观测错误边界。高频接口不刷串口，调用者可读取LastError()/Diagnostics统一报告。
*/
#include "bsp/bsp_flash_w25n01.h"
#include "bsp/bsp_pins.h"

#include <string.h>

namespace
{
    static constexpr uint8_t CMD_RESET = 0xFF;
    static constexpr uint8_t CMD_READ_JEDEC_ID = 0x9F;
    static constexpr uint8_t CMD_READ_STATUS = 0x0F;
    static constexpr uint8_t CMD_WRITE_STATUS = 0x1F;
    static constexpr uint8_t CMD_WRITE_ENABLE = 0x06;
    static constexpr uint8_t CMD_READ_BBM_LUT = 0xA5;
    static constexpr uint8_t CMD_BLOCK_ERASE = 0xD8;
    static constexpr uint8_t CMD_PROGRAM_DATA_LOAD = 0x02;
    static constexpr uint8_t CMD_PROGRAM_EXECUTE = 0x10;
    static constexpr uint8_t CMD_PAGE_DATA_READ = 0x13;
    static constexpr uint8_t CMD_READ_DATA = 0x03;

    static constexpr uint8_t REG_PROTECTION = 0xA0;
    static constexpr uint8_t REG_CONFIGURATION = 0xB0;
    static constexpr uint8_t REG_OPERATION = 0xC0;

    static constexpr uint8_t PROTECTION_BLOCK_MASK = 0x7C;
    static constexpr uint8_t CONFIG_OTP_LOCK = 0x80;
    static constexpr uint8_t CONFIG_OTP_MODE = 0x40;
    static constexpr uint8_t CONFIG_SR1_LOCK = 0x20;
    static constexpr uint8_t CONFIG_ECC_ENABLE = 0x10;
    static constexpr uint8_t CONFIG_BUFFER_MODE = 0x01;

    static constexpr uint8_t STATUS_LUT_FULL = 0x40;
    static constexpr uint8_t STATUS_ECC_MASK = 0x30;
    static constexpr uint8_t STATUS_PROGRAM_FAIL = 0x08;
    static constexpr uint8_t STATUS_ERASE_FAIL = 0x04;
    static constexpr uint8_t STATUS_WRITE_ENABLE = 0x02;
    static constexpr uint8_t STATUS_BUSY = 0x01;

    static constexpr uint32_t RESET_READY_TIMEOUT_MS = 20;
    static constexpr uint32_t PAGE_READ_TIMEOUT_MS = 5;
    static constexpr uint32_t PAGE_PROGRAM_TIMEOUT_MS = 5;
    static constexpr uint32_t BLOCK_ERASE_TIMEOUT_MS = 20;

    // Arduino-ESP32 在 ESP32-S3 上把 HSPI 索引1映射到 SPI3；SPI2 已由NV3007的IDF驱动占用。
    SPIClass s_spi(HSPI);
    SPISettings s_settings(BSP::W25n01::DEFAULT_SPI_FREQUENCY_HZ, MSBFIRST, SPI_MODE0);
    uint32_t s_frequency_hz = BSP::W25n01::DEFAULT_SPI_FREQUENCY_HZ;
    bool s_bus_started = false;
    bool s_initialized = false;
    bool s_ready = false;
    BSP::W25n01::Error s_last_error = BSP::W25n01::Error::NotInitialized;
    BSP::W25n01::Diagnostics s_diagnostics = {};

    bool Fail(BSP::W25n01::Error error, bool markNotReady = false)
    {
        s_last_error = error;
        if (markNotReady)
            s_ready = false;
        return false;
    }

    void ClearError()
    {
        s_last_error = BSP::W25n01::Error::None;
    }

    void Select()
    {
        s_spi.beginTransaction(s_settings);
        digitalWrite(BSP::Pins::NAND_CS, LOW);
    }

    void Deselect()
    {
        digitalWrite(BSP::Pins::NAND_CS, HIGH);
        s_spi.endTransaction();
    }

    uint8_t ReadRegisterRaw(uint8_t address)
    {
        Select();
        s_spi.transfer(CMD_READ_STATUS);
        s_spi.transfer(address);
        const uint8_t value = s_spi.transfer(0x00);
        Deselect();
        return value;
    }

    void WriteRegisterRaw(uint8_t address, uint8_t value)
    {
        Select();
        s_spi.transfer(CMD_WRITE_STATUS);
        s_spi.transfer(address);
        s_spi.transfer(value);
        Deselect();
    }

    BSP::W25n01::EccStatus DecodeEcc(uint8_t operation)
    {
        using BSP::W25n01::EccStatus;
        switch ((operation & STATUS_ECC_MASK) >> 4)
        {
        case 0: return EccStatus::Clean;
        case 1: return EccStatus::CorrectedOneBit;
        case 2: return EccStatus::Uncorrectable;
        case 3: return EccStatus::MultipleUncorrectable;
        default: return EccStatus::Unknown;
        }
    }

    BSP::W25n01::Status DecodeStatus(uint8_t protection, uint8_t configuration, uint8_t operation)
    {
        BSP::W25n01::Status out;
        out.protectionRaw = protection;
        out.configurationRaw = configuration;
        out.operationRaw = operation;
        out.arrayWriteProtected = (protection & PROTECTION_BLOCK_MASK) != 0;
        out.otpMode = (configuration & CONFIG_OTP_MODE) != 0;
        out.eccEnabled = (configuration & CONFIG_ECC_ENABLE) != 0;
        out.bufferReadMode = (configuration & CONFIG_BUFFER_MODE) != 0;
        out.lutFull = (operation & STATUS_LUT_FULL) != 0;
        out.ecc = DecodeEcc(operation);
        out.programFailed = (operation & STATUS_PROGRAM_FAIL) != 0;
        out.eraseFailed = (operation & STATUS_ERASE_FAIL) != 0;
        out.writeEnableLatch = (operation & STATUS_WRITE_ENABLE) != 0;
        out.busy = (operation & STATUS_BUSY) != 0;
        return out;
    }

    bool ReadStatusRaw(BSP::W25n01::Status *out)
    {
        if (!out || !s_bus_started)
            return false;
        const uint8_t protection = ReadRegisterRaw(REG_PROTECTION);
        const uint8_t configuration = ReadRegisterRaw(REG_CONFIGURATION);
        const uint8_t operation = ReadRegisterRaw(REG_OPERATION);
        *out = DecodeStatus(protection, configuration, operation);
        s_diagnostics.status = *out;
        return true;
    }

    bool WaitReadyRaw(uint32_t timeoutMs, BSP::W25n01::Status *finalStatus = nullptr)
    {
        if (!s_bus_started)
            return false;

        const uint32_t started_ms = millis();
        BSP::W25n01::Status status;
        do
        {
            if (!ReadStatusRaw(&status))
                return false;
            if (!status.busy)
            {
                if (finalStatus)
                    *finalStatus = status;
                return true;
            }
            delay(1);
        } while (millis() - started_ms < timeoutMs);

        if (finalStatus)
            *finalStatus = status;
        return false;
    }

    bool ReadJedecIdRaw(BSP::W25n01::JedecId *out)
    {
        if (!out || !s_bus_started)
            return false;
        Select();
        s_spi.transfer(CMD_READ_JEDEC_ID);
        s_spi.transfer(0x00); // 数据手册要求8个dummy clocks。
        out->manufacturer = s_spi.transfer(0x00);
        out->deviceHigh = s_spi.transfer(0x00);
        out->deviceLow = s_spi.transfer(0x00);
        Deselect();
        s_diagnostics.id = *out;
        s_diagnostics.identityValid = out->matchesW25n01gv();
        return true;
    }

    bool ConfigureReadMode()
    {
        BSP::W25n01::Status status;
        if (!ReadStatusRaw(&status))
            return Fail(BSP::W25n01::Error::ConfigurationVerifyFailed, true);

        /*
         * 只保留可能已经永久锁定的OTP-L/SR1-L，显式退出OTP访问模式，并开启ECC和Buffer Read。
         * 绝不在这里写OTP-L/SR1-L为1，也不触碰SR1块保护位。
         */
        const uint8_t desired = static_cast<uint8_t>(
            (status.configurationRaw & (CONFIG_OTP_LOCK | CONFIG_SR1_LOCK)) |
            CONFIG_ECC_ENABLE | CONFIG_BUFFER_MODE);
        if (status.configurationRaw != desired)
        {
            WriteRegisterRaw(REG_CONFIGURATION, desired);
            delayMicroseconds(10);
        }

        if (!ReadStatusRaw(&status) || status.otpMode || !status.eccEnabled || !status.bufferReadMode)
            return Fail(BSP::W25n01::Error::ConfigurationVerifyFailed, true);
        return true;
    }

    bool RequireReady()
    {
        if (!s_initialized || !s_ready)
            return Fail(BSP::W25n01::Error::NotInitialized);
        return true;
    }

    bool RequireArrayWritable()
    {
        if (!RequireReady())
            return false;
        BSP::W25n01::Status status;
        if (!ReadStatusRaw(&status))
            return Fail(BSP::W25n01::Error::NotInitialized, true);
        if (status.arrayWriteProtected)
            return Fail(BSP::W25n01::Error::ArrayWriteProtected);
        return true;
    }

    bool WriteEnable()
    {
        Select();
        s_spi.transfer(CMD_WRITE_ENABLE);
        Deselect();

        BSP::W25n01::Status status;
        if (!ReadStatusRaw(&status) || !status.writeEnableLatch)
            return Fail(BSP::W25n01::Error::WriteEnableFailed);
        return true;
    }

    void SendPageAddressCommand(uint8_t command, uint16_t page)
    {
        Select();
        s_spi.transfer(command);
        s_spi.transfer(0x00); // 行地址前的8个dummy clocks。
        s_spi.transfer(static_cast<uint8_t>(page >> 8));
        s_spi.transfer(static_cast<uint8_t>(page & 0xFF));
        Deselect();
    }

    bool LoadPageToBuffer(uint16_t page, BSP::W25n01::Status *statusOut)
    {
        SendPageAddressCommand(CMD_PAGE_DATA_READ, page);
        BSP::W25n01::Status status;
        if (!WaitReadyRaw(PAGE_READ_TIMEOUT_MS, &status))
            return Fail(BSP::W25n01::Error::BusyTimeout);
        s_diagnostics.lastPage = page;
        if (statusOut)
            *statusOut = status;
        return true;
    }

    void ReadBuffer(uint16_t column, uint8_t *data, size_t length)
    {
        Select();
        s_spi.transfer(CMD_READ_DATA);
        s_spi.transfer(static_cast<uint8_t>(column >> 8));
        s_spi.transfer(static_cast<uint8_t>(column & 0xFF));
        s_spi.transfer(0x00); // Buffer Read模式要求8个dummy clocks。
        s_spi.transferBytes(nullptr, data, static_cast<uint32_t>(length));
        Deselect();
    }
}

namespace BSP::W25n01
{
    bool Begin(uint32_t frequencyHz)
    {
        if (frequencyHz == 0)
            return Fail(Error::InvalidArgument);
        if (s_initialized && s_ready && frequencyHz == s_frequency_hz)
            return true;

        s_initialized = false;
        s_ready = false;
        s_last_error = Error::NotInitialized;
        s_diagnostics = {};
        s_frequency_hz = frequencyHz;
        s_settings = SPISettings(frequencyHz, MSBFIRST, SPI_MODE0);

        pinMode(BSP::Pins::NAND_CS, OUTPUT);
        digitalWrite(BSP::Pins::NAND_CS, HIGH);
        s_spi.begin(BSP::Pins::NAND_SCLK,
                    BSP::Pins::NAND_MISO,
                    BSP::Pins::NAND_MOSI,
                    BSP::Pins::NAND_CS);
        s_bus_started = true;
        s_diagnostics.busStarted = true;
        s_diagnostics.spiFrequencyHz = frequencyHz;

        // ESP软件重启可能发生在NAND自定时操作期间；先等它自然结束，禁止用RESET截断擦写。
        if (!WaitReadyRaw(RESET_READY_TIMEOUT_MS))
            return Fail(Error::BusyTimeout);

        Select();
        s_spi.transfer(CMD_RESET);
        Deselect();
        delay(1); // tRST最大500us，留出裕量后再发下一条指令。

        JedecId id;
        if (!ReadJedecIdRaw(&id))
            return Fail(Error::DeviceNotFound);
        if (!id.matchesW25n01gv())
        {
            const bool floating = (id.manufacturer == 0x00 && id.deviceHigh == 0x00 && id.deviceLow == 0x00) ||
                                  (id.manufacturer == 0xFF && id.deviceHigh == 0xFF && id.deviceLow == 0xFF);
            return Fail(floating ? Error::DeviceNotFound : Error::IdentityMismatch);
        }
        if (!WaitReadyRaw(RESET_READY_TIMEOUT_MS))
            return Fail(Error::BusyTimeout);

        s_initialized = true;
        s_ready = true;
        if (!ConfigureReadMode())
            return false;

        Status status;
        if (!ReadStatusRaw(&status))
            return Fail(Error::ConfigurationVerifyFailed, true);
        // 驱动对外承诺初始化后处于保护态；若SR1被永久锁成可写，必须拒绝进入ready状态。
        if (!status.arrayWriteProtected && !SetArrayWriteProtected(true))
            return Fail(Error::ConfigurationVerifyFailed, true);
        s_diagnostics.initialized = true;
        ClearError();
        return true;
    }

    bool Reset()
    {
        if (!RequireReady())
            return false;
        if (!WaitReadyRaw(RESET_READY_TIMEOUT_MS))
            return Fail(Error::BusyTimeout);

        Select();
        s_spi.transfer(CMD_RESET);
        Deselect();
        delay(1);
        if (!WaitReadyRaw(RESET_READY_TIMEOUT_MS))
            return Fail(Error::BusyTimeout, true);

        JedecId id;
        if (!ReadJedecIdRaw(&id) || !id.matchesW25n01gv())
            return Fail(Error::IdentityMismatch, true);
        if (!ConfigureReadMode())
            return false;
        Status status;
        if (!ReadStatusRaw(&status) ||
            (!status.arrayWriteProtected && !SetArrayWriteProtected(true)))
        {
            return Fail(Error::ConfigurationVerifyFailed, true);
        }
        ClearError();
        return true;
    }

    bool IsReady()
    {
        return s_initialized && s_ready;
    }

    bool IsPresent()
    {
        if (!s_bus_started)
            return false;
        JedecId id;
        return ReadJedecIdRaw(&id) && id.matchesW25n01gv();
    }

    SPIClass *Bus() { return s_bus_started ? &s_spi : nullptr; }
    uint32_t FrequencyHz() { return s_frequency_hz; }
    Error LastError() { return s_last_error; }

    const char *ErrorName(Error error)
    {
        switch (error)
        {
        case Error::None: return "无";
        case Error::InvalidArgument: return "参数无效";
        case Error::NotInitialized: return "尚未初始化";
        case Error::DeviceNotFound: return "未检测到器件";
        case Error::IdentityMismatch: return "器件型号不匹配";
        case Error::BusyTimeout: return "等待芯片空闲超时";
        case Error::ConfigurationVerifyFailed: return "配置回读失败";
        case Error::ArrayWriteProtected: return "阵列仍受写保护";
        case Error::WriteEnableFailed: return "写使能失败";
        case Error::ProgramFailed: return "页编程失败";
        case Error::EraseFailed: return "块擦除失败";
        case Error::EccUncorrectable: return "ECC检测到不可纠正错误";
        default: return "未知错误";
        }
    }

    const char *EccStatusName(EccStatus status)
    {
        switch (status)
        {
        case EccStatus::Clean: return "无纠错";
        case EccStatus::CorrectedOneBit: return "已纠正1位";
        case EccStatus::Uncorrectable: return "单页不可纠正";
        case EccStatus::MultipleUncorrectable: return "多页不可纠正";
        case EccStatus::Unknown: return "未知";
        default: return "未知";
        }
    }

    bool GetDiagnostics(Diagnostics *out)
    {
        if (!out)
            return false;
        *out = s_diagnostics;
        return true;
    }

    bool ReadJedecId(JedecId *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument);
        if (!RequireReady())
            return false;
        if (!ReadJedecIdRaw(out))
            return Fail(Error::DeviceNotFound, true);
        if (!out->matchesW25n01gv())
            return Fail(Error::IdentityMismatch, true);
        ClearError();
        return true;
    }

    bool ReadStatus(Status *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument);
        if (!RequireReady())
            return false;
        if (!ReadStatusRaw(out))
            return Fail(Error::NotInitialized, true);
        ClearError();
        return true;
    }

    bool WaitReady(uint32_t timeoutMs)
    {
        if (!RequireReady())
            return false;
        if (timeoutMs == 0)
            return Fail(Error::InvalidArgument);
        if (!WaitReadyRaw(timeoutMs))
            return Fail(Error::BusyTimeout);
        ClearError();
        return true;
    }

    bool SetArrayWriteProtected(bool protectedState)
    {
        if (!RequireReady())
            return false;
        if (!WaitReadyRaw(RESET_READY_TIMEOUT_MS))
            return Fail(Error::BusyTimeout);

        Status status;
        if (!ReadStatusRaw(&status))
            return Fail(Error::ConfigurationVerifyFailed, true);
        const uint8_t desired = protectedState
            ? static_cast<uint8_t>(status.protectionRaw | PROTECTION_BLOCK_MASK)
            : static_cast<uint8_t>(status.protectionRaw & ~PROTECTION_BLOCK_MASK);
        if (desired != status.protectionRaw)
        {
            WriteRegisterRaw(REG_PROTECTION, desired);
            delayMicroseconds(10);
        }
        if (!ReadStatusRaw(&status) || status.arrayWriteProtected != protectedState)
            return Fail(Error::ConfigurationVerifyFailed);
        ClearError();
        return true;
    }

    bool ReadPage(uint16_t page,
                  uint16_t column,
                  uint8_t *data,
                  size_t length,
                  EccStatus *eccStatus)
    {
        if (!data || length == 0 || column >= PAGE_DATA_SIZE || length > PAGE_DATA_SIZE - column)
            return Fail(Error::InvalidArgument);
        if (eccStatus)
            *eccStatus = EccStatus::Unknown;
        if (!RequireReady())
            return false;

        Status status;
        if (!LoadPageToBuffer(page, &status))
            return false;
        if (eccStatus)
            *eccStatus = status.ecc;
        if (status.ecc == EccStatus::Uncorrectable ||
            status.ecc == EccStatus::MultipleUncorrectable)
        {
            memset(data, 0, length);
            return Fail(Error::EccUncorrectable);
        }

        ReadBuffer(column, data, length);
        ClearError();
        return true;
    }

    bool ProgramPage(uint16_t page, const uint8_t *data, size_t length)
    {
        if (!data || length != PAGE_DATA_SIZE)
            return Fail(Error::InvalidArgument);
        if (!RequireArrayWritable())
            return false;
        if (!WaitReadyRaw(RESET_READY_TIMEOUT_MS))
            return Fail(Error::BusyTimeout);
        if (!WriteEnable())
            return false;

        Select();
        s_spi.transfer(CMD_PROGRAM_DATA_LOAD);
        s_spi.transfer(0x00);
        s_spi.transfer(0x00);
        s_spi.writeBytes(data, PAGE_DATA_SIZE);
        Deselect();

        SendPageAddressCommand(CMD_PROGRAM_EXECUTE, page);
        Status status;
        if (!WaitReadyRaw(PAGE_PROGRAM_TIMEOUT_MS, &status))
            return Fail(Error::BusyTimeout);
        s_diagnostics.lastPage = page;
        s_diagnostics.lastBlock = page / PAGES_PER_BLOCK;
        if (status.programFailed)
            return Fail(Error::ProgramFailed);
        ClearError();
        return true;
    }

    bool EraseBlock(uint16_t block)
    {
        if (block >= BLOCK_COUNT)
            return Fail(Error::InvalidArgument);
        if (!RequireArrayWritable())
            return false;
        if (!WaitReadyRaw(RESET_READY_TIMEOUT_MS))
            return Fail(Error::BusyTimeout);
        if (!WriteEnable())
            return false;

        const uint16_t first_page = static_cast<uint16_t>(block * PAGES_PER_BLOCK);
        SendPageAddressCommand(CMD_BLOCK_ERASE, first_page);
        Status status;
        if (!WaitReadyRaw(BLOCK_ERASE_TIMEOUT_MS, &status))
            return Fail(Error::BusyTimeout);
        s_diagnostics.lastBlock = block;
        s_diagnostics.lastPage = first_page;
        if (status.eraseFailed)
            return Fail(Error::EraseFailed);
        ClearError();
        return true;
    }

    bool ReadFactoryBadBlockMarker(uint16_t block, bool *isBad)
    {
        if (!isBad || block >= BLOCK_COUNT)
            return Fail(Error::InvalidArgument);
        if (!RequireReady())
            return false;

        const uint16_t page = static_cast<uint16_t>(block * PAGES_PER_BLOCK);
        Status status;
        if (!LoadPageToBuffer(page, &status))
            return false;

        uint8_t markers[3] = {};
        ReadBuffer(0, &markers[0], 1);
        ReadBuffer(PAGE_DATA_SIZE, &markers[1], 2);
        *isBad = markers[0] != 0xFF || markers[1] != 0xFF || markers[2] != 0xFF;
        s_diagnostics.lastBlock = block;
        ClearError();
        return true;
    }

    bool ReadBadBlockLut(BadBlockLink *entries, size_t entryCount)
    {
        if (!entries || entryCount < BBM_LUT_ENTRY_COUNT)
            return Fail(Error::InvalidArgument);
        if (!RequireReady())
            return false;
        if (!WaitReadyRaw(RESET_READY_TIMEOUT_MS))
            return Fail(Error::BusyTimeout);

        uint8_t raw[BBM_LUT_ENTRY_COUNT * 4] = {};
        Select();
        s_spi.transfer(CMD_READ_BBM_LUT);
        s_spi.transfer(0x00);
        s_spi.transferBytes(nullptr, raw, sizeof(raw));
        Deselect();

        for (size_t index = 0; index < BBM_LUT_ENTRY_COUNT; ++index)
        {
            const uint16_t lba = static_cast<uint16_t>(raw[index * 4] << 8) | raw[index * 4 + 1];
            const uint16_t pba = static_cast<uint16_t>(raw[index * 4 + 2] << 8) | raw[index * 4 + 3];
            entries[index].enabled = (lba & 0x8000) != 0;
            entries[index].invalid = (lba & 0x4000) != 0;
            entries[index].logicalBlock = lba & 0x03FF;
            entries[index].physicalBlock = pba & 0x03FF;
        }
        ClearError();
        return true;
    }

    void PrintDiagnostics()
    {
        const Diagnostics snapshot = s_diagnostics;
        if (IsReady())
        {
            Serial.printf("[BSP][外挂Flash] W25N01GV初始化成功：JEDEC=%02X %02X %02X，容量=128MiB，SPI3=%luMHz，ECC=%s，页缓存=%s，阵列写保护=%s。\n",
                          snapshot.id.manufacturer,
                          snapshot.id.deviceHigh,
                          snapshot.id.deviceLow,
                          static_cast<unsigned long>(snapshot.spiFrequencyHz / 1000000UL),
                          snapshot.status.eccEnabled ? "开启" : "关闭",
                          snapshot.status.bufferReadMode ? "开启" : "关闭",
                          snapshot.status.arrayWriteProtected ? "开启" : "关闭");
            return;
        }

        Serial.printf("[BSP][外挂Flash] W25N01GV初始化失败：错误=%s，JEDEC=%02X %02X %02X，SPI3=%luMHz；未执行擦除或编程。\n",
                      ErrorName(LastError()),
                      snapshot.id.manufacturer,
                      snapshot.id.deviceHigh,
                      snapshot.id.deviceLow,
                      static_cast<unsigned long>(snapshot.spiFrequencyHz / 1000000UL));
    }
}
