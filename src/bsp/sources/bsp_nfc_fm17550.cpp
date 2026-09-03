/*
【模块职责】FM17550 UART 寄存器级实体读卡驱动。
【协议依据】FM17550 技术手册规定 UART 为 8N1、默认 9600bps，地址字节 bit7 表示读写、bit5:0
为寄存器地址；写访问时芯片 TX 会回送地址字节。驱动在每次写操作后消费该回送字节，避免污染
下一次寄存器读取。
*/
#include "bsp/bsp_nfc_fm17550.h"

#include "bsp/bsp_pins.h"

#include <HardwareSerial.h>

namespace
{
    // 当前固件的日志走 USB CDC，UART0 因而可以独占 GPIO43/44 连接 FM17550。
    HardwareSerial s_nfcUart(0);

    bool s_ready = false;
    bool s_cardSelected = false;
    uint32_t s_uartBaud = 0;

    constexpr uint32_t kDefaultBaud = 9600;
    /*
     * 首轮实板在 end()/begin() 切换高速 UART 时丢失链路。现在改为原地更新 UART 分频器，不再
     * 关闭 UART0 或扰动 GPIO43/44，因此恢复 460800bps 工作速率。相对默认 9600bps，寄存器密集型
     * 的整卡读取可大幅减少主机侧串口耗时；若高速确认失败，初始化流程仍会自动退回 9600bps。
     */
    constexpr uint32_t kWorkingBaud = 460800;
    constexpr uint8_t kWorkingBaudReg = 0x3A;
    constexpr uint8_t kExpectedVersion = 0xA1;
    constexpr uint32_t kUartByteTimeoutMs = 8;

    enum Register : uint8_t
    {
        CommandReg = 0x01,
        ComIrqReg = 0x04,
        DivIrqReg = 0x05,
        ErrorReg = 0x06,
        Status2Reg = 0x08,
        FIFODataReg = 0x09,
        FIFOLevelReg = 0x0A,
        ControlReg = 0x0C,
        BitFramingReg = 0x0D,
        CollReg = 0x0E,
        ModeReg = 0x11,
        TxModeReg = 0x12,
        RxModeReg = 0x13,
        TxControlReg = 0x14,
        TxAutoReg = 0x15,
        SerialSpeedReg = 0x1F,
        CRCResultMSBReg = 0x21,
        CRCResultLSBReg = 0x22,
        RFCfgReg = 0x26,
        TModeReg = 0x2A,
        TPrescalerReg = 0x2B,
        TReloadHiReg = 0x2C,
        TReloadLoReg = 0x2D,
        VersionReg = 0x37,
    };

    enum Command : uint8_t
    {
        Idle = 0x00,
        CalcCRC = 0x03,
        Transceive = 0x0C,
        Authent = 0x0E,
        SoftReset = 0x0F,
    };

    enum class Result : uint8_t
    {
        Ok,
        Timeout,
        Collision,
        TransportError,
        ProtocolError,
        NoRoom,
        CrcError,
    };

    constexpr uint8_t kPiccWupa = 0x52;
    constexpr uint8_t kPiccCascadeTag = 0x88;
    constexpr uint8_t kPiccSelectCl1 = 0x93;
    constexpr uint8_t kPiccSelectCl2 = 0x95;
    constexpr uint8_t kPiccSelectCl3 = 0x97;
    constexpr uint8_t kPiccMifareKeyA = 0x60;
    constexpr uint8_t kPiccRead = 0x30;

    void clearRx();

    void startUart(uint32_t baud)
    {
        if (s_uartBaud != 0)
        {
            /*
             * FM17550 写 SerialSpeedReg 后立即换速。关闭再重开 UART0 会让 TX 脚短暂退出外设控制，
             * 首轮实板因此在 460800bps 切换后失联；原地更新分频器可保持引脚与 RX 缓冲所有权稳定。
             */
            clearRx();
            s_nfcUart.updateBaudRate(baud);
            s_uartBaud = baud;
            vTaskDelay(pdMS_TO_TICKS(3));
            clearRx();
            return;
        }

        // 扩展板可能未安装；RX 内部上拉让悬空接口保持 UART 空闲高电平，减少随机伪字节。
        pinMode(BSP::Pins::NFC_UART_RX, INPUT_PULLUP);
        s_nfcUart.setRxBufferSize(256);
        s_nfcUart.begin(baud,
                        SERIAL_8N1,
                        BSP::Pins::NFC_UART_RX,
                        BSP::Pins::NFC_UART_TX);
        s_uartBaud = baud;
        vTaskDelay(pdMS_TO_TICKS(3));
        while (s_nfcUart.available() > 0)
            (void)s_nfcUart.read();
    }

    void clearRx()
    {
        while (s_nfcUart.available() > 0)
            (void)s_nfcUart.read();
    }

    bool readUartByte(uint8_t &value, uint32_t timeoutMs = kUartByteTimeoutMs)
    {
        const uint32_t start = millis();
        while ((uint32_t)(millis() - start) < timeoutMs)
        {
            if (s_nfcUart.available() > 0)
            {
                value = (uint8_t)s_nfcUart.read();
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return false;
    }

    bool writeRegister(uint8_t reg, uint8_t value)
    {
        if (s_uartBaud == 0)
            return false;

        const uint8_t address = reg & 0x3F;
        clearRx();

        // UART 写帧必须连续发送“写地址 + 数据”；芯片随后从 TX 回送地址字节。
        s_nfcUart.write(address);
        s_nfcUart.write(value);
        s_nfcUart.flush();

        uint8_t echoedAddress = 0;
        return readUartByte(echoedAddress) && echoedAddress == address;
    }

    bool readRegister(uint8_t reg, uint8_t &value)
    {
        if (s_uartBaud == 0)
            return false;

        clearRx();
        s_nfcUart.write((uint8_t)(0x80 | (reg & 0x3F)));
        s_nfcUart.flush();
        return readUartByte(value);
    }

    bool setRegisterBits(uint8_t reg, uint8_t mask)
    {
        uint8_t value = 0;
        return readRegister(reg, value) && writeRegister(reg, value | mask);
    }

    bool clearRegisterBits(uint8_t reg, uint8_t mask)
    {
        uint8_t value = 0;
        return readRegister(reg, value) && writeRegister(reg, value & (uint8_t)~mask);
    }

    bool probeAtBaud(uint32_t baud)
    {
        startUart(baud);

        // 连读两次可排除扩展板缺席时 RX 浮动恰好产生 A1h 的极低概率假阳性。
        uint8_t version1 = 0;
        uint8_t version2 = 0;
        return readRegister(VersionReg, version1) &&
               readRegister(VersionReg, version2) &&
               version1 == kExpectedVersion &&
               version2 == kExpectedVersion;
    }

    bool findCurrentBaud()
    {
        /*
         * MCU 单独复位时扩展板通常仍保持工作速率，优先以该速率探测可避免先用 9600 的慢脉冲
         * 干扰一个仍在高速接收的芯片。扩展板真正掉电后才会回到默认 9600。
         */
        if (probeAtBaud(kWorkingBaud))
            return true;
        return probeAtBaud(kDefaultBaud);
    }

    bool resetToDefaultBaud()
    {
        if (!findCurrentBaud())
            return false;

        // SoftReset 会把 SerialSpeedReg 恢复到 9600bps；复位动作可能使最后一次写回送缺失，故不以回送判失败。
        (void)writeRegister(CommandReg, SoftReset);
        vTaskDelay(pdMS_TO_TICKS(8));
        return probeAtBaud(kDefaultBaud);
    }

    bool switchToWorkingBaud()
    {
        /*
         * SerialSpeedReg 写入后芯片立即切换速率，最后的地址回送可能恰好跨过切换边界。
         * 写回送只用于尽量清空旧速率字节，最终结果必须在新速率下连续读取 A1h 确认。
         */
        (void)writeRegister(SerialSpeedReg, kWorkingBaudReg);

        vTaskDelay(pdMS_TO_TICKS(2));
        for (uint8_t attempt = 0; attempt < 2; ++attempt)
        {
            if (probeAtBaud(kWorkingBaud))
                return true;
            vTaskDelay(pdMS_TO_TICKS(3));
        }

        if (probeAtBaud(kDefaultBaud))
        {
            Serial.printf("[BSP][NFC] FM17550 未切入 %lu bps，本次退回 9600bps 继续工作。\n",
                          (unsigned long)kWorkingBaud);
            return true;
        }

        /*
         * 芯片若已换速但首轮高速回读异常，直接尝试默认速率会继续失联。最后在工作速率发送
         * SoftReset，再回到默认 9600bps；即使该写入未命中，随后默认速率探测仍是安全的。
         */
        startUart(kWorkingBaud);
        (void)writeRegister(CommandReg, SoftReset);
        vTaskDelay(pdMS_TO_TICKS(8));
        if (probeAtBaud(kDefaultBaud))
        {
            Serial.println("[BSP][NFC] FM17550 高速确认失败，已软复位并退回 9600bps。");
            return true;
        }
        return false;
    }

    bool calculateCrc(const uint8_t *data, size_t length, uint8_t result[2])
    {
        if (!writeRegister(CommandReg, Idle) ||
            !writeRegister(DivIrqReg, 0x04) ||
            !writeRegister(FIFOLevelReg, 0x80))
            return false;

        for (size_t i = 0; i < length; ++i)
        {
            if (!writeRegister(FIFODataReg, data[i]))
                return false;
        }

        if (!writeRegister(CommandReg, CalcCRC))
            return false;

        const uint32_t start = millis();
        while ((uint32_t)(millis() - start) < 40)
        {
            uint8_t irq = 0;
            if (!readRegister(DivIrqReg, irq))
                return false;
            if ((irq & 0x04) != 0)
            {
                // ISO14443A 在线上传输 CRC 低字节在前。
                return readRegister(CRCResultLSBReg, result[0]) &&
                       readRegister(CRCResultMSBReg, result[1]) &&
                       writeRegister(CommandReg, Idle);
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        (void)writeRegister(CommandReg, Idle);
        return false;
    }

    Result communicate(uint8_t command,
                       uint8_t waitIrq,
                       const uint8_t *sendData,
                       size_t sendLength,
                       uint8_t *backData,
                       size_t &backLength,
                       uint8_t txLastBits,
                       uint8_t &rxValidBits,
                       uint16_t timeoutMs)
    {
        if (!writeRegister(CommandReg, Idle) ||
            !writeRegister(ComIrqReg, 0x7F) ||
            !writeRegister(FIFOLevelReg, 0x80))
            return Result::TransportError;

        for (size_t i = 0; i < sendLength; ++i)
        {
            if (!writeRegister(FIFODataReg, sendData[i]))
                return Result::TransportError;
        }

        const uint8_t framing = (uint8_t)(txLastBits & 0x07);
        if (!writeRegister(BitFramingReg, framing) || !writeRegister(CommandReg, command))
            return Result::TransportError;

        if (command == Transceive && !setRegisterBits(BitFramingReg, 0x80))
            return Result::TransportError;

        const uint32_t start = millis();
        uint8_t irq = 0;
        bool timerExpired = false;
        while ((uint32_t)(millis() - start) < timeoutMs)
        {
            if (!readRegister(ComIrqReg, irq))
            {
                (void)clearRegisterBits(BitFramingReg, 0x80);
                (void)writeRegister(CommandReg, Idle);
                return Result::TransportError;
            }
            if ((irq & waitIrq) != 0)
                break;
            if ((irq & 0x01) != 0)
            {
                timerExpired = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        (void)clearRegisterBits(BitFramingReg, 0x80);
        if (timerExpired || (irq & waitIrq) == 0)
        {
            (void)writeRegister(CommandReg, Idle);
            return Result::Timeout;
        }

        uint8_t error = 0;
        if (!readRegister(ErrorReg, error))
            return Result::TransportError;
        if ((error & 0x08) != 0)
            return Result::Collision;
        if ((error & 0x13) != 0)
            return Result::ProtocolError;

        if (backData == nullptr)
            return Result::Ok;

        uint8_t fifoLevel = 0;
        uint8_t control = 0;
        if (!readRegister(FIFOLevelReg, fifoLevel) || !readRegister(ControlReg, control))
            return Result::TransportError;
        if (fifoLevel > backLength)
            return Result::NoRoom;

        backLength = fifoLevel;
        rxValidBits = control & 0x07;
        for (size_t i = 0; i < backLength; ++i)
        {
            if (!readRegister(FIFODataReg, backData[i]))
                return Result::TransportError;
        }
        return Result::Ok;
    }

    bool transceiveWithCrc(const uint8_t *commandData,
                           size_t commandLength,
                           uint8_t *response,
                           size_t &responseLength,
                           uint16_t timeoutMs)
    {
        if (commandLength > 16)
            return false;

        uint8_t frame[18] = {};
        memcpy(frame, commandData, commandLength);
        if (!calculateCrc(commandData, commandLength, &frame[commandLength]))
            return false;

        uint8_t validBits = 0;
        return communicate(Transceive,
                           0x30,
                           frame,
                           commandLength + 2,
                           response,
                           responseLength,
                           0,
                           validBits,
                           timeoutMs) == Result::Ok &&
               validBits == 0;
    }

    void stopCrypto1()
    {
        (void)clearRegisterBits(Status2Reg, 0x08);
    }

    void haltSelectedCard()
    {
        if (!s_cardSelected)
            return;

        /*
         * HLTA 的正常结果就是卡片不再回答，所以底层会报告超时；这里故意忽略返回值。
         * MIFARE 已认证时必须先发加密 HLTA、再关闭 Crypto1，顺序不能反过来。
         */
        const uint8_t halt[] = {0x50, 0x00};
        uint8_t ignored[1] = {};
        size_t ignoredLength = sizeof(ignored);
        (void)transceiveWithCrc(halt, sizeof(halt), ignored, ignoredLength, 40);
        s_cardSelected = false;
    }

    bool selectCascadeLevel(uint8_t selectCode,
                            uint8_t cascadeData[5],
                            uint8_t &sak,
                            uint16_t timeoutMs)
    {
        uint8_t anticollision[] = {selectCode, 0x20};
        size_t anticollisionLength = 5;
        uint8_t validBits = 0;

        // ValuesAfterColl=0，保证发生冲突时 FIFO 不混入冲突位之后的不确定数据。
        if (!clearRegisterBits(CollReg, 0x80) ||
            communicate(Transceive,
                        0x30,
                        anticollision,
                        sizeof(anticollision),
                        cascadeData,
                        anticollisionLength,
                        0,
                        validBits,
                        timeoutMs) != Result::Ok ||
            anticollisionLength != 5 || validBits != 0)
            return false;

        const uint8_t bcc = cascadeData[0] ^ cascadeData[1] ^ cascadeData[2] ^ cascadeData[3];
        if (bcc != cascadeData[4])
            return false;

        uint8_t selectFrame[9] = {selectCode, 0x70};
        memcpy(&selectFrame[2], cascadeData, 5);
        if (!calculateCrc(selectFrame, 7, &selectFrame[7]))
            return false;

        uint8_t response[3] = {};
        size_t responseLength = sizeof(response);
        validBits = 0;
        if (communicate(Transceive,
                        0x30,
                        selectFrame,
                        sizeof(selectFrame),
                        response,
                        responseLength,
                        0,
                        validBits,
                        timeoutMs) != Result::Ok ||
            responseLength != 3 || validBits != 0)
            return false;

        uint8_t crc[2] = {};
        if (!calculateCrc(response, 1, crc) || crc[0] != response[1] || crc[1] != response[2])
            return false;

        sak = response[0];
        return true;
    }

    bool initializeReaderRegisters()
    {
        // 25us 计时步进、约 25ms 自动超时，避免无卡/认证失败时状态机无限等待。
        return writeRegister(TModeReg, 0x80) &&
               writeRegister(TPrescalerReg, 0xA9) &&
               writeRegister(TReloadHiReg, 0x03) &&
               writeRegister(TReloadLoReg, 0xE8) &&
               writeRegister(TxModeReg, 0x00) &&
               writeRegister(RxModeReg, 0x00) &&
               writeRegister(ModeReg, 0x3D) &&
               writeRegister(TxAutoReg, 0x40) &&
               writeRegister(RFCfgReg, 0x48) &&
               setRegisterBits(TxControlReg, 0x03);
    }
}

namespace BSP::NfcFm17550
{
    bool IsReady()
    {
        return s_ready;
    }

    void MarkOffline()
    {
        s_ready = false;
    }

    bool HealthCheck()
    {
        uint8_t version = 0;
        const bool ok = readRegister(VersionReg, version) && version == kExpectedVersion;
        if (!ok)
            s_ready = false;
        return ok;
    }

    bool Reinitialize(const char *reason, bool longBootWait)
    {
        s_ready = false;
        s_cardSelected = false;
        Serial.printf("[BSP][NFC] %s，正在初始化 FM17550 UART。\n",
                      reason ? reason : "收到恢复请求");

        if (longBootWait)
            vTaskDelay(pdMS_TO_TICKS(80));

        if (!resetToDefaultBaud())
        {
            Serial.println("[BSP][NFC] 未读到 FM17550 的 A1h 版本值；扩展板可能未安装、未供电或串口接线异常。");
            return false;
        }

        if (!switchToWorkingBaud())
        {
            Serial.println("[BSP][NFC] FM17550 UART 波特率配置失败。");
            return false;
        }

        if (!initializeReaderRegisters() || !HealthCheck())
        {
            Serial.println("[BSP][NFC] FM17550 寄存器初始化或回读校验失败。");
            return false;
        }

        s_ready = true;
        Serial.printf("[BSP][NFC] FM17550 在线，UART=%lu，射频场已开启。\n",
                      (unsigned long)s_uartBaud);
        return true;
    }

    bool Begin(bool longBootWait)
    {
        return Reinitialize("开机初始化", longBootWait);
    }

    bool ReadPassiveTarget(uint8_t *uid,
                           uint8_t *uidLen,
                           uint8_t *sak,
                           uint16_t timeoutMs)
    {
        if (!s_ready || uid == nullptr || uidLen == nullptr || sak == nullptr)
            return false;

        haltSelectedCard();
        stopCrypto1();

        // WUPA 同时唤醒 HALT 中的卡，适合后台连续扫描；请求帧只发送低 7 位。
        uint8_t request = kPiccWupa;
        uint8_t atqa[2] = {};
        size_t atqaLength = sizeof(atqa);
        uint8_t validBits = 0;
        if (communicate(Transceive,
                        0x30,
                        &request,
                        1,
                        atqa,
                        atqaLength,
                        7,
                        validBits,
                        timeoutMs) != Result::Ok ||
            atqaLength != 2 || validBits != 0)
            return false;

        s_cardSelected = false;

        static constexpr uint8_t kSelectCodes[] = {kPiccSelectCl1, kPiccSelectCl2, kPiccSelectCl3};
        uint8_t length = 0;
        uint8_t finalSak = 0;

        for (uint8_t level = 0; level < 3; ++level)
        {
            uint8_t cascadeData[5] = {};
            if (!selectCascadeLevel(kSelectCodes[level], cascadeData, finalSak, timeoutMs))
                return false;

            const bool hasCascadeTag = cascadeData[0] == kPiccCascadeTag;
            const uint8_t copyOffset = hasCascadeTag ? 1 : 0;
            const uint8_t copyLength = hasCascadeTag ? 3 : 4;
            if ((uint8_t)(length + copyLength) > 10)
                return false;

            memcpy(uid + length, cascadeData + copyOffset, copyLength);
            length += copyLength;

            const bool moreLevels = (finalSak & 0x04) != 0;
            if (!moreLevels)
            {
                *uidLen = length;
                *sak = finalSak;
                s_cardSelected = length == 4 || length == 7 || length == 10;
                return s_cardSelected;
            }
            if (!hasCascadeTag)
                return false;
        }
        return false;
    }

    bool MifareAuth(const uint8_t *uid,
                    uint8_t uidLen,
                    uint8_t block,
                    const uint8_t key[6])
    {
        if (!s_ready || uid == nullptr || key == nullptr || uidLen < 4)
            return false;

        uint8_t frame[12] = {kPiccMifareKeyA, block};
        memcpy(&frame[2], key, 6);
        memcpy(&frame[8], uid + uidLen - 4, 4);

        size_t ignoredLength = 0;
        uint8_t validBits = 0;
        const Result result = communicate(Authent,
                                          0x10,
                                          frame,
                                          sizeof(frame),
                                          nullptr,
                                          ignoredLength,
                                          0,
                                          validBits,
                                          60);
        uint8_t status2 = 0;
        return result == Result::Ok &&
               readRegister(Status2Reg, status2) &&
               (status2 & 0x08) != 0;
    }

    bool MifareReadBlock(uint8_t block, uint8_t data[16])
    {
        if (!s_ready || data == nullptr)
            return false;

        const uint8_t command[] = {kPiccRead, block};
        uint8_t response[18] = {};
        size_t responseLength = sizeof(response);
        if (!transceiveWithCrc(command, sizeof(command), response, responseLength, 60) || responseLength != 18)
            return false;

        uint8_t crc[2] = {};
        if (!calculateCrc(response, 16, crc) || crc[0] != response[16] || crc[1] != response[17])
            return false;

        memcpy(data, response, 16);
        return true;
    }

    bool NtagReadFourPages(uint8_t startPage, uint8_t data[16])
    {
        // MIFARE Ultralight/NTAG 的 READ 命令一次返回起始页起连续四页，共 16 字节。
        return MifareReadBlock(startPage, data);
    }

    void Sleep()
    {
        if (s_uartBaud == 0)
            return;

        (void)clearRegisterBits(TxControlReg, 0x03);
        (void)writeRegister(CommandReg, 0x10);
        s_ready = false;
        s_cardSelected = false;
    }

    void Wakeup()
    {
        if (s_uartBaud == 0)
            startUart(kDefaultBaud);

        // 手册要求 UART 模式先发送 55h 启动晶振，再轮询 0 地址，最后清除 PowerDown。
        clearRx();
        s_nfcUart.write((uint8_t)0x55);
        s_nfcUart.flush();
        vTaskDelay(pdMS_TO_TICKS(3));

        uint8_t page = 0;
        for (uint8_t i = 0; i < 12; ++i)
        {
            if (readRegister(0x00, page))
                break;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        (void)writeRegister(CommandReg, Idle);
        s_ready = false;
        s_cardSelected = false;
    }
}
