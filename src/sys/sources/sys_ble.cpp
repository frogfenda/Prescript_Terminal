/*
【模块职责】BLE 通信实现。创建 Terminal_01 服务与 BEEF 特征；网页写入的数据不直接处理，而是放入 sys_ble_queue 等主循环安全消费。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_ble.cpp
#include "sys/sys_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include "sys/sys_constants.h"
#include "sys/sys_ble_queue.h"
#include "sys/sys_runtime_status.h"

// 【关键 1】：全局特征值指针，保存下来以便随时调用 notify()
NimBLECharacteristic *g_ble_char = nullptr;
NimBLEServer *g_ble_server = nullptr;

namespace
{
    // 默认 ATT MTU 为 23，Notify 可携带 20 字节。身份 JSON 较长，不能依赖
    // 手机一定会成功协商大 MTU；所有长响应都使用分片帧，交给客户端重组。
    constexpr size_t kBleNotifyPayload = 20;
    constexpr size_t kChunkPayload = 9;
    uint8_t g_chunk_sequence = 0;
    volatile bool g_identity_notify_active = false;
    volatile uint16_t g_identity_notify_expected = 0;
    volatile uint16_t g_identity_notify_completed = 0;

    bool isUtf8Continuation(uint8_t value)
    {
        return (value & 0xC0u) == 0x80u;
    }

    void notifyFrame(const char *data, size_t length, bool pace)
    {
        if (g_ble_char == nullptr || data == nullptr || length == 0)
            return;
        g_ble_char->setValue(reinterpret_cast<const uint8_t *>(data), length);
        g_ble_char->notify();
        // 连续 Notify 之间留出一个连接事件窗口，避免手机端只收到最后一帧。
        if (pace)
            delay(40);
    }
}

class TerminalBLEServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *, ble_gap_conn_desc *desc) override
    {
        const uint16_t handle = desc != nullptr ? desc->conn_handle : 0;
        const uint16_t mtu = desc != nullptr ? ble_att_mtu(desc->conn_handle) : 0;
        Serial.printf("[BLE/诊断] 客户端已连接：句柄=%u，当前MTU=%u。\n",
                      static_cast<unsigned>(handle),
                      static_cast<unsigned>(mtu));
    }

    void onDisconnect(NimBLEServer *, ble_gap_conn_desc *desc) override
    {
        const uint16_t handle = desc != nullptr ? desc->conn_handle : 0;
        Serial.printf("[BLE/诊断] 客户端已断开：句柄=%u。\n",
                      static_cast<unsigned>(handle));
        NimBLEDevice::startAdvertising();
    }

    void onMTUChange(uint16_t mtu, ble_gap_conn_desc *desc) override
    {
        const uint16_t handle = desc != nullptr ? desc->conn_handle : 0;
        Serial.printf("[BLE/诊断] MTU协商完成：句柄=%u，MTU=%u，可用Notify载荷=%u。\n",
                      static_cast<unsigned>(handle),
                      static_cast<unsigned>(mtu),
                      static_cast<unsigned>(mtu >= 3 ? mtu - 3 : 0));
    }
};

class TerminalBLECallbacks : public NimBLECharacteristicCallbacks
{
    // 【函数说明】BLE 特征写入回调：读取网页写入的 UTF-8 命令字符串并放入 SysBleQueue，避免在 BLE 线程中直接分发事件。
    void onWrite(NimBLECharacteristic *pCharacteristic)
    {
        std::string value = pCharacteristic->getValue();

        // 命令可能包含 Wi-Fi 密码或设备通行码，串口只记录长度，不输出原始载荷。
        Serial.printf("[BLE] 已收到写入，长度=%u 字节，等待主循环处理。\n",
                      static_cast<unsigned int>(value.size()));

        // App 只回传无敏感内容的 BLE 分片诊断标记，不进入业务协议和宏引擎。
        if (value.rfind("BD:", 0) == 0)
        {
            Serial.printf("[BLE/诊断] App回执：%s。\n", value.c_str());
            return;
        }

        if (value.find("CMD:PUSH_NOW") != std::string::npos)
        {
            SysRuntime_RequestPushNotify();
        }
        else
        {
            // 不在 BLE 回调里直接路由，避免跨核心写文件/切 UI。
            SysBleQueue_Push(String(value.c_str()));
        }
    }

    void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *desc, uint16_t subValue) override
    {
        const uint16_t handle = desc != nullptr ? desc->conn_handle : 0;
        const uint16_t mtu = desc != nullptr ? ble_att_mtu(desc->conn_handle) : 0;
        Serial.printf("[BLE/诊断] Notify订阅变化：句柄=%u，订阅值=%u，MTU=%u。\n",
                      static_cast<unsigned>(handle),
                      static_cast<unsigned>(subValue),
                      static_cast<unsigned>(mtu));
    }

    void onStatus(NimBLECharacteristic *, Status status, int code) override
    {
        if (!g_identity_notify_active)
            return;
        const uint16_t completed = ++g_identity_notify_completed;
        Serial.printf("[BLE/诊断] 身份Notify发送回调：%u/%u，状态=%d，底层码=%d。\n",
                      static_cast<unsigned>(completed),
                      static_cast<unsigned>(g_identity_notify_expected),
                      static_cast<int>(status), code);
        if (completed >= g_identity_notify_expected)
            g_identity_notify_active = false;
    }
};

// 【函数说明】BLE 监护任务占位：保持服务任务常驻，后续可放连接状态监控。
void bleDaemonTask(void *pvParameters)
{
    Serial.print("[Core 0] BLE 守护进程已启动, 运行在核心: ");
    Serial.println(xPortGetCoreID());

    NimBLEDevice::init(PrescriptConst::BLE_DEVICE_NAME);

    // 声明较大的本机首选 MTU；即使手机未协商成功，长响应仍有应用层分片兜底。
    NimBLEDevice::setMTU(517);
    NimBLEServer *pServer = NimBLEDevice::createServer();
    g_ble_server = pServer;
    pServer->setCallbacks(new TerminalBLEServerCallbacks());
    NimBLEService *pService = pServer->createService(PrescriptConst::BLE_SERVICE_UUID);

    // 【关键 2】：赋值给全局指针，并加上 NIMBLE_PROPERTY::NOTIFY 权限！
    g_ble_char = pService->createCharacteristic(
        PrescriptConst::BLE_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    g_ble_char->setCallbacks(new TerminalBLECallbacks());

    pService->start();
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(PrescriptConst::BLE_SERVICE_UUID);
    pAdvertising->start();

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 【函数说明】创建 NimBLE 设备 Terminal_01，注册 DEAD 服务和 BEEF 特征，并启动可写/可 Notify 的 WebBLE 服务。
void SysBLE_Init()
{
    const BaseType_t created = xTaskCreatePinnedToCore(
        bleDaemonTask, "BLE_Daemon", 4096, NULL, 1, NULL, 0);
    if (created != pdPASS)
        Serial.println("[BLE] 守护任务创建失败，BLE服务本次启动不可用。");
}

// 【关键 3】：实现实体函数，让任何文件都能 include "sys_ble.h" 后向手机发数据！
// 【函数说明】向已连接网页发送一条文本 Notify，ACK、SYNC、LANG、SPC_META 都通过这个出口返回。
void SysBLE_Notify(const char *data)
{
    if (g_ble_char == nullptr || data == nullptr)
        return;

    const size_t length = strlen(data);
    if (length <= kBleNotifyPayload)
    {
        notifyFrame(data, length, false);
        return;
    }

    // 先按 UTF-8 边界计算真实分片数，再把总数写进每个帧头；不能直接用
    // length/9，因为避开字符边界后某些帧可能少于 9 个字节。
    std::vector<std::pair<size_t, size_t>> chunks;
    for (size_t offset = 0; offset < length;)
    {
        size_t count = (length - offset) < kChunkPayload
                           ? (length - offset)
                           : kChunkPayload;
        while (count > 1 && offset + count < length &&
               isUtf8Continuation(static_cast<uint8_t>(data[offset + count])))
        {
            --count;
        }
        chunks.emplace_back(offset, count);
        offset += count;
    }

    const size_t total = chunks.size();
    if (total > 99)
    {
        // 当前协议头最多容纳两位总帧数；异常超长消息保留原始帧供诊断。
        notifyFrame(data, length, false);
        return;
    }

    const uint8_t sequence = g_chunk_sequence++ % 10;
    const size_t subscribers = g_ble_char->getSubscribedCount();
    Serial.printf("[BLE/诊断] 开始发送长文本：序号=%u，正文=%u字节，分片=%u，订阅者=%u。\n",
                  static_cast<unsigned>(sequence),
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(total),
                  static_cast<unsigned>(subscribers));
    g_identity_notify_expected = static_cast<uint16_t>(total);
    g_identity_notify_completed = 0;
    g_identity_notify_active = true;
    for (size_t index = 0; index < total; ++index)
    {
        const size_t offset = chunks[index].first;
        const size_t count = chunks[index].second;
        // "CH:<seq>:<index>:<total>:" 最长 11 字节，配合 9 字节载荷不超过 20 字节。
        String frame = "CH:" + String(sequence) + ":" + String(index) + ":" +
                       String(total) + ":";
        frame.concat(data + offset, count);
        Serial.printf("[BLE/诊断] 投递分片：序号=%u，分片=%u/%u，帧长=%u。\n",
                      static_cast<unsigned>(sequence),
                      static_cast<unsigned>(index + 1),
                      static_cast<unsigned>(total),
                      static_cast<unsigned>(frame.length()));
        notifyFrame(frame.c_str(), frame.length(), true);
    }
    Serial.printf("[BLE/诊断] 长文本已全部交给协议栈：序号=%u，共%u片。\n",
                  static_cast<unsigned>(sequence),
                  static_cast<unsigned>(total));
}
