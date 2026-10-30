/*
【模块职责】蓝牙传输层实现（协议 v2）。
- 接收：RX 特征的写入回调只把字节交给行组装器，拼成完整行后放入命令行队列，由主循环执行。
- 发送：主循环把回包字节写入 PSRAM 发送缓冲；蓝牙任务在 Core 0 上按当前 MTU 切块，用 NimBLE 主机接口逐块 Notify。
  分配 mbuf 失败或主机队列已满时保留数据稍后重试，因此不会像旧实现那样截断或丢弃超长回包。
【安全边界】INFO/PAIR 是公开状态特征；只有设备设置页才能开启配对窗口。业务 RX 要求加密和 bonding，TX 仅在安全连接完成后进入协议发送状态。
【重要约束】回调运行在 NimBLE 主机任务，不能切页面、写文件或执行命令；连接断开时清空半行数据、发送缓冲和该来源未执行的请求。
*/
#include "sys/sys_ble.h"
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "sys/sys_constants.h"
#include "sys/sys_ble_queue.h"
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include "sys/sys_config.h"
#include "sys/sys_command_registry.h"
#include "sys/sys_device_identity.h"
#include <ArduinoJson.h>
#define SYS_BLE_NATIVE_SECURITY 1
#endif

namespace
{
    NimBLEServer *s_server = nullptr;
    NimBLECharacteristic *s_tx_char = nullptr;
#if defined(SYS_BLE_NATIVE_SECURITY)
    NimBLECharacteristic *s_info_char = nullptr;
    NimBLECharacteristic *s_pair_char = nullptr;
#endif

    volatile bool s_connected = false;
    volatile bool s_subscribed = false;
#if defined(SYS_BLE_NATIVE_SECURITY)
    volatile bool s_secure = false;
#endif
    volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
#if defined(SYS_BLE_NATIVE_SECURITY)
    volatile bool s_pairing_mode = false;
    volatile uint32_t s_pairing_deadline = 0;
    bool s_peer_was_bonded = false;
#endif

    // 只在 NimBLE 主机任务里使用（写入回调、连接回调都在该任务），不需要额外加锁。
    SysCmdLineAssembler s_rx_assembler(SysCmdSource::Ble);

    // 发送缓冲：主循环写入（Core 1），蓝牙任务读出（Core 0），用自旋锁保护读写位置。
    portMUX_TYPE s_tx_lock = portMUX_INITIALIZER_UNLOCKED;
    uint8_t *s_tx_buffer = nullptr;
    size_t s_tx_capacity = 0;
    size_t s_tx_read = 0;
    size_t s_tx_used = 0;
    uint32_t s_tx_session = 1; // 在发送锁内递增；旧 Peek/请求/回包不能提交到新连接。

    // 连接字段与发送环使用同一把锁，Peek/Write 看到的句柄和就绪状态是一组完整值。
    void SetLinkState(bool connected, bool subscribed, uint16_t handle)
    {
        portENTER_CRITICAL(&s_tx_lock);
        s_connected = connected;
        s_subscribed = subscribed;
        s_conn_handle = handle;
        portEXIT_CRITICAL(&s_tx_lock);
    }

#if defined(SYS_BLE_NATIVE_SECURITY)
    bool PairingActive()
    {
        if (s_pairing_mode && s_pairing_deadline != 0 &&
            static_cast<int32_t>(millis() - s_pairing_deadline) >= 0)
        {
            s_pairing_mode = false;
            s_pairing_deadline = 0;
            Serial.println("[蓝牙] 配对窗口已超时关闭。");
        }
        return s_pairing_mode;
    }

    bool IsValidName(const String &name)
    {
        if (name.isEmpty() || name.length() > PrescriptConst::BLE_NAME_MAX_BYTES)
            return false;
        for (size_t i = 0; i < name.length(); ++i)
        {
            const uint8_t c = static_cast<uint8_t>(name[i]);
            if (c < 0x20 || c == 0x7F)
                return false;
        }
        return true;
    }

    void FillPublicInfo(JsonDocument &document)
    {
        const SysDeviceIdentitySnapshot identity = SysDeviceIdentity_GetSnapshot();
        document["proto"] = 2;
        document["name"] = sysConfig.ble_name;
        document["machine_code"] = identity.machine_code;
        document["device_id"] = identity.machine_code.length() > 6
                                      ? identity.machine_code.substring(identity.machine_code.length() - 6)
                                      : identity.machine_code;
        document["firmware"] = "2026.10";
        document["paired"] = NimBLEDevice::getNumBonds() > 0;
        document["pairing_mode"] = PairingActive();
        document["secure"] = s_secure;
        document["pairing_method"] = "secure_connections_just_works";
        document["service"] = PrescriptConst::BLE_SERVICE_UUID;
    }

    void UpdatePublicInfo()
    {
        if (s_info_char == nullptr)
            return;
        JsonDocument document;
        FillPublicInfo(document);
        String json;
        serializeJson(document, json);
        s_info_char->setValue(json.c_str());
        if (s_info_char->getSubscribedCount() > 0)
            s_info_char->notify();
        if (s_pair_char != nullptr)
            s_pair_char->setValue(PairingActive() ? "PAIRING" : "IDLE");
    }
#endif

    void ResetTx()
    {
        portENTER_CRITICAL(&s_tx_lock);
        s_tx_read = 0;
        s_tx_used = 0;
        ++s_tx_session;
        portEXIT_CRITICAL(&s_tx_lock);
    }

    // 从发送缓冲复制最多 max_bytes 字节但不移动读位置；成功发出后再调用 CommitTx。
    size_t PeekTx(uint8_t *out, size_t max_bytes, uint32_t &session, uint16_t &handle)
    {
        portENTER_CRITICAL(&s_tx_lock);
        session = s_tx_session;
        handle = s_conn_handle;
        const size_t count = (s_connected && s_subscribed)
                                 ? (s_tx_used < max_bytes ? s_tx_used : max_bytes) : 0;
        const size_t first = (s_tx_read + count <= s_tx_capacity) ? count : s_tx_capacity - s_tx_read;
        if (count > 0)
        {
            memcpy(out, s_tx_buffer + s_tx_read, first);
            if (count > first)
                memcpy(out + first, s_tx_buffer, count - first);
        }
        portEXIT_CRITICAL(&s_tx_lock);
        return count;
    }

    void CommitTx(size_t count, uint32_t session)
    {
        portENTER_CRITICAL(&s_tx_lock);
        // 断开后即使新数据已填入，也不能用旧 Peek 的长度扣减新缓冲。
        if (session == s_tx_session && count <= s_tx_used && s_tx_capacity > 0)
        {
            s_tx_read = (s_tx_read + count) % s_tx_capacity;
            s_tx_used -= count;
        }
        portEXIT_CRITICAL(&s_tx_lock);
    }

    class TerminalServerCallbacks : public NimBLEServerCallbacks
    {
        void onConnect(NimBLEServer *, ble_gap_conn_desc *desc) override
        {
            SetLinkState(true, false, desc->conn_handle);
#if defined(SYS_BLE_NATIVE_SECURITY)
            s_secure = false;
            s_peer_was_bonded = NimBLEDevice::isBonded(NimBLEAddress(desc->peer_id_addr));
#endif
            s_rx_assembler.reset();
            ResetTx();
#if defined(SYS_BLE_NATIVE_SECURITY)
            UpdatePublicInfo();
#endif
        }

        void onDisconnect(NimBLEServer *, ble_gap_conn_desc *) override
        {
            SetLinkState(false, false, BLE_HS_CONN_HANDLE_NONE);
#if defined(SYS_BLE_NATIVE_SECURITY)
            s_secure = false;
            s_peer_was_bonded = false;
#endif
            s_rx_assembler.reset();
            ResetTx();
            // 旧连接留下但还没执行的请求不再有接收方，直接丢弃。
            SysCmdQueue_DropSource(SysCmdSource::Ble);
            NimBLEDevice::startAdvertising();
#if defined(SYS_BLE_NATIVE_SECURITY)
            UpdatePublicInfo();
#endif
        }

#if defined(SYS_BLE_NATIVE_SECURITY)
        uint32_t onPassKeyRequest() override
        {
            // 当前阶段使用 Secure Connections + Just Works，不要求用户输入固定 PIN。
            return 0;
        }

        void onAuthenticationComplete(ble_gap_conn_desc *desc) override
        {
            const bool encrypted = desc != nullptr && desc->sec_state.encrypted;
            const bool bonded = desc != nullptr && desc->sec_state.bonded;
            const bool accepted = encrypted && bonded &&
                                  (s_peer_was_bonded || PairingActive());
            if (accepted)
            {
                s_secure = true;
                if (!s_peer_was_bonded)
                {
                    s_pairing_mode = false;
                    s_pairing_deadline = 0;
                    Serial.println("[蓝牙] 新设备配对成功，配对窗口已关闭。");
                }
                UpdatePublicInfo();
                return;
            }

            s_secure = false;
            if (desc != nullptr)
            {
                NimBLEDevice::deleteBond(NimBLEAddress(desc->peer_id_addr));
                if (s_server != nullptr)
                    s_server->disconnect(desc->conn_handle);
            }
            Serial.println("[蓝牙] 未授权的配对请求已拒绝。");
            UpdatePublicInfo();
        }
#endif
    };

    class RxCallbacks : public NimBLECharacteristicCallbacks
    {
        // 客户端可以把一行拆成多次写入，也可以一次写入多行；这里只负责按换行拼行。
        void onWrite(NimBLECharacteristic *characteristic) override
        {
            const NimBLEAttValue value = characteristic->getValue();
            s_rx_assembler.feed(value.data(), value.size(), SysBLE_GetSession());
        }
    };

    class TxCallbacks : public NimBLECharacteristicCallbacks
    {
        void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *, uint16_t sub_value) override
        {
            const bool subscribed = (sub_value & 0x0001) != 0;
            SetLinkState(s_connected, subscribed, s_conn_handle);
            if (!subscribed)
            {
                ResetTx();
                s_rx_assembler.reset();
                SysCmdQueue_DropSource(SysCmdSource::Ble);
            }
        }
    };

    /**
     * 尝试发送一个 MTU 片段。分配/主机拥塞时保留原读指针，只有同代次 Notify 成功后才提交。
     * 独立于任务循环，便于主机测试直接覆盖真实的分包、重试及重连提交路径。
     */
    bool SendTxChunk(uint8_t *chunk, size_t max_payload)
    {
        uint32_t session = 0;
        uint16_t tx_handle = BLE_HS_CONN_HANDLE_NONE;
        size_t count = PeekTx(chunk, max_payload, session, tx_handle);
        if (count == 0)
            return false;
        // 每个片段重新按其句柄取得 MTU，避免一轮 burst 中重连后沿用旧连接的大 MTU。
        const uint16_t mtu = s_server->getPeerMTU(tx_handle);
        const size_t peer_payload = mtu > 3 ? static_cast<size_t>(mtu - 3) : 20;
        if (count > peer_payload)
            count = peer_payload;
        os_mbuf *om = ble_hs_mbuf_from_flat(chunk, count);
        if (om == nullptr)
            return false;
        // 分配期间已换连接时，释放旧片段，不能把它递交到复用的句柄。
        if (session != SysBLE_GetSession())
        {
            os_mbuf_free_chain(om);
            return false;
        }
        // 失败时 NimBLE 会释放 om；数据仍在缓冲里，下一轮重发。
        if (ble_gattc_notify_custom(tx_handle, s_tx_char->getHandle(), om) != 0)
            return false;
        CommitTx(count, session);
        return true;
    }

    /**
     * 蓝牙任务：初始化协议栈后常驻，负责发送缓冲的切块发送。
     * 每轮最多连发 8 块；mbuf 不足或主机返回错误时保留数据，下一轮重试。
     */
    void BleDaemonTask(void *)
    {
        Serial.printf("[蓝牙] 守护任务已启动，运行在核心 %d。\n", xPortGetCoreID());

 #if defined(SYS_BLE_NATIVE_SECURITY)
        NimBLEDevice::init(sysConfig.ble_name.c_str());
        // 业务特征必须走 BLE 加密；当前阶段使用 bonding + Secure Connections。
        // 屏幕配对码和 MITM 输入能力在后续 UI 阶段补齐，因此这里不使用固定 PIN。
        NimBLEDevice::setSecurityAuth(true, false, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
        // 旧固件可能已经保存了多个 bond；升级后统一清空，避免“旧手机都能继续进来”。
        // 新版本只允许从设备设置页重新建立一个最新 bonding。
        if (NimBLEDevice::getNumBonds() > 1)
        {
            NimBLEDevice::deleteAllBonds();
            Serial.println("[蓝牙] 检测到多个旧配对，已清空并要求重新配对。");
        }
        s_server = NimBLEDevice::createServer();
        s_server->setCallbacks(new TerminalServerCallbacks());

        NimBLEService *service = s_server->createService(PrescriptConst::BLE_SERVICE_UUID);
        NimBLECharacteristic *rx = service->createCharacteristic(
            PrescriptConst::BLE_RX_CHAR_UUID,
            NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
        rx->setCallbacks(new RxCallbacks());
        s_tx_char = service->createCharacteristic(
            PrescriptConst::BLE_TX_CHAR_UUID,
            NIMBLE_PROPERTY::NOTIFY);
        s_tx_char->setCallbacks(new TxCallbacks());
        s_info_char = service->createCharacteristic(
            PrescriptConst::BLE_INFO_CHAR_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        s_info_char->setValue("{}");
        s_pair_char = service->createCharacteristic(
            PrescriptConst::BLE_PAIR_CHAR_UUID,
            NIMBLE_PROPERTY::READ);
        s_pair_char->setValue("IDLE");
        service->start();

        UpdatePublicInfo();
#else
        NimBLEDevice::init(PrescriptConst::BLE_DEVICE_NAME);
        s_server = NimBLEDevice::createServer();
        s_server->setCallbacks(new TerminalServerCallbacks());
        NimBLEService *service = s_server->createService(PrescriptConst::BLE_SERVICE_UUID);
        NimBLECharacteristic *rx = service->createCharacteristic(
            PrescriptConst::BLE_RX_CHAR_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
        rx->setCallbacks(new RxCallbacks());
        s_tx_char = service->createCharacteristic(
            PrescriptConst::BLE_TX_CHAR_UUID,
            NIMBLE_PROPERTY::NOTIFY);
        s_tx_char->setCallbacks(new TxCallbacks());
        service->start();
#endif

        NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
#if defined(SYS_BLE_NATIVE_SECURITY)
        advertising->setName(sysConfig.ble_name.c_str());
#endif
        advertising->addServiceUUID(PrescriptConst::BLE_SERVICE_UUID);
        // 128 位 UUID 加设备名放不进 31 字节广播包，设备名放到扫描响应里。
        advertising->setScanResponse(true);
        advertising->start();
        Serial.println("[蓝牙] 协议 v2 服务已开始广播。");

        uint8_t chunk[512];
#if defined(SYS_BLE_NATIVE_SECURITY)
        uint32_t last_info_refresh = millis();
#endif
        while (true)
        {
#if defined(SYS_BLE_NATIVE_SECURITY)
            if (millis() - last_info_refresh >= 1000)
            {
                last_info_refresh = millis();
                // 配对窗口会自然超时；周期刷新公开特征，避免手机读到过期状态。
                PairingActive();
                UpdatePublicInfo();
            }
#endif
            bool sent = false;
            if (s_connected && s_subscribed && s_tx_buffer != nullptr)
            {
                for (int burst = 0; burst < 8; ++burst)
                {
                    if (!SendTxChunk(chunk, sizeof(chunk)))
                        break;
                    sent = true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(sent ? 2 : 10));
        }
    }
}

void SysBLE_Init()
{
    s_tx_capacity = PrescriptConst::BLE_TX_BUFFER_BYTES;
    s_tx_buffer = static_cast<uint8_t *>(heap_caps_malloc(s_tx_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_tx_buffer == nullptr)
    {
        // PSRAM 不可用时退回 4 KB 内部内存，长列表会更慢但仍可工作。
        s_tx_capacity = 4096;
        s_tx_buffer = static_cast<uint8_t *>(heap_caps_malloc(s_tx_capacity, MALLOC_CAP_8BIT));
        Serial.println("[蓝牙] PSRAM 发送缓冲分配失败，改用 4 KB 内部内存。");
    }
    if (s_tx_buffer == nullptr)
    {
        s_tx_capacity = 0;
        Serial.println("[蓝牙] 发送缓冲分配失败，本次启动无法回包。");
    }

    const BaseType_t created = xTaskCreatePinnedToCore(BleDaemonTask, "BLE_Daemon", 4096, nullptr, 1, nullptr, 0);
    if (created != pdPASS)
        Serial.println("[蓝牙] 守护任务创建失败，蓝牙服务本次启动不可用。");
}

bool SysBLE_Write(const char *data, size_t length, uint32_t session)
{
    if (data == nullptr || length == 0)
        return true;
    bool written = false;
    portENTER_CRITICAL(&s_tx_lock);
    if (s_connected && s_subscribed && s_tx_buffer != nullptr &&
        session == s_tx_session && s_tx_capacity - s_tx_used >= length)
    {
        const size_t write_pos = (s_tx_read + s_tx_used) % s_tx_capacity;
        const size_t first = (write_pos + length <= s_tx_capacity) ? length : s_tx_capacity - write_pos;
        memcpy(s_tx_buffer + write_pos, data, first);
        if (length > first)
            memcpy(s_tx_buffer, data + first, length - first);
        s_tx_used += length;
        written = true;
    }
    portEXIT_CRITICAL(&s_tx_lock);
    return written;
}

size_t SysBLE_TxFree()
{
    portENTER_CRITICAL(&s_tx_lock);
    const size_t free_bytes = s_tx_capacity - s_tx_used;
    portEXIT_CRITICAL(&s_tx_lock);
    return free_bytes;
}

bool SysBLE_IsLinkReady()
{
    portENTER_CRITICAL(&s_tx_lock);
#if defined(SYS_BLE_NATIVE_SECURITY)
    const bool ready = s_connected && s_secure && s_subscribed && s_tx_buffer != nullptr;
#else
    const bool ready = s_connected && s_subscribed && s_tx_buffer != nullptr;
#endif
    portEXIT_CRITICAL(&s_tx_lock);
    return ready;
}

#if defined(SYS_BLE_NATIVE_SECURITY)
bool SysBLE_IsSecure()
{
    portENTER_CRITICAL(&s_tx_lock);
    const bool secure = s_secure;
    portEXIT_CRITICAL(&s_tx_lock);
    return secure;
}

bool SysBLE_IsPairingMode()
{
    const bool active = PairingActive();
    UpdatePublicInfo();
    return active;
}

uint32_t SysBLE_PairingRemainingMs()
{
    if (!PairingActive())
        return 0;
    const uint32_t now = millis();
    return s_pairing_deadline > now ? s_pairing_deadline - now : 0;
}

void SysBLE_EnterPairingMode(bool replace_existing)
{
    if (replace_existing)
    {
        // 先断开旧手机，再清空 bonding；否则旧连接仍可能持有已加密会话。
        if (s_server != nullptr && s_connected && s_conn_handle != BLE_HS_CONN_HANDLE_NONE)
            s_server->disconnect(s_conn_handle);
        NimBLEDevice::deleteAllBonds();
        s_secure = false;
    }
    s_pairing_mode = true;
    s_pairing_deadline = millis() + PrescriptConst::BLE_PAIRING_WINDOW_MS;
    Serial.println("[蓝牙] 已开启 120 秒配对窗口。");
    UpdatePublicInfo();
}

void SysBLE_ExitPairingMode()
{
    s_pairing_mode = false;
    s_pairing_deadline = 0;
    Serial.println("[蓝牙] 配对窗口已关闭。");
    UpdatePublicInfo();
}

bool SysBLE_ClearBonds()
{
    NimBLEDevice::deleteAllBonds();
    // 当前链路已经完成加密，保留本次连接到 ACK 发出；清除结果从下一次连接生效。
    // 立即把 s_secure 置 false 会让命令框架丢掉本命令的最终 ACK。
    UpdatePublicInfo();
    return NimBLEDevice::getNumBonds() == 0;
}

bool SysBLE_SetDeviceName(const String &name)
{
    if (!IsValidName(name))
        return false;
    sysConfig.ble_name = name;
    sysConfig.saveCommon();
    NimBLEDevice::setDeviceName(name.c_str());
    if (s_server != nullptr && !s_connected)
    {
        NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
        advertising->setName(name.c_str());
        advertising->start();
    }
    UpdatePublicInfo();
    return true;
}

// ===== 【蓝牙命令区】安全状态、名称和配对 =====
static void Cmd_BleInfo(SysCmdCtx &ctx, const SysCmdArgs &)
{
    JsonDocument document;
    FillPublicInfo(document);
    ctx.data(document);
    ctx.ok("OK");
}

static void Cmd_BleStatus(SysCmdCtx &ctx, const SysCmdArgs &)
{
    JsonDocument document;
    document["secure"] = SysBLE_IsSecure();
    document["pairing_mode"] = SysBLE_IsPairingMode();
    document["remaining_ms"] = SysBLE_PairingRemainingMs();
    document["bonds"] = NimBLEDevice::getNumBonds();
    ctx.data(document);
    ctx.ok("OK");
}

static const SysCmdArgSpec kBleNameArgs[] = {
    SYS_ARG_REST("name", 1, PrescriptConst::BLE_NAME_MAX_BYTES, ARG_TRIM),
};

static void Cmd_BleName(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    if (!SysBLE_SetDeviceName(args.string(0)))
    {
        ctx.error("INVALID_NAME");
        return;
    }
    ctx.ok("RENAMED", args.string(0));
}

static void Cmd_BleNameReset(SysCmdCtx &ctx, const SysCmdArgs &)
{
    if (!SysBLE_SetDeviceName(PrescriptConst::BLE_DEVICE_NAME))
    {
        ctx.error("NAME_SAVE_FAILED");
        return;
    }
    ctx.ok("RESET", PrescriptConst::BLE_DEVICE_NAME);
}

static void Cmd_BleClearBonds(SysCmdCtx &ctx, const SysCmdArgs &)
{
    if (!SysBLE_ClearBonds())
        ctx.error("BONDS_CLEAR_FAILED");
    else
        ctx.ok("CLEARED");
}

static const SysCmdSpec kBleCmds[] = {
    {"INFO", nullptr, 0, 0, Cmd_BleInfo, nullptr},
    {"STATUS", nullptr, 0, 0, Cmd_BleStatus, nullptr},
    {"NAME", kBleNameArgs, SYS_COUNT_OF(kBleNameArgs), 0, Cmd_BleName, nullptr},
    {"NAME_RESET", nullptr, 0, 0, Cmd_BleNameReset, nullptr},
    {"BONDS_CLEAR", nullptr, 0, 0, Cmd_BleClearBonds, nullptr},
};

static const SysCmdGroup kBleGroup = {"BLE", 1, SRC_BLE, kBleCmds, SYS_COUNT_OF(kBleCmds)};

void SysBLE_RegisterCommands()
{
    SysCommand_RegisterGroup(&kBleGroup);
}
#else
bool SysBLE_IsSecure() { return false; }
bool SysBLE_IsPairingMode() { return false; }
uint32_t SysBLE_PairingRemainingMs() { return 0; }
void SysBLE_EnterPairingMode(bool) {}
void SysBLE_ExitPairingMode() {}
bool SysBLE_ClearBonds() { return false; }
bool SysBLE_SetDeviceName(const String &) { return false; }
void SysBLE_RegisterCommands() {}
#endif

uint32_t SysBLE_GetSession()
{
    portENTER_CRITICAL(&s_tx_lock);
    const uint32_t session = s_tx_session;
    portEXIT_CRITICAL(&s_tx_lock);
    return session;
}

size_t SysBLE_TxCapacity()
{
    return s_tx_capacity; // 只在启动时写入，之后不再改变。
}
