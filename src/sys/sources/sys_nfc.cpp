/*
【模块职责】FM17550 NFC 系统服务。后台扫描 MIFARE Classic 与 NTAG/Ultralight 卡，提取其中的
Prescript 文本命令并投入主循环共享队列；同时负责扩展板缺席、通信异常和系统休眠时的状态恢复。
【能力边界】当前产品功能已移除手机模拟卡，保留的模拟查询接口只为现有 HUD/App 安全降级。
*/
#include "sys/sys_nfc.h"

#include "bsp/bsp_nfc_fm17550.h"
#include "sys/sys_ble_queue.h"
#include "sys/sys_config.h"
#include "sys/sys_feedback.h"
#include "sys/sys_sleep_scheduler.h"

SysNFC sysNfc;

namespace
{
    TaskHandle_t s_nfcTaskHandle = nullptr;

    volatile bool s_nfcReady = false;
    volatile bool s_sleepRequest = false;
    volatile bool s_sleepAck = false;
    volatile uint32_t s_nextRetryMs = 0;

    constexpr uint32_t kOfflineRetryMs = 2500;
    constexpr uint16_t kIdleHealthCheckLimit = 400; // 约 100 秒无卡后只读版本寄存器确认链路健康。
    constexpr uint8_t kCardErrorRecoverLimit = 3;
    constexpr size_t kMaxRawTextLength = 768;

    uint16_t s_idleMissCount = 0;
    uint8_t s_cardErrorCount = 0;

    // MIFARE Classic 1K 的数据块列表；每个扇区的尾块是密钥区，绝不能当文本读取。
    constexpr uint8_t kMifareDataBlocks[] = {
        4, 5, 6, 8, 9, 10, 12, 13, 14, 16, 17, 18, 20, 21, 22,
        24, 25, 26, 28, 29, 30, 32, 33, 34, 36, 37, 38,
        40, 41, 42, 44, 45, 46, 48, 49, 50, 52, 53, 54,
        56, 57, 58, 60, 61, 62};

    constexpr uint8_t kMifareKeys[][6] = {
        {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}, // NFC Forum/MIFARE NDEF 常见 Key A。
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // 出厂空白卡默认 Key A。
    };

    void nfcBackgroundTask(void *parameter);

    void waitForNextScan(uint32_t timeoutMs)
    {
        // 手动扫描通过任务通知打断等待；若通知在读卡过程中到达，下一次等待会立刻返回。
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeoutMs));
    }

    bool timeReached(uint32_t deadline)
    {
        return (int32_t)(millis() - deadline) >= 0;
    }

    void clearHealthCounters()
    {
        s_idleMissCount = 0;
        s_cardErrorCount = 0;
    }

    void enterOffline(const char *reason, uint32_t retryDelayMs = kOfflineRetryMs)
    {
        BSP::NfcFm17550::MarkOffline();
        s_nfcReady = false;
        clearHealthCounters();
        s_nextRetryMs = millis() + retryDelayMs;
        Serial.printf("[NFC] %s，%lu ms 后由后台重试。\n",
                      reason ? reason : "FM17550 暂不可用",
                      (unsigned long)retryDelayMs);
    }

    bool reinitialize(const char *reason, bool longBootWait)
    {
        const bool ok = BSP::NfcFm17550::Reinitialize(reason, longBootWait);
        s_nfcReady = ok;
        if (ok)
            clearHealthCounters();
        return ok;
    }

    void startTaskIfNeeded()
    {
        if (s_nfcTaskHandle != nullptr)
            return;

        const BaseType_t created = xTaskCreatePinnedToCore(
            nfcBackgroundTask,
            "nfc_task",
            8192,
            nullptr,
            5,
            &s_nfcTaskHandle,
            1);

        if (created != pdPASS)
        {
            s_nfcTaskHandle = nullptr;
            enterOffline("NFC 后台任务创建失败", 5000);
        }
    }

    // 在卡片原始文本中找协议层支持的最早命令头，保证 NFC 与 BLE 使用同一套命令集合。
    int findCommandStart(const String &rawText)
    {
        static const char *const kPrefixes[] = {
            "GET:SPC_TXT:",
            "GET:TARGET",
            "GET:TGT",
            "GET:SYNC",
            "GET:LANG",
            "GET:INFO",
            "TGT_ADD:",
            "TGT_DEL:",
            "TGT_SET:",
            "TXT:",
            "ALM_DEL:",
            "ALM:",
            "POM:",
            "SCH_HID:",
            "SCH_DEL:",
            "SCH:",
            "PRE_DEL:ZH:",
            "PRE_DEL:EN:",
            "PRE:ZH:",
            "PRE:EN:",
            "WIFI:",
            "COIN_DEL:",
            "COIN:",
            "SPC:"};

        int earliest = -1;
        for (const char *prefix : kPrefixes)
        {
            const int index = rawText.indexOf(prefix);
            if (index >= 0 && (earliest < 0 || index < earliest))
                earliest = index;
        }
        return earliest;
    }

    void appendCardBytes(String &text,
                         const uint8_t *data,
                         size_t length,
                         bool &stopReading)
    {
        for (size_t i = 0; i < length && !stopReading; ++i)
        {
            const uint8_t value = data[i];
            if (value == 0xFE)
            {
                stopReading = true;
                break;
            }
            if (value == 0x00 || value == 0xFF)
                continue;
            if (((value >= 32 && value <= 126) || value >= 128) && text.length() < kMaxRawTextLength)
                text += (char)value;
        }
    }

    void formatUid(const uint8_t *uid, uint8_t uidLength, char output[32])
    {
        size_t offset = 0;
        output[0] = '\0';
        for (uint8_t i = 0; i < uidLength && offset + 2 < 32; ++i)
        {
            const int written = snprintf(output + offset, 32 - offset, "%02X", uid[i]);
            if (written <= 0)
                break;
            offset += (size_t)written;
        }
    }

    bool selectSameCard(const uint8_t *expectedUid, uint8_t expectedUidLength)
    {
        uint8_t uid[10] = {};
        uint8_t uidLength = 0;
        uint8_t sak = 0;
        return BSP::NfcFm17550::ReadPassiveTarget(uid, &uidLength, &sak, 70) &&
               uidLength == expectedUidLength &&
               memcmp(uid, expectedUid, uidLength) == 0;
    }

    bool authenticateSector(const uint8_t *uid,
                            uint8_t uidLength,
                            uint8_t block,
                            uint8_t &preferredKey)
    {
        for (uint8_t attempt = 0; attempt < 2; ++attempt)
        {
            const uint8_t keyIndex = (preferredKey + attempt) % 2;
            if (attempt > 0 && !selectSameCard(uid, uidLength))
                continue;

            if (BSP::NfcFm17550::MifareAuth(uid, uidLength, block, kMifareKeys[keyIndex]))
            {
                preferredKey = keyIndex;
                return true;
            }
        }
        return false;
    }

    bool readMifareText(const uint8_t *uid,
                        uint8_t uidLength,
                        String &rawText,
                        bool &readAborted)
    {
        rawText = "";
        rawText.reserve(512);
        readAborted = false;

        int currentSector = -1;
        uint8_t preferredKey = 0;
        bool stopReading = false;

        for (uint8_t block : kMifareDataBlocks)
        {
            if (stopReading)
                break;

            const int sector = block / 4;
            if (sector != currentSector)
            {
                if (!authenticateSector(uid, uidLength, block, preferredKey))
                    continue;
                currentSector = sector;
            }

            uint8_t data[16] = {};
            bool blockOk = false;
            for (uint8_t retry = 0; retry < 2; ++retry)
            {
                if (retry > 0)
                {
                    if (!selectSameCard(uid, uidLength) ||
                        !BSP::NfcFm17550::MifareAuth(uid,
                                                    uidLength,
                                                    block,
                                                    kMifareKeys[preferredKey]))
                        continue;
                }

                if (BSP::NfcFm17550::MifareReadBlock(block, data))
                {
                    blockOk = true;
                    appendCardBytes(rawText, data, sizeof(data), stopReading);
                    break;
                }
            }

            if (!blockOk)
            {
                Serial.printf("[NFC] MIFARE 块 %u 连续读取失败，本次数据作废。\n", block);
                readAborted = true;
                return false;
            }
        }
        return true;
    }

    bool readNtagText(String &rawText, bool &pageError)
    {
        rawText = "";
        rawText.reserve(256);
        pageError = false;
        bool stopReading = false;

        // READ 命令一次返回四页，因此步长为 4，避免把同一批数据重复拼接四遍。
        for (uint8_t page = 4; page < 64 && !stopReading; page += 4)
        {
            uint8_t data[16] = {};
            bool pageOk = false;
            for (uint8_t retry = 0; retry < 3; ++retry)
            {
                if (retry > 0)
                    vTaskDelay(pdMS_TO_TICKS(10));
                if (BSP::NfcFm17550::NtagReadFourPages(page, data))
                {
                    pageOk = true;
                    appendCardBytes(rawText, data, sizeof(data), stopReading);
                    break;
                }
            }

            if (!pageOk)
            {
                pageError = true;
                break;
            }
        }
        return !pageError || rawText.length() > 0;
    }

    bool enqueueExtractedCommand(const String &rawText)
    {
        const int start = findCommandStart(rawText);
        if (start < 0)
        {
            if (rawText.length() > 0)
                Serial.printf("[NFC] 卡片有 %u 字节文本，但没有找到受支持的命令头。\n",
                              (unsigned int)rawText.length());
            else
                Serial.println("[NFC] 卡片数据区为空或没有可读文本。");
            Feedback_PlayNfcReadError();
            return false;
        }

        const String cleanText = rawText.substring(start);
        SysBleQueue_Push(cleanText);
        Feedback_PlayNfcReadOk();
        // NFC 也可能承载 WIFI 密码等私密字段，运行日志只记录长度，不打印完整载荷。
        Serial.printf("[NFC] 已提取并入队命令，长度=%u。\n", (unsigned int)cleanText.length());
        clearHealthCounters();
        return true;
    }

    void noteCardReadError(const char *reason)
    {
        if (++s_cardErrorCount < kCardErrorRecoverLimit)
            return;

        s_cardErrorCount = 0;
        if (!reinitialize(reason ? reason : "连续卡片读取异常", false))
            enterOffline(reason ? reason : "卡片读取恢复失败", 1200);
    }

    void noteIdleMiss()
    {
        if (++s_idleMissCount < kIdleHealthCheckLimit)
            return;

        s_idleMissCount = 0;
        if (!BSP::NfcFm17550::HealthCheck())
            enterOffline("长时间空轮询后版本寄存器失联", 1200);
    }

    void nfcBackgroundTask(void *parameter)
    {
        (void)parameter;

        while (true)
        {
            // 只在两次卡片事务之间确认休眠，避免主线程把 UART 操作截断在半帧状态。
            if (s_sleepRequest)
            {
                s_sleepAck = true;
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            s_sleepAck = false;

            if (sysConfig.nfc_mode != 0)
            {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            if (!s_nfcReady)
            {
                if (timeReached(s_nextRetryMs))
                {
                    if (reinitialize("后台离线重试", false))
                        Serial.println("[NFC] FM17550 已恢复，继续后台读卡。");
                    else
                        s_nextRetryMs = millis() + kOfflineRetryMs;
                }
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            uint8_t uid[10] = {};
            uint8_t uidLength = 0;
            uint8_t sak = 0;
            const bool found = BSP::NfcFm17550::ReadPassiveTarget(uid, &uidLength, &sak, 90);

            if (!found)
            {
                noteIdleMiss();
                waitForNextScan(250);
                continue;
            }

            s_idleMissCount = 0;
            char uidText[32] = {};
            formatUid(uid, uidLength, uidText);
            Serial.printf("[NFC] 检测到 ISO14443A 卡，UID=%s，SAK=%02X。\n", uidText, sak);

            String rawText;
            bool validCommand = false;

            // 常见 MIFARE Mini/1K/4K 的 SAK 都包含 08h 位；旧功能只扫描前 1K 数据块范围。
            if ((sak & 0x08) != 0)
            {
                bool readAborted = false;
                if (readMifareText(uid, uidLength, rawText, readAborted) && !readAborted)
                {
                    validCommand = enqueueExtractedCommand(rawText);
                }
                else
                {
                    Feedback_PlayNfcReadError();
                    noteCardReadError("MIFARE 连续块读取异常");
                }
            }
            // Ultralight/NTAG 完成防冲突选择后的常见 SAK 为 00h，UID 长度不能再作为唯一类型依据。
            else if (sak == 0x00)
            {
                bool pageError = false;
                (void)readNtagText(rawText, pageError);
                validCommand = enqueueExtractedCommand(rawText);
                if (pageError && !validCommand)
                    noteCardReadError("NTAG 连续页读取异常");
            }
            else
            {
                Serial.printf("[NFC] 当前只迁移 MIFARE Classic 与 NTAG 文本卡，SAK=%02X 本轮不读取。\n", sak);
                Feedback_PlayNfcReadError();
            }

            waitForNextScan(validCommand ? 1500 : 500);
        }
    }
}

void SysNFC::begin()
{
    SysSleep_SetBlocker(SysSleepBlocker::NfcEmulation, false);

    if (reinitialize("开机初始化", true))
        Serial.println("[NFC] FM17550 实体卡读取服务已启动。");
    else
        enterOffline("开机未检测到 FM17550，服务进入后台重试态", 1200);

    startTaskIfNeeded();
}

void SysNFC::triggerManualScan()
{
    if (s_nfcTaskHandle != nullptr)
        xTaskNotifyGive(s_nfcTaskHandle);
    Serial.println("[NFC] 已请求立即扫描；后台常扫仍保持开启。");
}

bool SysNfc_IsEmulating()
{
    return false;
}

void SysNfc_StartEmulation()
{
    SysSleep_SetBlocker(SysSleepBlocker::NfcEmulation, false);
    Serial.println("[NFC] 当前产品已移除手机模拟卡功能。");
}

void SysNfc_StopEmulation()
{
    SysSleep_SetBlocker(SysSleepBlocker::NfcEmulation, false);
}

int SysNfc_GetEmulationRemainingSeconds()
{
    return 0;
}

void SysNfc_Sleep()
{
    s_sleepRequest = true;

    const uint32_t start = millis();
    while (!s_sleepAck && (uint32_t)(millis() - start) < 700)
        vTaskDelay(pdMS_TO_TICKS(20));

    if (!s_sleepAck)
        Serial.println("[NFC] 等待后台任务进入休眠安全点超时，将直接关闭射频场。");

    s_nfcReady = false;
    BSP::NfcFm17550::Sleep();
    Serial.println("[NFC] FM17550 已关闭射频并进入 Soft Power-down。");
}

void SysNfc_Wakeup()
{
    BSP::NfcFm17550::Wakeup();
    s_sleepAck = false;
    s_sleepRequest = false;
    s_nfcReady = false;
    s_nextRetryMs = millis();
    startTaskIfNeeded();
    Serial.println("[NFC] 已发送 UART 唤醒序列，后台将重新初始化 FM17550。");
}
