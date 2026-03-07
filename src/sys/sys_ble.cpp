// 文件：src/sys/sys_ble.cpp
#include "sys_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "app_manager.h" 

#define BLE_SERVICE_UUID        "0000DEAD-0000-1000-8000-00805F9B34FB"
#define BLE_CHARACTERISTIC_UUID "0000BEEF-0000-1000-8000-00805F9B34FB"

// 文件：src/sys/sys_ble.cpp (替换 onWrite 函数)
class TerminalBLECallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        
        Serial.print("[Core 0] BLE Rx: '");
        Serial.print(value.c_str());
        Serial.println("'");
        
        if (value.find("CMD:PUSH_NOW") != std::string::npos) {
            g_cross_core_trigger_push = true; 
        } else {
            // 【跨核安全投递】：将复杂指令放入信箱，由 Core 1 在安全时机拆解
            if (value.length() < 500) {
                snprintf(g_ble_msg_buf, sizeof(g_ble_msg_buf), "%s", value.c_str());
                g_ble_has_msg = true;
            }
        }
    }
};

void bleDaemonTask(void *pvParameters) {
    Serial.print("[Core 0] BLE 守护进程已启动, 运行在核心: ");
    Serial.println(xPortGetCoreID()); 

    NimBLEDevice::init("Terminal_01"); 
    
    NimBLEServer *pServer = NimBLEDevice::createServer();
    NimBLEService *pService = pServer->createService(BLE_SERVICE_UUID);
    
    NimBLECharacteristic *pCharacteristic = pService->createCharacteristic(
        BLE_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    pCharacteristic->setCallbacks(new TerminalBLECallbacks());
    
    pService->start();
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

void SysBLE_Init() {
    xTaskCreatePinnedToCore(bleDaemonTask, "BLE_Daemon", 4096, NULL, 1, NULL, 0);
}