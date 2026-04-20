// 文件：src/sys/sys_nfc.cpp
#include "sys_nfc.h"
#include "sys_config.h"
#include "sys_event.h"
#include "sys_haptic.h"
#include "sys_audio.h"
#include <SPI.h>
#include <Adafruit_PN532.h>

// ==========================================
// 硬件 SPI 引脚
// ==========================================
#define PIN_NFC_SCK 1
#define PIN_NFC_MISO 2
#define PIN_NFC_MOSI 47
#define PIN_NFC_SS 15
#define PIN_NFC_RESET 39

SPIClass nfc_spi(HSPI);
Adafruit_PN532 nfc(PIN_NFC_SS, &nfc_spi);

static const SPISettings pn532_spi_settings(1000000, LSBFIRST, SPI_MODE0);

SysNFC sysNfc;
TaskHandle_t nfcTaskHandle = NULL;

bool g_nfc_is_emulating = false;
bool g_nfc_just_started_emu = false;
uint32_t g_nfc_emu_end_time = 0;

void SysNfc_StartEmulation()
{
    g_nfc_is_emulating = true;
    g_nfc_just_started_emu = true;
    g_nfc_emu_end_time = millis() + 60000;
    Serial.println("[NFC-硬件SPI] 停止主动雷达，进入【边狱巴士】模拟伪装模式 60 秒！");
    SYS_SOUND_CONFIRM();
}

bool SysNfc_IsEmulating()
{
    if (g_nfc_is_emulating && millis() >= g_nfc_emu_end_time)
    {
        g_nfc_is_emulating = false;
    }
    return g_nfc_is_emulating;
}
void SysNfc_StopEmulation()
{
    if (g_nfc_is_emulating)
    {
        // 【核心魔法】：直接把结束时间归零！
        // 这样无论后台线程卡在哪个超时等待里，只要它一抬头看表，就会立刻退出循环并执行完美复位！
        g_nfc_emu_end_time = 0;
        Serial.println("[NFC-硬件SPI] 收到战术撤退指令，提前终止靶卡伪装！");
    }
}

uint8_t cc_file[] = {0x00, 0x0F, 0x20, 0x00, 0x3B, 0x00, 0x34, 0x04, 0x06, 0xE1, 0x04, 0x00, 0xFF, 0x00, 0x00};
uint8_t ndef_file[] = {
    0x00, 0x2F, 0xD4, 0x0F, 0x1D,
    'a', 'n', 'd', 'r', 'o', 'i', 'd', '.', 'c', 'o', 'm', ':', 'p', 'k', 'g',
    'c', 'o', 'm', '.', 'P', 'r', 'o', 'j', 'e', 'c', 't', 'M', 'o', 'o', 'n', '.', 'L', 'i', 'm', 'b', 'u', 's', 'C', 'o', 'm', 'p', 'a', 'n', 'y'};

bool raw_sendCommand(uint8_t *cmd, uint8_t cmdlen)
{
    nfc_spi.beginTransaction(pn532_spi_settings);

    digitalWrite(PIN_NFC_SS, LOW);
    delay(2);
    nfc_spi.transfer(0x01);
    nfc_spi.transfer(0x00);
    nfc_spi.transfer(0x00);
    nfc_spi.transfer(0xFF);

    uint8_t len = cmdlen + 1;
    nfc_spi.transfer(len);
    nfc_spi.transfer(~len + 1);
    nfc_spi.transfer(0xD4);

    uint8_t sum = 0xD4;
    for (int i = 0; i < cmdlen; i++)
    {
        nfc_spi.transfer(cmd[i]);
        sum += cmd[i];
    }
    nfc_spi.transfer(~sum + 1);
    nfc_spi.transfer(0x00);
    digitalWrite(PIN_NFC_SS, HIGH);

    uint16_t t = 1000;
    bool isReady = false;
    while (t > 0)
    {
        digitalWrite(PIN_NFC_SS, LOW);
        delay(2);
        nfc_spi.transfer(0x02);
        uint8_t status = nfc_spi.transfer(0x00);
        digitalWrite(PIN_NFC_SS, HIGH);

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
        nfc_spi.endTransaction();
        return false;
    }

    digitalWrite(PIN_NFC_SS, LOW);
    delay(1);
    nfc_spi.transfer(0x03);
    for (int i = 0; i < 6; i++)
        nfc_spi.transfer(0x00);
    digitalWrite(PIN_NFC_SS, HIGH);

    nfc_spi.endTransaction();
    return true;
}

int raw_readResponse(uint8_t *buf, uint8_t maxlen, uint16_t timeout)
{
    nfc_spi.beginTransaction(pn532_spi_settings);

    uint16_t t = timeout;
    bool isReady = false;
    while (t > 0)
    {
        digitalWrite(PIN_NFC_SS, LOW);
        delay(2);
        nfc_spi.transfer(0x02);
        uint8_t status = nfc_spi.transfer(0x00);
        digitalWrite(PIN_NFC_SS, HIGH);

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
        nfc_spi.endTransaction();
        return -1;
    }

    digitalWrite(PIN_NFC_SS, LOW);
    delay(1);
    nfc_spi.transfer(0x03);

    if (nfc_spi.transfer(0x00) != 0x00 || nfc_spi.transfer(0x00) != 0x00 || nfc_spi.transfer(0x00) != 0xFF)
    {
        digitalWrite(PIN_NFC_SS, HIGH);
        nfc_spi.endTransaction();
        return -1;
    }

    uint8_t len = nfc_spi.transfer(0x00);
    if ((uint8_t)(len + nfc_spi.transfer(0x00)) != 0)
    {
        digitalWrite(PIN_NFC_SS, HIGH);
        nfc_spi.endTransaction();
        return -1;
    }

    nfc_spi.transfer(0x00);
    nfc_spi.transfer(0x00);

    int actual_len = len - 2;
    for (int i = 0; i < actual_len; i++)
    {
        uint8_t b = nfc_spi.transfer(0x00);
        if (i < maxlen)
            buf[i] = b;
    }

    nfc_spi.transfer(0x00);
    nfc_spi.transfer(0x00);
    digitalWrite(PIN_NFC_SS, HIGH);

    nfc_spi.endTransaction();
    return actual_len;
}

void raw_reset()
{
    digitalWrite(PIN_NFC_RESET, LOW);
    vTaskDelay(pdMS_TO_TICKS(50));
    digitalWrite(PIN_NFC_RESET, HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));

    digitalWrite(PIN_NFC_SS, LOW);
    vTaskDelay(pdMS_TO_TICKS(2));
    digitalWrite(PIN_NFC_SS, HIGH);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ==========================================
// 【低功耗控制接口】：随时物理断电
// ==========================================
void SysNfc_Sleep()
{
    if (nfcTaskHandle != NULL)
    {
        vTaskSuspend(nfcTaskHandle);
    }
    digitalWrite(PIN_NFC_RESET, LOW);
    Serial.println("[NFC-电源管理] 模块已进入深度休眠，射频天线关闭 (1µA)。");
}

void SysNfc_Wakeup()
{
    digitalWrite(PIN_NFC_RESET, HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));
    nfc.begin();
    nfc.SAMConfig();
    if (nfcTaskHandle != NULL)
    {
        vTaskResume(nfcTaskHandle);
    }
    Serial.println("[NFC-电源管理] 模块已唤醒，恢复主动雷达扫描。");
}

void nfc_bg_task(void *pvParameters)
{
    while (true)
    {
        if (sysConfig.nfc_mode != 0)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (g_nfc_is_emulating)
        {
            if (millis() > g_nfc_emu_end_time)
            {
                Serial.println("[NFC-硬件SPI] 60秒伪装结束，恢复主动雷达扫描！");
                sysHaptic.playTick();
                g_nfc_is_emulating = false;

                digitalWrite(PIN_NFC_RESET, LOW);
                vTaskDelay(pdMS_TO_TICKS(50));
                digitalWrite(PIN_NFC_RESET, HIGH);
                vTaskDelay(pdMS_TO_TICKS(50));
                nfc.begin();
                nfc.SAMConfig();
                continue;
            }

            if (g_nfc_just_started_emu)
            {
                g_nfc_just_started_emu = false;
                raw_reset();
            }

            uint8_t tgInitCmd[] = {
                0x8C, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x20,
                0x01, 0xFE, 0x05, 0x01, 0x86, 0x04, 0x02, 0x02, 0x03, 0x00, 0x4B, 0x02, 0x4F, 0x49, 0x8A, 0x00, 0xFF, 0xFF,
                0x01, 0xFE, 0x05, 0x01, 0x86, 0x04, 0x02, 0x02, 0x03, 0x00, 0x00, 0x00};

            if (raw_sendCommand(tgInitCmd, sizeof(tgInitCmd)))
            {
                Serial.println("[NFC-硬件SPI] 靶卡伪装就绪，等待手机贴近...");
                uint8_t response[128];
                bool connected = false;

                while (g_nfc_is_emulating && millis() < g_nfc_emu_end_time)
                {
                    if (raw_readResponse(response, sizeof(response), 500) > 0)
                    {
                        connected = true;
                        break;
                    }
                }

                if (connected)
                {
                    Serial.println("[NFC-硬件SPI] 手机已碰触！建立 APDU 隧道...");
                    sysHaptic.playConfirm();
                    int current_file = 0;
                    bool payload_delivered = false;

                    int timeout_err_cnt = 0;

                    while (g_nfc_is_emulating && millis() < g_nfc_emu_end_time)
                    {
                        uint8_t tgGet[] = {0x86};
                        if (!raw_sendCommand(tgGet, 1))
                            break;

                        int apdu_len = raw_readResponse(response, sizeof(response), 500);

                        if (apdu_len <= 1)
                        {
                            timeout_err_cnt++;
                            if (timeout_err_cnt >= 3)
                            {
                                Serial.println("[NFC-硬件SPI] APDU 交互超时！手机可能已移开，斩断隧道...");
                                break;
                            }
                            continue;
                        }

                        timeout_err_cnt = 0;

                        uint8_t *apdu = &response[1];
                        apdu_len -= 1;

                        Serial.printf("[APDU] 收到指令: %02X %02X %02X %02X\n", apdu[0], apdu[1], apdu[2], apdu[3]);

                        uint8_t res_apdu[128];
                        uint8_t res_len = 0;

                        if (apdu[0] == 0x00 && apdu[1] == 0xA4 && apdu[2] == 0x04 && apdu[3] == 0x00)
                        {
                            res_apdu[0] = 0x90;
                            res_apdu[1] = 0x00;
                            res_len = 2;
                        }
                        else if (apdu[0] == 0x00 && apdu[1] == 0xA4 && apdu[2] == 0x00 && apdu[3] == 0x0C)
                        {
                            uint16_t file_id = (apdu[5] << 8) | apdu[6];
                            if (file_id == 0xE103)
                                current_file = 1;
                            else if (file_id == 0xE104)
                                current_file = 2;
                            res_apdu[0] = 0x90;
                            res_apdu[1] = 0x00;
                            res_len = 2;
                        }
                        else if (apdu[0] == 0x00 && apdu[1] == 0xB0)
                        {
                            uint16_t offset = (apdu[2] << 8) | apdu[3];
                            uint8_t length = apdu[4];
                            uint8_t *file_data = NULL;
                            uint16_t file_size = 0;

                            if (current_file == 1)
                            {
                                file_data = cc_file;
                                file_size = sizeof(cc_file);
                            }
                            else if (current_file == 2)
                            {
                                file_data = ndef_file;
                                file_size = sizeof(ndef_file);
                            }

                            if (file_data != NULL && offset < file_size)
                            {
                                uint16_t bytes_to_read = length;
                                if (offset + bytes_to_read > file_size)
                                    bytes_to_read = file_size - offset;
                                memcpy(res_apdu, file_data + offset, bytes_to_read);
                                res_apdu[bytes_to_read] = 0x90;
                                res_apdu[bytes_to_read + 1] = 0x00;
                                res_len = bytes_to_read + 2;

                                if (current_file == 2 && (offset + bytes_to_read >= file_size))
                                {
                                    payload_delivered = true;
                                }
                            }
                            else
                            {
                                res_apdu[0] = 0x6A;
                                res_apdu[1] = 0x82;
                                res_len = 2;
                            }
                        }
                        else
                        {
                            res_apdu[0] = 0x68;
                            res_apdu[1] = 0x00;
                            res_len = 2;
                        }

                        uint8_t tgSet[130];
                        tgSet[0] = 0x8E;
                        memcpy(&tgSet[1], res_apdu, res_len);
                        if (!raw_sendCommand(tgSet, res_len + 1))
                            break;
                        raw_readResponse(response, sizeof(response), 1000);

                        if (payload_delivered)
                        {
                            Serial.println("[NFC-硬件SPI] 手机提取完整载荷！主动切断隧道！");
                            break;
                        }
                    }

                    if (payload_delivered)
                    {
                        Serial.println("[NFC-硬件SPI] 载荷投递成功！准备迎接下一次碰触...");
                        sysHaptic.playConfirm();
                    }
                    else
                    {
                        Serial.println("[NFC-硬件SPI] 隧道已重置，继续保持靶卡伪装...");
                    }

                    vTaskDelay(pdMS_TO_TICKS(1500));
                    raw_reset();
                }
            }
            else
            {
                Serial.println("[NFC-硬件SPI] 靶卡部署失败，洗刷芯片重试...");
                raw_reset();
            }
            continue;
        }

        // =====================================
        // 日常读卡区：扇区缓存提速 + 严格完整性校验
        // =====================================
        nfc.setPassiveActivationRetries(0x05);

        uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
        uint8_t uidLength;

        bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
        bool has_valid_cmd = false;

        if (success)
        {
            vTaskDelay(pdMS_TO_TICKS(30));

            Evt_NfcScanned_t payload = {0};

            if (uidLength == 4)
            {
                sprintf(payload.uid, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
                Serial.printf("[NFC-官方] 发现 M1 卡, UID: %s\n", payload.uid);

                // ==========================================
                // M1 极速引擎：智能跳过 + 50ms 超时护盾
                // ==========================================
                uint8_t keys[2][6] = {
                    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}, // NDEF 钥匙
                    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}  // 出厂白板 钥匙
                };
                int current_key_idx = 0; // 默认先试 NDEF

                String raw_text = "";
                bool read_aborted = false;
                int current_sector = -1;
                bool stop_reading = false;

                // 全盘雷达
                int data_blocks[] = {
                    4, 5, 6, 8, 9, 10, 12, 13, 14, 16, 17, 18, 20, 21, 22,
                    24, 25, 26, 28, 29, 30, 32, 33, 34, 36, 37, 38,
                    40, 41, 42, 44, 45, 46, 48, 49, 50, 52, 53, 54,
                    56, 57, 58, 60, 61, 62};
                int num_blocks = sizeof(data_blocks) / sizeof(data_blocks[0]);

                for (int i = 0; i < num_blocks; i++)
                {
                    if (stop_reading)
                        break;

                    int block = data_blocks[i];
                    int target_sector = block / 4;
                    bool block_ok = false;
                    bool needs_auth = (target_sector != current_sector);

                    // 1. 智能极速开门 (打不开直接跳，不浪费时间)
                    if (needs_auth)
                    {
                        bool auth_success = false;
                        for (int k = 0; k < 2; k++)
                        {
                            int test_idx = (current_key_idx + k) % 2; // 优先试上次成功的钥匙！
                            if (k > 0)
                            {
                                // 【核心提速】：唤醒加入 50ms 超时！绝不让芯片死等发呆！
                                uint8_t d_uid[7], d_len;
                                nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, d_uid, &d_len, 50);
                            }

                            if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, keys[test_idx]))
                            {
                                auth_success = true;
                                current_key_idx = test_idx; // 记住这把好钥匙
                                current_sector = target_sector;
                                needs_auth = false;
                                break;
                            }
                        }
                        if (!auth_success)
                        {
                            // 两把常规钥匙都错？说明这扇门没我们要的东西，直接跳过！
                            continue;
                        }
                    }

                    // 2. 极速读取数据
                    uint8_t data[16];
                    for (int read_retry = 0; read_retry < 2; read_retry++)
                    {
                        if (read_retry > 0)
                        {
                            uint8_t d_uid[7], d_len;
                            nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, d_uid, &d_len, 50); // 同样 50ms 超时
                            nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, keys[current_key_idx]);
                        }

                        if (nfc.mifareclassic_ReadDataBlock(block, data))
                        {
                            block_ok = true;
                            for (int j = 0; j < 16; j++)
                            {
                                uint8_t b = data[j];
                                if (b == 0xFE)
                                {
                                    stop_reading = true;
                                    break;
                                }
                                if (b == 0xFF || b == 0x00)
                                    continue;
                                if ((b >= 32 && b <= 126) || b >= 128)
                                    raw_text += (char)b;
                            }
                            break;
                        }
                    }

                    if (!block_ok)
                    {
                        Serial.printf("[NFC] 块 %d 物理断开，中断。\n", block);
                        read_aborted = true;
                        break;
                    }
                }

                if (read_aborted)
                {
                    Serial.println("[NFC-警告] 读取不完整，数据丢弃！");
                    sysAudio.playTone(400, 150);
                }
                else
                {
                    int min_idx = 9999;
                    int idx;
                    if ((idx = raw_text.indexOf("PRE:")) != -1 && idx < min_idx)
                        min_idx = idx;
                    if ((idx = raw_text.indexOf("SCH:")) != -1 && idx < min_idx)
                        min_idx = idx;
                    if ((idx = raw_text.indexOf("ALM:")) != -1 && idx < min_idx)
                        min_idx = idx;
                    if ((idx = raw_text.indexOf("TXT:")) != -1 && idx < min_idx)
                        min_idx = idx;
                    if ((idx = raw_text.indexOf("POM:")) != -1 && idx < min_idx)
                        min_idx = idx;
                    if ((idx = raw_text.indexOf("COIN:")) != -1 && idx < min_idx)
                        min_idx = idx;
                    if ((idx = raw_text.indexOf("COIN_DEL:")) != -1 && idx < min_idx)
                        min_idx = idx;
                    if ((idx = raw_text.indexOf("WIFI:")) != -1 && idx < min_idx)
                        min_idx = idx;

                    if (min_idx != 9999)
                    {
                        String clean_text = raw_text.substring(min_idx);
                        Serial.printf("[NFC] 提取指令: %s\n", clean_text.c_str());
                        snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                        sysAudio.playTone(4000, 50);
                        sysHaptic.playConfirm();
                        SysEvent_Publish(EVT_NFC_SCANNED, &payload);
                        has_valid_cmd = true;
                    }
                    else
                    {
                        if (raw_text.length() > 0)
                            Serial.printf("[NFC-警告] 未发现有效指令头: %s\n", raw_text.c_str());
                        else
                            Serial.println("[NFC-警告] 卡片内容为空！");
                        sysAudio.playTone(400, 150);
                    }
                }
            }
            else if (uidLength == 7)
            {
                sprintf(payload.uid, "%02X%02X%02X%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
                Serial.printf("[NFC-官方] 发现 NTAG, UID: %s\n", payload.uid);

                String raw_text = "";
                bool stop_reading = false; // 同样恢复提前结束

                for (uint8_t page = 4; page < 64; page++)
                {
                    if (stop_reading)
                        break;

                    bool page_ok = false;
                    for (int retry = 0; retry < 3; retry++)
                    {
                        if (retry > 0)
                        {
                            vTaskDelay(pdMS_TO_TICKS(10));
                            uint8_t d_uid[7], d_len;
                            nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, d_uid, &d_len);
                        }

                        uint8_t data[4];
                        if (nfc.mifareultralight_ReadPage(page, data))
                        {
                            page_ok = true;
                            for (int i = 0; i < 4; i++)
                            {
                                uint8_t b = data[i];
                                // 【NTAG 的护盾也加回来】
                                if (b == 0xFE)
                                {
                                    stop_reading = true;
                                    break;
                                }
                                if (b == 0xFF || b == 0x00)
                                    continue;
                                if ((b >= 32 && b <= 126) || b >= 128)
                                    raw_text += (char)b;
                            }
                            break;
                        }
                    }
                    if (!page_ok)
                        break;
                }

                int min_idx = 9999;
                int idx;
                if ((idx = raw_text.indexOf("PRE:")) != -1 && idx < min_idx)
                    min_idx = idx;
                if ((idx = raw_text.indexOf("SCH:")) != -1 && idx < min_idx)
                    min_idx = idx;
                if ((idx = raw_text.indexOf("ALM:")) != -1 && idx < min_idx)
                    min_idx = idx;
                if ((idx = raw_text.indexOf("TXT:")) != -1 && idx < min_idx)
                    min_idx = idx;
                if ((idx = raw_text.indexOf("POM:")) != -1 && idx < min_idx)
                    min_idx = idx;
                if ((idx = raw_text.indexOf("COIN:")) != -1 && idx < min_idx)
                    min_idx = idx;
                if ((idx = raw_text.indexOf("COIN_DEL:")) != -1 && idx < min_idx)
                    min_idx = idx;
                if ((idx = raw_text.indexOf("WIFI:")) != -1 && idx < min_idx)
                    min_idx = idx;

                if (min_idx != 9999)
                {
                    String clean_text = raw_text.substring(min_idx);
                    Serial.printf("[NFC] 提取指令: %s\n", clean_text.c_str());
                    snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                    sysAudio.playTone(4000, 50);
                    sysHaptic.playConfirm();
                    SysEvent_Publish(EVT_NFC_SCANNED, &payload);
                    has_valid_cmd = true;
                }
                else
                {
                    if (raw_text.length() > 0)
                        Serial.printf("[NFC-警告] 未发现有效指令头: %s\n", raw_text.c_str());
                    else
                        Serial.println("[NFC-警告] NTAG 内容为空！");
                    sysAudio.playTone(400, 150);
                }
            }
        }

        if (has_valid_cmd)
            vTaskDelay(pdMS_TO_TICKS(1500));
        else
            vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void SysNFC::begin()
{
    // 1. 硬件级强制复位
    pinMode(PIN_NFC_RESET, OUTPUT);
    digitalWrite(PIN_NFC_RESET, LOW);
    vTaskDelay(pdMS_TO_TICKS(50)); // 保持拉低，彻底放电
    digitalWrite(PIN_NFC_RESET, HIGH);
    vTaskDelay(pdMS_TO_TICKS(200)); // 【修复1】给足 200ms 等待芯片数字核心完全启动！

    nfc_spi.begin(PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, -1);
    nfc.begin();

    // 2. 唤醒数字大脑（带重试机制，防止 SPI 拥堵）
    uint32_t versiondata = 0;
    for (int i = 0; i < 5; i++)
    {
        versiondata = nfc.getFirmwareVersion();
        if (versiondata)
            break;
        Serial.println("[NFC-硬件SPI] 等待 PN532 大脑响应...");
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!versiondata)
    {
        Serial.println("[NFC-致命错误] 找不到 PN532 模块，强制挂起！");
        while (1)
        {
            vTaskDelay(10);
        }
    }

    Serial.printf("[NFC-硬件SPI] 成功连接 PN532 固件版本 %d.%d\n", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);

    // ==========================================
    // 3. 【核心修复：死磕天线点火】
    // ==========================================
    vTaskDelay(pdMS_TO_TICKS(50)); // 唤醒大脑后，等 50ms 让内部电压稳定

    bool sam_ok = false;
    for (int i = 0; i < 5; i++)
    {
        // SAMConfig() 返回 true 才代表天线真正通电发波了！
        if (nfc.SAMConfig())
        {
            sam_ok = true;
            break;
        }
        Serial.println("[NFC-硬件SPI] 天线点火失败(可能因SPI总线冲突)，正在重新尝试...");
        vTaskDelay(pdMS_TO_TICKS(100)); // 避让屏幕的 SPI 通信高峰期
    }

    if (!sam_ok)
    {
        Serial.println("[NFC-致命错误] 无法开启 PN532 射频天线！请按复位键重启！");
        // 天线没开，读卡纯属扯淡，直接挂起报错
        while (1)
        {
            vTaskDelay(10);
        }
    }

    Serial.println("[NFC-硬件SPI] 天线点火成功！纯血引擎已启动 (高速稳定读卡 + 硬件级穿透模拟)...");

    // 规范硬件重试次数，防止死锁
    nfc.setPassiveActivationRetries(0x05);

    // 4. 启动后台守护任务 (优先级设高一点，防止被 UI 阻塞)
    xTaskCreatePinnedToCore(
        nfc_bg_task,
        "nfc_task",
        4096,
        NULL,
        5, // 优先级
        &nfcTaskHandle,
        1 // 绑定到 APP CPU (Core 1)，避开屏幕刷新的 PRO CPU
    );
}