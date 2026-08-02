/*
【模块职责】PN532 NFC 板级驱动实现。这里直接接触 SPI、Adafruit_PN532 和 RESET/SS 引脚。
*/
#include "bsp/bsp_nfc_pn532.h"
#include "bsp/bsp_pins.h"
#include <SPI.h>
#include <Adafruit_PN532.h>
#include "driver/gpio.h"

namespace
{
    // PN532 使用独立 HSPI，避免和屏幕 SPI 互相影响。
    SPIClass s_nfcSpi(HSPI);

    // Adafruit_PN532 对象负责常规读卡命令；原始帧能力在本文件下方手写。
    Adafruit_PN532 s_nfc(BSP::Pins::NFC_SS, &s_nfcSpi);

    // PN532 SPI 模式要求低速、LSB first、MODE0；原始帧收发必须使用同一组设置。
    static const SPISettings kPn532SpiSettings(1000000, LSBFIRST, SPI_MODE0);

    // 记录 PN532 当前是否已经通过固件版本读取和 SAMConfig。
    bool s_ready = false;
}

namespace BSP::NfcPn532
{
    // 【函数说明】返回 PN532 是否处于可执行读卡命令的状态。
    bool IsReady()
    {
        return s_ready;
    }

    // 【函数说明】把 PN532 标记为离线，等待上层决定何时重新初始化。
    void MarkOffline()
    {
        s_ready = false;
    }

    // 【函数说明】对 PN532 执行硬复位，并整理 SS 到空闲高电平。
    void Reset()
    {
        // RESET 拉低再拉高，让 PN532 从硬件层面重新启动。
        digitalWrite(Pins::NFC_RESET, LOW);
        vTaskDelay(pdMS_TO_TICKS(50));
        digitalWrite(Pins::NFC_RESET, HIGH);
        vTaskDelay(pdMS_TO_TICKS(50));

        // 轻触 SS，确保 SPI 片选状态回到已释放，避免下一帧被 PN532 误判。
        digitalWrite(Pins::NFC_SS, LOW);
        vTaskDelay(pdMS_TO_TICKS(2));
        digitalWrite(Pins::NFC_SS, HIGH);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 【函数说明】重新初始化 PN532，包括引脚、SPI、固件探测和天线启动。
    bool Reinitialize(const char *reason, bool longBootWait)
    {
        Serial.printf("[BSP][NFC] %s，正在重新初始化 PN532。\n",
                      reason ? reason : "收到恢复请求");

        // 先把 RESET/SS 设为明确输出状态，避免复位过程中片选浮动。
        pinMode(Pins::NFC_RESET, OUTPUT);
        pinMode(Pins::NFC_SS, OUTPUT);
        digitalWrite(Pins::NFC_SS, HIGH);

        // 开机时 PN532 需要更长稳定时间；普通恢复可以缩短等待。
        digitalWrite(Pins::NFC_RESET, LOW);
        vTaskDelay(pdMS_TO_TICKS(50));
        digitalWrite(Pins::NFC_RESET, HIGH);
        vTaskDelay(pdMS_TO_TICKS(longBootWait ? 200 : 80));

        digitalWrite(Pins::NFC_SS, LOW);
        vTaskDelay(pdMS_TO_TICKS(2));
        digitalWrite(Pins::NFC_SS, HIGH);
        vTaskDelay(pdMS_TO_TICKS(10));

        // 初始化独立 SPI 总线，再交给 Adafruit_PN532 建立内部状态。
        s_nfcSpi.begin(Pins::NFC_SCK, Pins::NFC_MISO, Pins::NFC_MOSI, -1);
        if (!s_nfc.begin())
        {
            Serial.println("[BSP][NFC] Adafruit PN532对象初始化失败，本轮初始化结束。");
            s_ready = false;
            return false;
        }

        // 固件版本读取是 PN532 是否真实在线的第一层确认。
        uint32_t versiondata = 0;
        for (int i = 0; i < 5; i++)
        {
            versiondata = s_nfc.getFirmwareVersion();
            if (versiondata)
                break;
            Serial.println("[BSP][NFC] PN532 暂无响应，继续等待...");
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (!versiondata)
        {
            Serial.println("[BSP][NFC] PN532 无响应，本轮初始化失败。");
            s_ready = false;
            return false;
        }

        Serial.printf("[BSP][NFC] PN532 固件版本 %d.%d。\n", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);

        // SAMConfig 会打开射频场，是后续主动读卡前必须完成的第二层确认。
        bool samOk = false;
        for (int i = 0; i < 5; i++)
        {
            if (s_nfc.SAMConfig())
            {
                samOk = true;
                break;
            }
            Serial.println("[BSP][NFC] 天线启动失败，正在重试...");
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (!samOk)
        {
            Serial.println("[BSP][NFC] PN532 射频天线未能开启。");
            s_ready = false;
            return false;
        }

        // 默认给普通寻卡留少量内部重试，减少瞬时读不到卡的概率。
        s_nfc.setPassiveActivationRetries(0x05);
        s_ready = true;
        Serial.println("[BSP][NFC] PN532 已恢复到主动读卡模式。");
        return true;
    }

    // 【函数说明】开机初始化入口。复用 Reinitialize，保持恢复逻辑只有一份。
    bool Begin(bool longBootWait)
    {
        return Reinitialize("开机初始化", longBootWait);
    }

    // 【函数说明】休眠前关闭 PN532，并保持 RESET 低电平。
    void Sleep()
    {
        s_ready = false;

        // 拉低 RESET 后开启 GPIO hold，让 Light Sleep 期间 PN532 不被意外唤醒。
        digitalWrite(Pins::NFC_RESET, LOW);
        gpio_hold_en((gpio_num_t)Pins::NFC_RESET);
        gpio_deep_sleep_hold_en();
    }

    // 【函数说明】唤醒后恢复 PN532 引脚状态；真正通信恢复由 Reinitialize 完成。
    void WakeupPins()
    {
        gpio_hold_dis((gpio_num_t)Pins::NFC_RESET);

        // SS 空闲为高，RESET 先拉高，让后续重新初始化能从确定状态开始。
        pinMode(Pins::NFC_RESET, OUTPUT);
        pinMode(Pins::NFC_SS, OUTPUT);
        digitalWrite(Pins::NFC_SS, HIGH);
        digitalWrite(Pins::NFC_RESET, HIGH);
        s_ready = false;
    }

    // 【函数说明】读取 ISO14443A 卡片 UID。
    bool ReadPassiveTarget(uint8_t *uid, uint8_t *uidLen, uint16_t timeoutMs)
    {
        return s_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, timeoutMs);
    }

    // 【函数说明】使用 Key A 认证 MIFARE Classic 指定块。
    bool MifareAuth(uint8_t *uid, uint8_t uidLen, uint8_t block, const uint8_t *key)
    {
        return s_nfc.mifareclassic_AuthenticateBlock(uid, uidLen, block, 0, (uint8_t *)key);
    }

    // 【函数说明】读取 MIFARE Classic 的一个数据块。
    bool MifareReadBlock(uint8_t block, uint8_t *data)
    {
        return s_nfc.mifareclassic_ReadDataBlock(block, data);
    }

    // 【函数说明】读取 NTAG/MIFARE Ultralight 的一个页。
    bool NtagReadPage(uint8_t page, uint8_t *data)
    {
        return s_nfc.mifareultralight_ReadPage(page, data);
    }

    // 【函数说明】设置被动寻卡内部重试次数。
    void SetPassiveActivationRetries(uint8_t maxRetries)
    {
        s_nfc.setPassiveActivationRetries(maxRetries);
    }

    // 【函数说明】通过 SPI 手写 PN532 命令帧。
    bool RawSendCommand(const uint8_t *cmd, uint8_t cmdLen)
    {
        s_nfcSpi.beginTransaction(kPn532SpiSettings);

        // 发送 PN532 SPI 数据写入标记和普通帧前导码。
        digitalWrite(Pins::NFC_SS, LOW);
        delay(2);
        s_nfcSpi.transfer(0x01);
        s_nfcSpi.transfer(0x00);
        s_nfcSpi.transfer(0x00);
        s_nfcSpi.transfer(0xFF);

        uint8_t len = cmdLen + 1;
        s_nfcSpi.transfer(len);
        s_nfcSpi.transfer(~len + 1);
        s_nfcSpi.transfer(0xD4);

        // TFI 固定为 0xD4，DCS 为 TFI+命令数据求和后取二补。
        uint8_t sum = 0xD4;
        for (int i = 0; i < cmdLen; i++)
        {
            s_nfcSpi.transfer(cmd[i]);
            sum += cmd[i];
        }
        s_nfcSpi.transfer(~sum + 1);
        s_nfcSpi.transfer(0x00);
        digitalWrite(Pins::NFC_SS, HIGH);

        // 轮询 PN532 SPI 状态字，等待它准备好返回 ACK。
        uint16_t t = 1000;
        bool isReady = false;
        while (t > 0)
        {
            digitalWrite(Pins::NFC_SS, LOW);
            delay(2);
            s_nfcSpi.transfer(0x02);
            uint8_t status = s_nfcSpi.transfer(0x00);
            digitalWrite(Pins::NFC_SS, HIGH);

            if (status == 0x01)
            {
                isReady = true;
                break;
            }
            delay(1);
            t--;
        }

        if (!isReady)
        {
            s_nfcSpi.endTransaction();
            return false;
        }

        // 读取并丢弃 6 字节 ACK 帧；上层随后再调用 RawReadResponse 获取真正响应。
        digitalWrite(Pins::NFC_SS, LOW);
        delay(1);
        s_nfcSpi.transfer(0x03);
        for (int i = 0; i < 6; i++)
            s_nfcSpi.transfer(0x00);
        digitalWrite(Pins::NFC_SS, HIGH);

        s_nfcSpi.endTransaction();
        return true;
    }

    // 【函数说明】读取 PN532 原始响应帧，返回响应 payload 长度。
    int RawReadResponse(uint8_t *buf, uint8_t maxLen, uint16_t timeoutMs)
    {
        s_nfcSpi.beginTransaction(kPn532SpiSettings);

        // 等待 PN532 表示有响应数据可读。
        uint16_t t = timeoutMs;
        bool isReady = false;
        while (t > 0)
        {
            digitalWrite(Pins::NFC_SS, LOW);
            delay(2);
            s_nfcSpi.transfer(0x02);
            uint8_t status = s_nfcSpi.transfer(0x00);
            digitalWrite(Pins::NFC_SS, HIGH);

            if (status == 0x01)
            {
                isReady = true;
                break;
            }
            delay(1);
            t--;
        }

        if (!isReady)
        {
            s_nfcSpi.endTransaction();
            return -1;
        }

        // 开始读取响应帧，并验证普通帧前导码 00 00 FF。
        digitalWrite(Pins::NFC_SS, LOW);
        delay(1);
        s_nfcSpi.transfer(0x03);

        if (s_nfcSpi.transfer(0x00) != 0x00 || s_nfcSpi.transfer(0x00) != 0x00 || s_nfcSpi.transfer(0x00) != 0xFF)
        {
            digitalWrite(Pins::NFC_SS, HIGH);
            s_nfcSpi.endTransaction();
            return -1;
        }

        // LEN 与 LCS 相加应为 0，作为长度字段完整性校验。
        uint8_t len = s_nfcSpi.transfer(0x00);
        if ((uint8_t)(len + s_nfcSpi.transfer(0x00)) != 0)
        {
            digitalWrite(Pins::NFC_SS, HIGH);
            s_nfcSpi.endTransaction();
            return -1;
        }

        // 跳过 TFI 和响应码，buf 只交付真正 payload。
        s_nfcSpi.transfer(0x00);
        s_nfcSpi.transfer(0x00);

        int actualLen = len - 2;
        for (int i = 0; i < actualLen; i++)
        {
            uint8_t b = s_nfcSpi.transfer(0x00);
            // 如果响应比调用方缓冲区长，仍读完整帧，但只保存前 maxLen 字节。
            if (i < maxLen)
                buf[i] = b;
        }

        // 丢弃 DCS 和 postamble，结束本次 SPI 事务。
        s_nfcSpi.transfer(0x00);
        s_nfcSpi.transfer(0x00);
        digitalWrite(Pins::NFC_SS, HIGH);

        s_nfcSpi.endTransaction();
        return actualLen;
    }
}

