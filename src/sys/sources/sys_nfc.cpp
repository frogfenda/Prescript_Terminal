/*
【模块职责】PN532 NFC 实现。常态扫描 M1/NTAG 文本命令并投入主循环命令队列；伪装模式下手写 PN532 target/APDU 流程向手机暴露 LimbusCompany NDEF。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_nfc.cpp
#include "sys/sys_nfc.h"
#include "sys/sys_constants.h"
#include "sys/sys_config.h"
#include "sys/sys_event.h"
#include "sys/sys_ble_queue.h"
#include "sys/sys_haptic.h"
#include "sys/sys_audio.h"
#include "bsp/bsp_nfc_pn532.h"

SysNFC sysNfc;
TaskHandle_t nfcTaskHandle = NULL;

volatile bool g_nfc_is_emulating = false;
volatile bool g_nfc_just_started_emu = false;
volatile uint32_t g_nfc_emu_end_time = 0;

// 【稳定性参数】普通“没有卡”不是错误，因此空轮询只做低频自恢复；真正读到 UID 后的块/页异常才快速触发恢复。
static constexpr uint16_t NFC_IDLE_RECOVER_LIMIT = 300;       // 约 90 秒无卡轮询后轻量重置一次 PN532，防止芯片偶发假死。
static constexpr uint8_t NFC_CARD_ERROR_RECOVER_LIMIT = 3;    // 连续 3 次读到卡但底层读失败后重置 PN532。
static uint16_t g_nfc_idle_miss_count = 0;
static uint8_t g_nfc_card_error_count = 0;

// 【新增】PN532 在线状态机。以前开机初始化失败会直接不创建任务，运行中恢复失败也会继续硬扫，
// 容易表现为“刚开机能扫、过一会儿扫不出”或“开机偶发离线”。现在任务常驻，离线时按间隔自恢复。
static constexpr uint32_t NFC_OFFLINE_RETRY_MS = 2500;
static volatile bool g_nfc_ready = false;
static volatile uint32_t g_nfc_next_retry_ms = 0;

// 【新增】休眠采用协作式安全点，而不是直接 vTaskSuspend 半截 SPI 事务。
static volatile bool g_nfc_sleep_request = false;
static volatile bool g_nfc_sleep_ack = false;

void nfc_bg_task(void *pvParameters);
static bool nfc_reinitialize(const char *reason, bool long_boot_wait);
static void nfc_start_task_if_needed();
static void nfc_enter_offline(const char *reason, uint32_t retry_delay_ms = NFC_OFFLINE_RETRY_MS);

// 【函数说明】启动 60 秒靶卡伪装窗口，设置结束时间和首次复位标志，并播放确认反馈。
void SysNfc_StartEmulation()
{
    g_nfc_is_emulating = true;
    g_nfc_just_started_emu = true;
    g_nfc_emu_end_time = millis() + 60000;
    Serial.println("[NFC-硬件SPI] 停止主动雷达，进入【边狱巴士】模拟伪装模式 60 秒！");
    SYS_SOUND_CONFIRM();
}

// 【函数说明】返回伪装状态；如果当前时间超过结束时间，先自动清除伪装标志。
bool SysNfc_IsEmulating()
{
    if (g_nfc_is_emulating && millis() >= g_nfc_emu_end_time)
    {
        g_nfc_is_emulating = false;
    }
    return g_nfc_is_emulating;
}
// 【函数说明】提前终止伪装：把结束时间置 0，让后台 APDU 循环在下一次检查时退出并复位 PN532。
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

// 【函数说明】计算伪装窗口剩余秒数，HUD 显示 BUS 倒计时使用。
int SysNfc_GetEmulationRemainingSeconds()
{
    if (!SysNfc_IsEmulating()) return 0;
    uint32_t now = millis();
    if (g_nfc_emu_end_time <= now) return 0;
    return (int)((g_nfc_emu_end_time - now) / 1000);
}

uint8_t cc_file[] = {0x00, 0x0F, 0x20, 0x00, 0x3B, 0x00, 0x34, 0x04, 0x06, 0xE1, 0x04, 0x00, 0xFF, 0x00, 0x00};
uint8_t ndef_file[] = {
    0x00, 0x2F, 0xD4, 0x0F, 0x1D,
    'a', 'n', 'd', 'r', 'o', 'i', 'd', '.', 'c', 'o', 'm', ':', 'p', 'k', 'g',
    'c', 'o', 'm', '.', 'P', 'r', 'o', 'j', 'e', 'c', 't', 'M', 'o', 'o', 'n', '.', 'L', 'i', 'm', 'b', 'u', 's', 'C', 'o', 'm', 'p', 'a', 'n', 'y'};

// 【函数说明】按 PN532 SPI 帧格式手写命令发送：写 preamble/len/checksum/data，轮询 ready 状态并读取 ACK。
bool raw_sendCommand(uint8_t *cmd, uint8_t cmdlen)
{
    return BSP::NfcPn532::RawSendCommand(cmd, cmdlen);
}

// 【函数说明】按 PN532 SPI 帧格式读取响应：等待 ready，校验 preamble 与长度校验，再把 payload 拷入缓冲。
int raw_readResponse(uint8_t *buf, uint8_t maxlen, uint16_t timeout)
{
    return BSP::NfcPn532::RawReadResponse(buf, maxlen, timeout);
}

// 【函数说明】硬复位 PN532：拉低 RESET、恢复 HIGH，再轻触 SS，清理 target 模式或读卡异常状态。
void raw_reset()
{
    BSP::NfcPn532::Reset();
}

// 【函数说明】统一重新初始化 PN532。用于开机、唤醒、伪装结束和读卡异常自恢复，避免不同路径各写一套 begin/SAMConfig。
static bool nfc_reinitialize(const char *reason, bool long_boot_wait)
{
    bool ok = BSP::NfcPn532::Reinitialize(reason, long_boot_wait);
    if (!ok)
        return false;
    g_nfc_idle_miss_count = 0;
    g_nfc_card_error_count = 0;
    return true;
}

// 【函数说明】启动后台任务；重复调用不会创建第二个 NFC 任务。
static void nfc_start_task_if_needed()
{
    if (nfcTaskHandle != NULL)
        return;

    xTaskCreatePinnedToCore(
        nfc_bg_task,
        "nfc_task",
        8192,
        NULL,
        5,
        &nfcTaskHandle,
        1);
}

// 【函数说明】把 PN532 标记为离线，并安排后台任务稍后重试初始化。
// 这里不直接阻塞反复初始化，避免 UI/音频被 NFC 硬件异常拖住。
static void nfc_enter_offline(const char *reason, uint32_t retry_delay_ms)
{
    g_nfc_ready = false;
    g_nfc_idle_miss_count = 0;
    g_nfc_card_error_count = 0;
    g_nfc_next_retry_ms = millis() + retry_delay_ms;
    Serial.printf("[NFC-离线] %s，%lu ms 后后台重试。\n",
                  reason ? reason : "PN532 暂不可用",
                  (unsigned long)retry_delay_ms);
}

// 【函数说明】在原始卡片文本中查找协议层支持的最早命令头，避免 NFC 和 BLE 可用命令不一致。
static int nfc_find_command_start(const String &raw_text)
{
    static const char *kPrefixes[] = {
        "GET:SPC_TXT:",
        "GET:SYNC",
        "GET:LANG",
        "GET:INFO",
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

    int min_idx = 9999;
    for (size_t i = 0; i < sizeof(kPrefixes) / sizeof(kPrefixes[0]); ++i)
    {
        int idx = raw_text.indexOf(kPrefixes[i]);
        if (idx != -1 && idx < min_idx)
            min_idx = idx;
    }
    return min_idx;
}

// 【函数说明】NFC 后台任务只入队，不直接切页面/写文件；协议路由统一回到 AppManager 主循环执行。
static void nfc_enqueue_command(const String &clean_text)
{
    SysBleQueue_Push(clean_text);
    Serial.printf("[NFC] 指令已进入主循环队列: %s\n", clean_text.c_str());
}

// 【函数说明】普通无卡轮询失败是正常状态，只在持续很久无响应时低频重置 PN532，防止模块假死。
static void nfc_note_idle_poll_miss()
{
    if (++g_nfc_idle_miss_count >= NFC_IDLE_RECOVER_LIMIT)
    {
        g_nfc_idle_miss_count = 0;
        if (nfc_reinitialize("长时间未检测到卡片，执行低频健康重置", false))
        {
            g_nfc_ready = true;
        }
        else
        {
            nfc_enter_offline("健康重置失败，PN532 进入离线重试态", 1200);
        }
    }
}

// 【函数说明】读到 UID 但块/页读取连续失败时执行恢复；不改变 M1/NTAG 当前读卡完整性策略。
static void nfc_note_card_read_error(const char *reason)
{
    if (++g_nfc_card_error_count >= NFC_CARD_ERROR_RECOVER_LIMIT)
    {
        g_nfc_card_error_count = 0;
        if (nfc_reinitialize(reason ? reason : "连续卡片读取异常", false))
        {
            g_nfc_ready = true;
        }
        else
        {
            nfc_enter_offline(reason ? reason : "连续卡片读取异常，PN532 恢复失败", 1200);
        }
    }
}

static void nfc_clear_health_counters()
{
    g_nfc_idle_miss_count = 0;
    g_nfc_card_error_count = 0;
}

// ==========================================
// 【低功耗控制接口】：随时物理断电
// ==========================================
// ==========================================
// 【低功耗控制接口】：随时物理断电 + 锁死引脚防漏电
// ==========================================
// 【函数说明】休眠前请求 NFC 任务进入安全点，再拉低并 hold RESET 引脚。
// 重点：不再 vTaskSuspend 半截 SPI 读卡流程，避免唤醒后 Adafruit PN532/总线状态不一致。
void SysNfc_Sleep()
{
    g_nfc_sleep_request = true;

    // 等后台任务在循环安全点确认暂停。若 PN532 正在一次短读卡中，通常会在数百毫秒内退出。
    uint32_t wait_start = millis();
    while (!g_nfc_sleep_ack && (millis() - wait_start) < 900)
    {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (!g_nfc_sleep_ack)
    {
        Serial.println("[NFC-电源管理] 等待 NFC 安全暂停超时，继续执行硬件休眠。若后续异常，后台会重新初始化。");
    }

    g_nfc_ready = false;
    BSP::NfcPn532::Sleep();

    Serial.println("[NFC-电源管理] 模块已进入休眠，射频天线关闭并锁定引脚。");
}

// 【函数说明】唤醒后解除 GPIO hold，不在 UI 线程里直接硬扫；交给 NFC 后台任务重新初始化。
void SysNfc_Wakeup()
{
    BSP::NfcPn532::WakeupPins();

    g_nfc_ready = false;
    g_nfc_sleep_ack = false;
    g_nfc_sleep_request = false;
    g_nfc_next_retry_ms = millis();

    nfc_start_task_if_needed();
    Serial.println("[NFC-电源管理] 模块已唤醒，后台任务将重新初始化 PN532。");
}
// 【函数说明】NFC 后台任务主循环：普通模式扫描 M1/NTAG 命令卡，伪装模式进入 PN532 target/APDU 流程向手机投递 NDEF。
void nfc_bg_task(void *pvParameters)
{
    while (true)
    {
        // 【安全休眠点】只有在这里暂停 NFC 轮询，避免从 SPI 事务中间强行 suspend。
        if (g_nfc_sleep_request)
        {
            g_nfc_sleep_ack = true;
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }
        g_nfc_sleep_ack = false;

        if (sysConfig.nfc_mode != 0)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // 【离线重试态】开机初始化失败、运行中健康恢复失败后，都在这里后台重试，
        // 不再让 NFC 服务永久离线。
        if (!g_nfc_ready)
        {
            if (g_nfc_is_emulating && millis() >= g_nfc_emu_end_time)
            {
                g_nfc_is_emulating = false;
                Serial.println("[NFC-自恢复] 伪装窗口已超时，但 PN532 尚未恢复，保持离线重试。");
            }

            uint32_t now = millis();
            if ((int32_t)(now - g_nfc_next_retry_ms) >= 0)
            {
                if (nfc_reinitialize("后台离线重试", false))
                {
                    g_nfc_ready = true;
                    Serial.println("[NFC-自恢复] PN532 后台重试成功，恢复主动读卡。");
                }
                else
                {
                    g_nfc_next_retry_ms = millis() + NFC_OFFLINE_RETRY_MS;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (g_nfc_is_emulating)
        {
            if (millis() > g_nfc_emu_end_time)
            {
                Serial.println("[NFC-硬件SPI] 60秒伪装结束，恢复主动雷达扫描！");
                Feedback_PlayKnobTick();
                g_nfc_is_emulating = false;

                if (nfc_reinitialize("伪装窗口结束，恢复主动读卡", false))
                    g_nfc_ready = true;
                else
                    nfc_enter_offline("伪装结束后 PN532 恢复失败", 1200);
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
                    Feedback_PlayConfirm();
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
                        Feedback_PlayConfirm();
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
        uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
        uint8_t uidLength;

        bool success = BSP::NfcPn532::ReadPassiveTarget(uid, &uidLength, 150);
        bool has_valid_cmd = false;

        if (!success)
        {
            nfc_note_idle_poll_miss();
        }

        if (success)
        {
            g_nfc_idle_miss_count = 0;
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
                                BSP::NfcPn532::ReadPassiveTarget(d_uid, &d_len, 50);
                            }

                            if (BSP::NfcPn532::MifareAuth(uid, uidLength, block, keys[test_idx]))
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
                            BSP::NfcPn532::ReadPassiveTarget(d_uid, &d_len, 50); // 同样 50ms 超时
                            BSP::NfcPn532::MifareAuth(uid, uidLength, block, keys[current_key_idx]);
                        }

                        if (BSP::NfcPn532::MifareReadBlock(block, data))
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
                    Feedback_PlayNfcReadError();
                    nfc_note_card_read_error("M1 连续块读取异常");
                }
                else
                {
                    int min_idx = nfc_find_command_start(raw_text);

                    if (min_idx != 9999)
                    {
                        String clean_text = raw_text.substring(min_idx);
                        Serial.printf("[NFC] 提取指令: %s\n", clean_text.c_str());
                        snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                        Feedback_PlayNfcReadOk();
                        nfc_enqueue_command(clean_text);
                        has_valid_cmd = true;
                        nfc_clear_health_counters();
                    }
                    else
                    {
                        if (raw_text.length() > 0)
                            Serial.printf("[NFC-警告] 未发现有效指令头: %s\n", raw_text.c_str());
                        else
                            Serial.println("[NFC-警告] 卡片内容为空！");
                        Feedback_PlayNfcReadError();
                    }
                }
            }
            else if (uidLength == 7)
            {
                sprintf(payload.uid, "%02X%02X%02X%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
                Serial.printf("[NFC-官方] 发现 NTAG, UID: %s\n", payload.uid);

                String raw_text = "";
                bool stop_reading = false; // 同样恢复提前结束
                bool ntag_page_error = false;

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
                            BSP::NfcPn532::ReadPassiveTarget(d_uid, &d_len, 50);
                        }

                        uint8_t data[4];
                        if (BSP::NfcPn532::NtagReadPage(page, data))
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
                    {
                        ntag_page_error = true;
                        break;
                    }
                }

                int min_idx = nfc_find_command_start(raw_text);

                if (min_idx != 9999)
                {
                    String clean_text = raw_text.substring(min_idx);
                    Serial.printf("[NFC] 提取指令: %s\n", clean_text.c_str());
                    snprintf(payload.payload, sizeof(payload.payload), "%s", clean_text.c_str());
                    Feedback_PlayNfcReadOk();
                    nfc_enqueue_command(clean_text);
                    has_valid_cmd = true;
                    nfc_clear_health_counters();
                }
                else
                {
                    if (raw_text.length() > 0)
                        Serial.printf("[NFC-警告] 未发现有效指令头: %s\n", raw_text.c_str());
                    else
                        Serial.println("[NFC-警告] NTAG 内容为空！");
                    if (ntag_page_error)
                        nfc_note_card_read_error("NTAG 连续页读取异常");
                    Feedback_PlayNfcReadError();
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
    if (nfc_reinitialize("开机初始化", true))
    {
        g_nfc_ready = true;
        Serial.println("[NFC-硬件SPI] 天线点火成功！纯血引擎已启动 (高速稳定读卡 + 硬件级穿透模拟)...");
    }
    else
    {
        // 开机瞬间 PN532 没响应时，不再让 NFC 永久离线；后台任务会持续重试。
        nfc_enter_offline("开机初始化失败，NFC 服务进入后台重试态", 1200);
    }

    nfc_start_task_if_needed();
}

// 【接口说明】保留旧接口，当前 NFC 采用后台常扫模式；调用时只打印状态，不直接在 UI 线程读卡。
void SysNFC::triggerManualScan()
{
    Serial.println("[NFC] 手动扫描请求已收到：当前版本使用后台常扫，无需额外触发。");
}



