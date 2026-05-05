// 文件：src/sys/sys_ble.cpp
#include "sys_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "sys_constants.h"
#include "sys_ble_queue.h"
#include "sys_runtime_status.h"

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
            SysRuntime_RequestPushNotify();
        }
        else
        {
            // 不在 BLE 回调里直接路由，避免跨核心写文件/切 UI。
            SysBleQueue_Push(String(value.c_str()));
        }
    }
};

void bleDaemonTask(void *pvParameters)
{
    Serial.print("[Core 0] BLE 守护进程已启动, 运行在核心: ");
    Serial.println(xPortGetCoreID());

    NimBLEDevice::init(PrescriptConst::BLE_DEVICE_NAME);

    NimBLEServer *pServer = NimBLEDevice::createServer();
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