#pragma once

#ifdef __cplusplus

#include <USBCDC.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * 手动注册的 TinyUSB CDC 串口。
 *
 * Arduino 自动 USB CDC 被关闭后，CDCSerial 接替全项目原有的 Serial 输出。
 * USB 主机尚未打开串口时，发送内容暂存在固定环形缓冲区中；主循环调用
 * Service() 后会在 CDC 可以发送时补发，避免丢失开机早期日志。
 */
class BufferedUSBCDC final : public USBCDC
{
public:
    static constexpr size_t TX_BUFFER_SIZE = 16384;

    explicit BufferedUSBCDC(uint8_t interfaceNumber = 0);

    bool Configure(size_t rxBufferSize, uint32_t txTimeoutMs);
    void Service();

    size_t write(uint8_t value) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    void flush() override;

private:
    bool ensureLock();
    void bufferLocked(const uint8_t *buffer, size_t size);
    void flushBufferedLocked();

    SemaphoreHandle_t bufferLock_ = nullptr;
    uint8_t txBuffer_[TX_BUFFER_SIZE] = {};
    size_t txHead_ = 0;
    size_t txTail_ = 0;
    size_t txCount_ = 0;
    size_t droppedBytes_ = 0;
};

// 使用 -DNO_GLOBAL_SERIAL 禁用 Arduino 的 HardwareSerial Serial 后由 CDC 接管日志。
extern BufferedUSBCDC CDCSerial;

// 兼容原有调用点以及第三方头文件中的默认 Stream 参数；所有 Serial 均落到 CDCSerial。
#ifndef Serial
#define Serial CDCSerial
#endif

#endif // __cplusplus
