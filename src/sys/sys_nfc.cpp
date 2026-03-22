#include "sys_nfc.h"
#include "sys_config.h"
#include "sys_event.h"
#include "sys_haptic.h"
#include "sys_audio.h"
#include <SPI.h>
#include <Adafruit_PN532.h>

// ==========================================
// 纯净 SPI 引脚
// ==========================================
#define PIN_NFC_SCK   1
#define PIN_NFC_MISO  2
#define PIN_NFC_MOSI  47
#define PIN_NFC_SS    15
#define PIN_NFC_RESET 39

Adafruit_PN532 nfc(PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, PIN_NFC_SS);

SysNFC sysNfc;
TaskHandle_t nfcTaskHandle = NULL;

bool g_nfc_is_emulating = false;
bool g_nfc_just_started_emu = false; 
uint32_t g_nfc_emu_end_time = 0;

void SysNfc_StartEmulation() {
    g_nfc_is_emulating = true;
    g_nfc_just_started_emu = true; 
    g_nfc_emu_end_time = millis() + 60000; 
    Serial.println("[NFC-SPI穿透] 停止主动雷达，进入【边狱巴士】模拟伪装模式 60 秒！");
    SYS_SOUND_CONFIRM();
}

bool SysNfc_IsEmulating() {
    if (g_nfc_is_emulating && millis() >= g_nfc_emu_end_time) {
        g_nfc_is_emulating = false; 
    }
    return g_nfc_is_emulating;
}

uint8_t cc_file[] = { 0x00, 0x0F, 0x20, 0x00, 0x3B, 0x00, 0x34, 0x04, 0x06, 0xE1, 0x04, 0x00, 0xFF, 0x00, 0x00 };
uint8_t ndef_file[] = {
    0x00, 0x2F, 0xD4, 0x0F, 0x1D, 
    'a','n','d','r','o','i','d','.','c','o','m',':','p','k','g',
    'c','o','m','.','P','r','o','j','e','c','t','M','o','o','n','.','L','i','m','b','u','s','C','o','m','p','a','n','y'
};

void spi_write(uint8_t c) {
    for (int i = 0; i < 8; i++) {
        digitalWrite(PIN_NFC_MOSI, (c & (1 << i)) ? HIGH : LOW);
        digitalWrite(PIN_NFC_SCK, HIGH);
        digitalWrite(PIN_NFC_SCK, LOW);
    }
}

uint8_t spi_read() {
    uint8_t x = 0;
    for (int i = 0; i < 8; i++) {
        digitalWrite(PIN_NFC_SCK, HIGH);
        if (digitalRead(PIN_NFC_MISO)) x |= (1 << i);
        digitalWrite(PIN_NFC_SCK, LOW);
    }
    return x;
}

bool isReady() {
    digitalWrite(PIN_NFC_SS, LOW);
    delay(2);
    spi_write(0x02); 
    uint8_t status = spi_read();
    digitalWrite(PIN_NFC_SS, HIGH);
    return status == 0x01;
}

bool raw_sendCommand(uint8_t* cmd, uint8_t cmdlen) {
    digitalWrite(PIN_NFC_SS, LOW);
    delay(2);
    spi_write(0x01); 
    spi_write(0x00); spi_write(0x00); spi_write(0xFF);
    
    uint8_t len = cmdlen + 1;
    spi_write(len); spi_write(~len + 1);
    spi_write(0xD4);
    
    uint8_t sum = 0xD4;
    for(int i = 0; i < cmdlen; i++) {
        spi_write(cmd[i]);
        sum += cmd[i];
    }
    spi_write(~sum + 1); spi_write(0x00);
    digitalWrite(PIN_NFC_SS, HIGH);

    uint16_t t = 1000;
    while(!isReady() && t > 0) { delay(1); t--; }
    if(t == 0) return false;

    digitalWrite(PIN_NFC_SS, LOW);
    delay(1);
    spi_write(0x03); 
    for(int i = 0; i < 6; i++) spi_read(); 
    digitalWrite(PIN_NFC_SS, HIGH);
    return true;
}

int raw_readResponse(uint8_t* buf, uint8_t maxlen, uint16_t timeout) {
    uint16_t t = 0;
    while(!isReady()) { 
        delay(1); t++; 
        if(t > timeout) return -1; 
    }

    digitalWrite(PIN_NFC_SS, LOW);
    delay(1);
    spi_write(0x03); 

    if (spi_read() != 0x00 || spi_read() != 0x00 || spi_read() != 0xFF) {
        digitalWrite(PIN_NFC_SS, HIGH); return -1;
    }
    
    uint8_t len = spi_read();
    if ((uint8_t)(len + spi_read()) != 0) {
        digitalWrite(PIN_NFC_SS, HIGH); return -1;
    }

    spi_read(); spi_read(); 
    
    int actual_len = len - 2;
    for(int i = 0; i < actual_len; i++) {
        uint8_t b = spi_read();
        if (i < maxlen) buf[i] = b;
    }
    
    spi_read(); spi_read(); 
    digitalWrite(PIN_NFC_SS, HIGH);
    return actual_len;
}

void raw_reset() {
    digitalWrite(PIN_NFC_RESET, LOW);
    vTaskDelay(pdMS_TO_TICKS(50));
    digitalWrite(PIN_NFC_RESET, HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    digitalWrite(PIN_NFC_SS, LOW);
    vTaskDelay(pdMS_TO_TICKS(2));
    digitalWrite(PIN_NFC_SS, HIGH);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void nfc_bg_task(void *pvParameters)
{
    while (true)
    {
        if (sysConfig.nfc_mode != 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (g_nfc_is_emulating) {
            if (millis() > g_nfc_emu_end_time) {
                Serial.println("[NFC-SPI穿透] 60秒伪装结束，恢复主动雷达扫描！");
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

            if (g_nfc_just_started_emu) {
                g_nfc_just_started_emu = false;
                raw_reset(); 
            }

            uint8_t tgInitCmd[] = { 
                0x8C, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x20, 
                0x01, 0xFE, 0x05, 0x01, 0x86, 0x04, 0x02, 0x02, 0x03, 0x00, 0x4B, 0x02, 0x4F, 0x49, 0x8A, 0x00, 0xFF, 0xFF,
                0x01, 0xFE, 0x05, 0x01, 0x86, 0x04, 0x02, 0x02, 0x03, 0x00, 0x00, 0x00 
            };
            
            if (raw_sendCommand(tgInitCmd, sizeof(tgInitCmd))) {
                Serial.println("[NFC-SPI穿透] 靶卡伪装就绪，等待手机贴近...");
                uint8_t response[128];
                bool connected = false;
                
                while(g_nfc_is_emulating && millis() < g_nfc_emu_end_time) {
                    if (raw_readResponse(response, sizeof(response), 500) > 0) {
                        connected = true; break;
                    }
                }
                
                if (connected) {
                    Serial.println("[NFC-SPI穿透] 手机已碰触！建立 APDU 隧道...");
                    sysHaptic.playConfirm(); 
                    int current_file = 0; 
                    bool payload_delivered = false; // 【关键变量】：标记任务完成
                    
                    while (g_nfc_is_emulating && millis() < g_nfc_emu_end_time) {
                        uint8_t tgGet[] = { 0x86 };
                        if (!raw_sendCommand(tgGet, 1)) {
                            // 保护机制：如果发不出去，说明手机已经跑路了
                            break;
                        }
                        
                        int apdu_len = raw_readResponse(response, sizeof(response), 1000);
                        if (apdu_len <= 1) continue; 
                        
                        uint8_t* apdu = &response[1];
                        apdu_len -= 1;
                        
                        // 【新增】：打印交互日志，让你清楚看到手机读取的进度！
                        Serial.printf("[APDU] 收到指令: %02X %02X %02X %02X\n", apdu[0], apdu[1], apdu[2], apdu[3]);
                        
                        uint8_t res_apdu[128];
                        uint8_t res_len = 0;
                        
                        if (apdu[0] == 0x00 && apdu[1] == 0xA4 && apdu[2] == 0x04 && apdu[3] == 0x00) {
                            res_apdu[0] = 0x90; res_apdu[1] = 0x00; res_len = 2;
                        }
                        else if (apdu[0] == 0x00 && apdu[1] == 0xA4 && apdu[2] == 0x00 && apdu[3] == 0x0C) {
                            uint16_t file_id = (apdu[5] << 8) | apdu[6];
                            if (file_id == 0xE103) current_file = 1; 
                            else if (file_id == 0xE104) current_file = 2;
                            res_apdu[0] = 0x90; res_apdu[1] = 0x00; res_len = 2;
                        }
                        else if (apdu[0] == 0x00 && apdu[1] == 0xB0) {
                            uint16_t offset = (apdu[2] << 8) | apdu[3];
                            uint8_t length = apdu[4];
                            uint8_t* file_data = NULL;
                            uint16_t file_size = 0;
                            
                            if (current_file == 1) { file_data = cc_file; file_size = sizeof(cc_file); }
                            else if (current_file == 2) { file_data = ndef_file; file_size = sizeof(ndef_file); }
                            
                            if (file_data != NULL && offset < file_size) {
                                uint16_t bytes_to_read = length;
                                if (offset + bytes_to_read > file_size) bytes_to_read = file_size - offset; 
                                memcpy(res_apdu, file_data + offset, bytes_to_read);
                                res_apdu[bytes_to_read] = 0x90; res_apdu[bytes_to_read + 1] = 0x00;
                                res_len = bytes_to_read + 2;

                                // 【核心破局】：监控 NDEF 文件，如果手机读到了结尾，宣告胜利！
                                if (current_file == 2 && (offset + bytes_to_read >= file_size)) {
                                    payload_delivered = true;
                                }
                            } else {
                                res_apdu[0] = 0x6A; res_apdu[1] = 0x82; res_len = 2;
                            }
                        } else {
                            res_apdu[0] = 0x68; res_apdu[1] = 0x00; res_len = 2; 
                        }
                        
                        uint8_t tgSet[130];
                        tgSet[0] = 0x8E; 
                        memcpy(&tgSet[1], res_apdu, res_len);
                        if (!raw_sendCommand(tgSet, res_len + 1)) break;
                        raw_readResponse(response, sizeof(response), 1000);

                        // 【主动斩断】：数据发送完后立刻跳出循环，绝不再等下一条！
                        if (payload_delivered) {
                            Serial.println("[NFC-SPI穿透] 检测到手机已提取完整载荷！主动切断隧道！");
                            break; 
                        }
                    }
                    Serial.println("[NFC-SPI穿透] 载荷投递完毕！准备迎接下一次碰触...");
                    sysHaptic.playConfirm();

                    // 等手机完全拿开，然后彻底纯净洗刷！
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    raw_reset(); 
                }
            } else {
                Serial.println("[NFC-SPI穿透] 靶卡部署失败，洗刷芯片重试...");
                raw_reset(); 
            }
            continue; 
        }

        // =====================================
        // 日常读卡区 保持不变
        // =====================================
        uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
        uint8_t uidLength;
        bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);

        if (success) {
            Evt_NfcScanned_t payload = {0};
            if (uidLength == 4) {
                sprintf(payload.uid, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
                Serial.printf("[NFC-官方] 发现 M1 卡, UID: %s\n", payload.uid);
                vTaskDelay(pdMS_TO_TICKS(50));

                uint8_t key_factory[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                uint8_t key_ndef[6] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7};
                String raw_text = "";
                int data_blocks[] = {4, 5, 6, 8, 9, 10, 12, 13, 14, 16, 17, 18, 20, 21, 22};
                bool stop_reading = false;
                int retry_count = 0; 

                for (int i = 0; i < 15;) {
                    if (stop_reading) break;
                    int block = data_blocks[i];
                    bool auth_success = false;

                    if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, key_ndef)) auth_success = true;
                    else if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, key_factory)) auth_success = true;

                    if (auth_success) {
                        uint8_t data[16];
                        if (nfc.mifareclassic_ReadDataBlock(block, data)) {
                            for (int j = 0; j < 16; j++) {
                                uint8_t b = data[j];
                                if (b == 0xFE) { stop_reading = true; break; }
                                if (b == 0xFF || b == 0x00) continue;
                                if ((b >= 32 && b <= 126) || b >= 128) raw_text += (char)b;
                            }
                            i++; retry_count = 0; 
                        } else {
                            retry_count++;
                            if (retry_count > 3) break;                     
                            vTaskDelay(pdMS_TO_TICKS(10)); 
                        }
                    } else {
                        retry_count++;
                        if (retry_count > 3) break;
                    }
                }

                int min_idx = 9999; int idx;
                if ((idx = raw_text.indexOf("PRE:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("SCH:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("CMD:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("ALM:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("TXT:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("POM:")) != -1 && idx < min_idx) min_idx = idx;
                
                if (min_idx != 9999) {
                    String clean_text = raw_text.substring(min_idx);
                    Serial.printf("[NFC] 提取指令: %s\n", clean_text.c_str());
                    snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                    sysAudio.playTone(4000, 50); sysHaptic.playConfirm();     
                    SysEvent_Publish(EVT_NFC_SCANNED, &payload);
                }
            }
            else if (uidLength == 7) {
                // ... NTAG 逻辑保持原样
                sprintf(payload.uid, "%02X%02X%02X%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
                Serial.printf("[NFC-官方] 发现 NTAG, UID: %s\n", payload.uid);
                vTaskDelay(pdMS_TO_TICKS(50));
                
                uint8_t data[4];
                String raw_text = "";
                bool stop_reading = false;
                int retry_count = 0;

                for (uint8_t page = 4; page < 64;) {
                    if (stop_reading) break;
                    if (nfc.mifareultralight_ReadPage(page, data)) {
                        for (int i = 0; i < 4; i++) {
                            uint8_t b = data[i];
                            if (b == 0xFE) { stop_reading = true; break; }
                            if (b == 0xFF || b == 0x00) continue;
                            if ((b >= 32 && b <= 126) || b >= 128) raw_text += (char)b;
                        }
                        page++; retry_count = 0;
                    } else {
                        retry_count++;
                        if (retry_count > 3) break;                     
                        vTaskDelay(pdMS_TO_TICKS(10)); 
                    }
                }

                int min_idx = 9999; int idx;
                if ((idx = raw_text.indexOf("PRE:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("SCH:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("CMD:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("ALM:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("TXT:")) != -1 && idx < min_idx) min_idx = idx;
                if ((idx = raw_text.indexOf("POM:")) != -1 && idx < min_idx) min_idx = idx;
                
                if (min_idx != 9999) {
                    String clean_text = raw_text.substring(min_idx);
                    Serial.printf("[NFC] 提取指令: %s\n", clean_text.c_str());
                    snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                    sysAudio.playTone(4000, 50); sysHaptic.playConfirm();     
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
    pinMode(PIN_NFC_RESET, OUTPUT);
    digitalWrite(PIN_NFC_RESET, LOW);
    delay(50);
    digitalWrite(PIN_NFC_RESET, HIGH);
    delay(150);

    nfc.begin();
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        Serial.println("[NFC-官方] 致命错误：模块离线！");
        return;
    }
    
    Serial.printf("[NFC-官方] 成功连接 PN5%02X 固件版本 %d.%d\n", 
                  (versiondata >> 24) & 0xFF, 
                  (versiondata >> 16) & 0xFF, 
                  (versiondata >> 8) & 0xFF);
                  
    nfc.SAMConfig();
    xTaskCreate(nfc_bg_task, "NFC_Task", 4096, NULL, 2, &nfcTaskHandle);
    Serial.println("[NFC-官方] 混合引擎已启动 (官方极稳读卡 + 纯软件 SPI 穿透模拟).");
}