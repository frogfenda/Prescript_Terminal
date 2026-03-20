// 文件：src/sys/sys_ble.cpp
#include "sys_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "app_manager.h"
#include "sys_router.h"
#include <queue>
#include <mutex>
extern std::queue<String> g_ble_msg_queue;
extern std::mutex g_ble_mutex;
#define BLE_SERVICE_UUID "0000DEAD-0000-1000-8000-00805F9B34FB"
#define BLE_CHARACTERISTIC_UUID "0000BEEF-0000-1000-8000-00805F9B34FB"

// 【关键 1】：全局特征值指针，保存下来以便随时调用 notify()
NimBLECharacteristic *g_ble_char = nullptr;

class TerminalBLECallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pCharacteristic)
    {
        std::string value = pCharacteristic->getValue();

        Serial.print("[Core 0] BLE Rx: '");
        Serial.print(value.c_str());
        Serial.println("'");

        if (value.find("CMD:PUSH_NOW") != std::string::npos)
        {
            g_cross_core_trigger_push = true;
        }
        else
        {
            // 【核心修复】：不再直接调用 SysRouter_ProcessBLE！
            // 把接收到的数据安全地塞进邮筒，让主线程慢慢去处理。
            std::lock_guard<std::mutex> lock(g_ble_mutex);
            g_ble_msg_queue.push(String(value.c_str()));
        }
    }
};

void bleDaemonTask(void *pvParameters)
{
    Serial.print("[Core 0] BLE 守护进程已启动, 运行在核心: ");
    Serial.println(xPortGetCoreID());

    NimBLEDevice::init("Terminal_01");

    NimBLEServer *pServer = NimBLEDevice::createServer();
    NimBLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    // 【关键 2】：赋值给全局指针，并加上 NIMBLE_PROPERTY::NOTIFY 权限！
    g_ble_char = pService->createCharacteristic(
        BLE_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    g_ble_char->setCallbacks(new TerminalBLECallbacks());

    pService->start();
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->start();

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void SysBLE_Init()
{
    xTaskCreatePinnedToCore(bleDaemonTask, "BLE_Daemon", 4096, NULL, 1, NULL, 0);
}

// 【关键 3】：实现实体函数，让任何文件都能 include "sys_ble.h" 后向手机发数据！
void SysBLE_Notify(const char *data)
{
    if (g_ble_char != nullptr)
    {
        g_ble_char->setValue((uint8_t *)data, strlen(data));
        g_ble_char->notify();
    }
}