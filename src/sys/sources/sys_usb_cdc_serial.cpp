#include "sys/sys_usb_cdc_serial.h"

#include <algorithm>

BufferedUSBCDC CDCSerial(0);

BufferedUSBCDC::BufferedUSBCDC(uint8_t interfaceNumber)
    : USBCDC(interfaceNumber)
{
}

bool BufferedUSBCDC::ensureLock()
{
    if (bufferLock_)
        return true;

    bufferLock_ = xSemaphoreCreateMutex();
    return bufferLock_ != nullptr;
}

bool BufferedUSBCDC::Configure(size_t rxBufferSize, uint32_t txTimeoutMs)
{
    if (!ensureLock())
        return false;

    setTxTimeoutMs(txTimeoutMs);
    return setRxBufferSize(rxBufferSize) == rxBufferSize;
}

void BufferedUSBCDC::bufferLocked(const uint8_t *buffer, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        if (txCount_ == TX_BUFFER_SIZE)
        {
            txTail_ = (txTail_ + 1) % TX_BUFFER_SIZE;
            --txCount_;
            ++droppedBytes_;
        }

        txBuffer_[txHead_] = buffer[i];
        txHead_ = (txHead_ + 1) % TX_BUFFER_SIZE;
        ++txCount_;
    }
}

void BufferedUSBCDC::flushBufferedLocked()
{
    while (txCount_ > 0)
    {
        /*
         * Arduino-ESP32 2.0.14 的 USBCDC::operator bool() 只有在 DTR 和 RTS
         * 同时置位时才返回 true；不少终端只置 DTR。直接检查 TinyUSB 可写空间，
         * 既能兼容这些终端，也能避免对一个已满的 USB TX FIFO 阻塞等待。
         */
        const int available = USBCDC::availableForWrite();
        if (available <= 0)
            break;

        const size_t contiguous = std::min(txCount_, TX_BUFFER_SIZE - txTail_);
        const size_t chunk = std::min(contiguous, static_cast<size_t>(available));
        const size_t sent = USBCDC::write(txBuffer_ + txTail_, chunk);
        if (sent == 0)
            break;

        txTail_ = (txTail_ + sent) % TX_BUFFER_SIZE;
        txCount_ -= sent;
    }
}

size_t BufferedUSBCDC::write(uint8_t value)
{
    return write(&value, 1);
}

size_t BufferedUSBCDC::write(const uint8_t *buffer, size_t size)
{
    // 日志不应在 ISR 中等待互斥锁；遇到这种误用时直接拒绝本次写入。
    if (xPortInIsrContext())
        return 0;

    if (!buffer || size == 0 || !ensureLock())
        return 0;

    if (xSemaphoreTake(bufferLock_, portMAX_DELAY) != pdTRUE)
        return 0;

    flushBufferedLocked();

    // 所有日志先进入统一环形缓冲，再尽可能非阻塞地送往 CDC。
    bufferLocked(buffer, size);
    flushBufferedLocked();

    xSemaphoreGive(bufferLock_);
    return size;
}

void BufferedUSBCDC::flush()
{
    if (!ensureLock())
        return;

    if (xSemaphoreTake(bufferLock_, portMAX_DELAY) != pdTRUE)
        return;

    flushBufferedLocked();
    USBCDC::flush();

    xSemaphoreGive(bufferLock_);
}

void BufferedUSBCDC::Service()
{
    flush();
}
