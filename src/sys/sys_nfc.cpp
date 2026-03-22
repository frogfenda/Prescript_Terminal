#include "sys_nfc.h"
#include "sys_config.h"
#include "sys_event.h"
#include "sys_haptic.h"
#include "sys_audio.h"
#include <Wire.h>
#include <Adafruit_PN532.h>

// 完美安全区引脚
#define PIN_NFC_SDA 1
#define PIN_NFC_SCL 2
#define PIN_NFC_IRQ 47
#define PIN_NFC_RESET 39

// 恢复使用 Adafruit 官方原版对象
Adafruit_PN532 nfc(PIN_NFC_IRQ, PIN_NFC_RESET);

SysNFC sysNfc;
TaskHandle_t nfcTaskHandle = NULL;

// ==========================================
// 【UI 兼容占位符】：为了不让其他文件报错，保留变量名但禁用功能
// ==========================================
bool g_nfc_is_emulating = false;
uint32_t g_nfc_emu_end_time = 0;

void SysNfc_StartEmulation() {
    Serial.println("[NFC] 伪装功能已暂时回滚屏蔽。");
    sysHaptic.playTick(); // 随便震动一下假装按了
}

bool SysNfc_IsEmulating() {
    return false;
}

// ==========================================
// 后台轮询线程 (稳定读卡版)
// ==========================================
void nfc_bg_task(void *pvParameters)
{
    while (true)
    {
        if (sysConfig.nfc_mode != 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
        uint8_t uidLength;

        // 原始阻塞轮询，超时200毫秒
        bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);

        if (success)
        {
            Evt_NfcScanned_t payload = {0};
            
            // 【分支 A】：M1 加密扣 (Mifare Classic 1K)
            if (uidLength == 4)
            {
                sprintf(payload.uid, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
                Serial.printf("[NFC] 发现 M1 卡, UID: %s\n", payload.uid);

                vTaskDelay(pdMS_TO_TICKS(50));

                uint8_t key_factory[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                uint8_t key_ndef[6] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7};

                String raw_text = "";
                int data_blocks[] = {4, 5, 6, 8, 9, 10, 12, 13, 14, 16, 17, 18, 20, 21, 22};
                bool stop_reading = false;
                int retry_count = 0; 

                for (int i = 0; i < 15;)
                {
                    if (stop_reading) break;

                    int block = data_blocks[i];
                    bool auth_success = false;

                    if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, key_ndef))
                        auth_success = true;
                    else if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, key_factory))
                        auth_success = true;

                    if (auth_success)
                    {
                        uint8_t data[16];
                        if (nfc.mifareclassic_ReadDataBlock(block, data))
                        {
                            for (int j = 0; j < 16; j++)
                            {
                                uint8_t b = data[j];
                                if (b == 0xFE) { stop_reading = true; break; }
                                if (b == 0xFF || b == 0x00) continue;
                                if ((b >= 32 && b <= 126) || b >= 128) raw_text += (char)b;
                            }
                            i++;             
                            retry_count = 0; 
                        }
                        else
                        {
                            retry_count++;
                            if (retry_count > 3) break;                     
                            vTaskDelay(pdMS_TO_TICKS(10)); 
                        }
                    }
                    else
                    {
                        retry_count++;
                        if (retry_count > 3) break;
                    }
                }

                // 宏指令安全切割
                int min_idx = 9999;
                int idx;
                
                if ((idx = raw_text.indexOf("PRE:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("SCH:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("CMD:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("ALM:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("TXT:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("POM:")) != -1 && idx < min_idx) min_idx = idx;
                
                if (min_idx != 9999) {
                    String clean_text = raw_text.substring(min_idx);
                    Serial.printf("[NFC] 完美提取到完整指令: %s\n", clean_text.c_str());
                    snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                    
                    sysAudio.playTone(4000, 50); 
                    sysHaptic.playConfirm();     
                    SysEvent_Publish(EVT_NFC_SCANNED, &payload);
                }
            }
            // 【分支 B】：NTAG 贴纸
            else if (uidLength == 7)
            {
                sprintf(payload.uid, "%02X%02X%02X%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
                Serial.printf("[NFC] 发现 NTAG, UID: %s\n", payload.uid);

                vTaskDelay(pdMS_TO_TICKS(50));

                uint8_t data[4];
                String raw_text = "";
                bool stop_reading = false;
                int retry_count = 0;

                for (uint8_t page = 4; page < 64;)
                {
                    if (stop_reading) break;

                    if (nfc.mifareultralight_ReadPage(page, data))
                    {
                        for (int i = 0; i < 4; i++)
                        {
                            uint8_t b = data[i];
                            if (b == 0xFE) { stop_reading = true; break; }
                            if (b == 0xFF || b == 0x00) continue;
                            if ((b >= 32 && b <= 126) || b >= 128) raw_text += (char)b;
                        }
                        page++; 
                        retry_count = 0;
                    }
                    else
                    {
                        retry_count++;
                        if (retry_count > 3) break;                     
                        vTaskDelay(pdMS_TO_TICKS(10)); 
                    }
                }

                int min_idx = 9999;
                int idx;
                
                if ((idx = raw_text.indexOf("PRE:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("SCH:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("CMD:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("ALM:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("TXT:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("POM:")) != -1 && idx < min_idx) min_idx = idx;
                
                if (min_idx != 9999) {
                    String clean_text = raw_text.substring(min_idx);
                    Serial.printf("[NFC] 完美提取到完整指令: %s\n", clean_text.c_str());
                    snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                    
                    sysAudio.playTone(4000, 50); 
                    sysHaptic.playConfirm();     
                    SysEvent_Publish(EVT_NFC_SCANNED, &payload);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1500));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void SysNFC::begin()
{
    // 物理复位
    pinMode(PIN_NFC_RESET, OUTPUT);
    digitalWrite(PIN_NFC_RESET, LOW);
    delay(50);
    digitalWrite(PIN_NFC_RESET, HIGH);
    delay(50);

    // 绑定总线
    Wire.begin(PIN_NFC_SDA, PIN_NFC_SCL);
    
    // 启动官方库
    nfc.begin();

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata)
    {
        Serial.println("[NFC] 致命错误：模块离线！");
        return;
    }
    
    Serial.printf("[NFC] 成功连接 PN5%02X 固件版本 %d.%d\n", 
                  (versiondata >> 24) & 0xFF, 
                  (versiondata >> 16) & 0xFF, 
                  (versiondata >> 8) & 0xFF);
                  
    nfc.SAMConfig();

    xTaskCreate(nfc_bg_task, "NFC_Task", 4096, NULL, 2, &nfcTaskHandle);
    Serial.println("[NFC] 轮询引擎已启动 (恢复稳定版).");
}