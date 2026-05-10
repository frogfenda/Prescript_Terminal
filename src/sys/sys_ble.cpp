/*
【模块职责】BLE 通信实现。创建 Terminal_01 服务与 BEEF 特征；网页写入的数据不直接处理，而是放入 sys_ble_queue 等主循环安全消费。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
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
    // 【函数说明】BLE 特征写入回调：读取网页写入的 UTF-8 命令字符串并放入 SysBleQueue，避免在 BLE 线程中直接分发事件。
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

// 【函数说明】BLE 监护任务占位：保持服务任务常驻，后续可放连接状态监控。
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

// 【函数说明】创建 NimBLE 设备 Terminal_01，注册 DEAD 服务和 BEEF 特征，并启动可写/可 Notify 的 WebBLE 服务。
void SysBLE_Init()
{
    xTaskCreatePinnedToCore(bleDaemonTask, "BLE_Daemon", 4096, NULL, 1, NULL, 0);
}

// 【关键 3】：实现实体函数，让任何文件都能 include "sys_ble.h" 后向手机发数据！
// 【函数说明】向已连接网页发送一条文本 Notify，ACK、SYNC、LANG、SPC_META 都通过这个出口返回。
void SysBLE_Notify(const char *data)
{
    if (g_ble_char != nullptr)
    {
        g_ble_char->setValue((uint8_t *)data, strlen(data));
        g_ble_char->notify();
    }
}
