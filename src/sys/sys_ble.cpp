// 文件：src/sys/sys_ble.cpp
#include "sys_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "app_manager.h" 

#define BLE_SERVICE_UUID        "0000DEAD-0000-1000-8000-00805F9B34FB"
#define BLE_CHARACTERISTIC_UUID "0000BEEF-0000-1000-8000-00805F9B34FB"

class TerminalBLECallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        
        // 【新增硬核调试】：把手机发来的真实数据原封不动打印出来，带上单引号看清有没有换行！
        Serial.print("[Core 0] BLE 收到真实数据: '");
        Serial.print(value.c_str());
        Serial.println("'");
        
        // 【核心修复】：使用 .find() 模糊匹配！无视手机乱加的 \r\n 换行符
        if (value.find("CMD:PUSH_NOW") != std::string::npos) {
            Serial.println("[Core 0] 💥 指令匹配成功！正在唤醒主核拉起警报！");
            g_cross_core_trigger_push = true; 
        } else {
            Serial.println("[Core 0] ❌ 指令不匹配。");
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