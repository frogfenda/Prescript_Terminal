#include "sys_nfc.h"
#include "sys_config.h"
#include "sys_event.h"
#include "sys_haptic.h" 
#include "sys_audio.h"  
#include <Wire.h>
#include <Adafruit_PN532.h>

// 完美安全区引脚
#define PIN_NFC_SDA   1   
#define PIN_NFC_SCL   2   
#define PIN_NFC_IRQ   47  
#define PIN_NFC_RESET 39  

Adafruit_PN532 nfc(PIN_NFC_IRQ, PIN_NFC_RESET);
SysNFC sysNfc;
TaskHandle_t nfcTaskHandle = NULL;

// ==========================================
// 【终极重构】：后台轮询线程 (降维打击版)
// ==========================================
void nfc_bg_task(void *pvParameters) {
    while (true) {
        // 如果系统设置里关了 NFC，就休眠
        if (sysConfig.nfc_mode != 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
        uint8_t uidLength;

        // 【核心魔法】：直接使用最原始的阻塞轮询！超时设为 200 毫秒。
        // 因为我们在独立总线上，这里的阻塞绝对不会卡顿主菜单！
        bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);

        if (success) {


            Evt_NfcScanned_t payload = {0};
// 【分支 A】：M1 加密扣 (Mifare Classic 1K)
            if (uidLength == 4) {
                sprintf(payload.uid, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
                Serial.printf("[NFC] 发现 M1 卡, UID: %s\n", payload.uid);

                uint8_t key_factory[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; 
                uint8_t key_ndef[6]    = { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 }; 
                
                String raw_text = "";
                

               // 【扩容】：追加第 3、4、5 扇区的数据块
                // 严禁写入 7, 11, 15, 19, 23（这些是密码所在块）
                int data_blocks[] = {
                    4, 5, 6,       // 扇区 1
                    8, 9, 10,      // 扇区 2
                    12, 13, 14,    // 扇区 3
                    16, 17, 18,    // 扇区 4
                    20, 21, 22     // 扇区 5
                }; 
                
                // 【核心修改】：把循环次数改成数组的长度 (15)
                for (int i = 0; i < 15; i++) {
                    int block = data_blocks[i];

                    bool auth_success = false;
                    
                    // 每次读取都需要验证扇区密码
                    if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, key_ndef)) auth_success = true;
                    else if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, key_factory)) auth_success = true;

                    if (auth_success) {
                        uint8_t data[16];
                        if (nfc.mifareclassic_ReadDataBlock(block, data)) {
                            for (int j = 0; j < 16; j++) {
                                uint8_t b = data[j];
                                // 【核心修复 2】：保留中文！过滤底层控制符
                                if ((b >= 32 && b <= 126) || (b >= 128 && b != 0xFE)) {
                                    raw_text += (char)b;
                                }
                            }
                        }
                    } else {
                        break; // 如果密码不对，后面的就不读了
                    }
                }

                // 【核心修复 3】：精准切除 NDEF 垃圾头
                int start_idx = -1;
                if ((start_idx = raw_text.indexOf("PRE:")) != -1) {}
                else if ((start_idx = raw_text.indexOf("SCH:")) != -1) {}
                else if ((start_idx = raw_text.indexOf("CMD:")) != -1) {}
                else if ((start_idx = raw_text.indexOf("ALM:")) != -1) {}
                else if ((start_idx = raw_text.indexOf("TXT:")) != -1) {}
                
                if (start_idx != -1) {
                    // 只截取真正的业务指令部分
                    String clean_text = raw_text.substring(start_idx);
                    Serial.printf("[NFC] M1 卡完美提取: %s\n", clean_text.c_str());
                    
                    snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                    sysAudio.playTone(4000, 50);   // 更长一点的成功音效
                    sysHaptic.playConfirm();       // 更爽快的确认震动
                    SysEvent_Publish(EVT_NFC_SCANNED, &payload);
                }
            }
           // 【分支 B】：NTAG 贴纸
            else if (uidLength == 7) {
                sprintf(payload.uid, "%02X%02X%02X%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
                
                uint8_t data[4];
                String raw_text = "";
                
                // 【核心修复 1】：扩大读取范围！连读 20 页 (80字节)，保证长指令也能装下
                for (uint8_t page = 4; page < 64; page++) {
                    if (nfc.mifareultralight_ReadPage(page, data)) {
                        for (int i = 0; i < 4; i++) {
                            uint8_t b = data[i];
                            // 【核心修复 2】：保留中文！
                            // 允许 ASCII 字符 (32~126) 和 UTF-8 中文字节 (>= 128)
                            // 过滤掉 0~31 的 NDEF 二进制控制字符和 0xFE 结束符
                            if ((b >= 32 && b <= 126) || (b >= 128 && b != 0xFE)) {
                                raw_text += (char)b;
                            }
                        }
                    } else {
                        break; // 如果读不到这页了，提前结束
                    }
                }
                
                // 【核心修复 3】：精准切除 NDEF 垃圾头 (比如 Ten 或 zh)
                int start_idx = -1;
                if ((start_idx = raw_text.indexOf("PRE:")) != -1) {}
                else if ((start_idx = raw_text.indexOf("SCH:")) != -1) {}
                else if ((start_idx = raw_text.indexOf("CMD:")) != -1) {}
                else if ((start_idx = raw_text.indexOf("ALM:")) != -1) {}
                else if ((start_idx = raw_text.indexOf("TXT:")) != -1) {}
                
                if (start_idx != -1) {
                    // 只截取指令开头到结尾的部分，彻底抛弃包头
                    String clean_text = raw_text.substring(start_idx);
                    Serial.printf("[NFC] NTAG 完美提取: %s\n", clean_text.c_str());
                    
                    snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                    sysAudio.playTone(4000, 50);   // 更长一点的成功音效
                    sysHaptic.playConfirm();       // 更爽快的确认震动
                    SysEvent_Publish(EVT_NFC_SCANNED, &payload);
                }
            }
            // 防连刷冷冻期：读成功后休息 1.5 秒
            vTaskDelay(pdMS_TO_TICKS(1500)); 
        }
        
        // 每次轮询结束后，让出 50 毫秒的 CPU 喘息时间，防止看门狗报警
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void SysNFC::begin() {
    // 1. 物理复位
    pinMode(PIN_NFC_RESET, OUTPUT);
    digitalWrite(PIN_NFC_RESET, LOW); 
    delay(50);
    digitalWrite(PIN_NFC_RESET, HIGH); 
    delay(50);

    // 2. 抢占专属总线
    Wire.begin(PIN_NFC_SDA, PIN_NFC_SCL);
    Wire.setClock(100000); 

    // 3. 启动第三方库
    nfc.begin(); 

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        Serial.println("[NFC] 致命错误：模块离线！");
        return;
    }
    nfc.SAMConfig(); 

    // 4. 启动后台轮询幽灵线程
    xTaskCreate(nfc_bg_task, "NFC_Task", 4096, NULL, 2, &nfcTaskHandle);
    Serial.println("[NFC] 轮询嗅探引擎已启动 (彻底告别中断 Bug 版).");
}