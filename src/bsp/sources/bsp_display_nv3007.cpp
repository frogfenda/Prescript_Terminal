/*
【模块职责】NV3007/NV3006A1 QSPI 长条屏驱动实现。来自当前硬件测试平台中已经验证的 QSPI/TE 时序。
*/
#include "bsp/bsp_display_nv3007.h"
#include "bsp/bsp_pins.h"

#include <initializer_list>
#include <string.h>
#include "driver/spi_master.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#ifndef NV3007_SPI_FREQUENCY
#define NV3007_SPI_FREQUENCY 40000000
#endif

#ifndef NV3007_INIT_SPI_FREQUENCY
#define NV3007_INIT_SPI_FREQUENCY 40000000
#endif

#ifndef NV3007_RAM_SPI_FREQUENCY
#define NV3007_RAM_SPI_FREQUENCY NV3007_SPI_FREQUENCY
#endif

#ifndef NV3007_TE_DELAY_US
#define NV3007_TE_DELAY_US 800
#endif

#ifndef NV3007_TE_PHASE_US
#define NV3007_TE_PHASE_US NV3007_TE_DELAY_US
#endif

namespace
{
    using namespace BSP::DisplayNv3007;

    static constexpr spi_host_device_t LCD_HOST = SPI2_HOST;
    static constexpr uint16_t LCD_TX_ROWS = 64;
    static constexpr uint8_t LCD_TX_BUFFER_COUNT = 2;
    static constexpr size_t LCD_TX_CHUNK_BYTES = (size_t)PANEL_WIDTH * LCD_TX_ROWS * 2;
    static constexpr uint32_t LCD_DEFAULT_PERIOD_US = 16667;

    static DRAM_ATTR uint8_t s_txChunk[LCD_TX_BUFFER_COUNT][LCD_TX_CHUNK_BYTES];

    static volatile uint32_t s_teIsrCount = 0;
    static volatile uint32_t s_teLastIsrUs = 0;
    static volatile uint32_t s_tePeriodUs = LCD_DEFAULT_PERIOD_US;
    static uint32_t s_teWaitCount = 0;
    static uint32_t s_teTimeoutCount = 0;
    static uint32_t s_tePhaseUs = NV3007_TE_PHASE_US;
    static bool s_teReady = false;
    static SemaphoreHandle_t s_teSemaphore = nullptr;

    class Nv3007Qspi
    {
    public:
        bool begin()
        {
            if (ready_)
                return true;

            pinMode(BSP::Pins::LCD_CS, OUTPUT);
            digitalWrite(BSP::Pins::LCD_CS, HIGH);
            pinMode(BSP::Pins::LCD_BLK, OUTPUT);
            digitalWrite(BSP::Pins::LCD_BLK, HIGH);
            pinMode(BSP::Pins::LCD_RST, OUTPUT);

            spi_bus_config_t bus = {};
            bus.data0_io_num = BSP::Pins::LCD_SDA0;
            bus.data1_io_num = BSP::Pins::LCD_SDA1;
            bus.sclk_io_num = BSP::Pins::LCD_SCL;
            bus.data2_io_num = BSP::Pins::LCD_SDA2;
            bus.data3_io_num = BSP::Pins::LCD_SDA3;
            bus.data4_io_num = -1;
            bus.data5_io_num = -1;
            bus.data6_io_num = -1;
            bus.data7_io_num = -1;
            bus.max_transfer_sz = LCD_TX_CHUNK_BYTES + 8;
            bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;

            lastErr_ = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
            if (lastErr_ != ESP_OK)
                return false;

            if (!addDevice(NV3007_INIT_SPI_FREQUENCY, &commandDevice_))
                return false;
            if (!addDevice(NV3007_RAM_SPI_FREQUENCY, &ramDevice_))
                return false;

            reset();
            ready_ = initSequence();
            if (!ready_)
                return false;

            FillScreen(0x0000);
            digitalWrite(BSP::Pins::LCD_BLK, HIGH);
            return ready_;
        }

        bool ready() const
        {
            return ready_;
        }

        esp_err_t lastError() const
        {
            return lastErr_;
        }

        bool FillScreen(uint16_t color)
        {
            return FillRect(0, 0, PANEL_WIDTH, PANEL_HEIGHT, color);
        }

        bool FillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color)
        {
            if (!ready_ || w == 0 || h == 0)
                return false;

            int32_t x0 = x;
            int32_t y0 = y;
            int32_t x1 = x0 + (int32_t)w - 1;
            int32_t y1 = y0 + (int32_t)h - 1;
            if (!clipPhysicalRect(x0, y0, x1, y1))
                return true;

            uint16_t clippedW = (uint16_t)(x1 - x0 + 1);
            uint16_t clippedH = (uint16_t)(y1 - y0 + 1);
            uint8_t hi = (uint8_t)(color >> 8);
            uint8_t lo = (uint8_t)(color & 0xFF);

            if (!setWindow((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1))
                return fail();

            select();
            uint8_t header[4] = {0x32, 0x00, 0x2C, 0x00};
            bool ok = txCommand(header, sizeof(header), 0);
            RamDmaQueue queue;
            for (uint16_t row = 0; row < clippedH && ok;)
            {
                uint16_t rows = LCD_TX_ROWS;
                if (row + rows > clippedH)
                    rows = clippedH - row;

                int bufferIndex = reserveRamBuffer(queue);
                if (bufferIndex < 0)
                {
                    ok = false;
                    break;
                }

                uint8_t *out = s_txChunk[bufferIndex];
                size_t pixels = (size_t)clippedW * rows;
                for (size_t i = 0; i < pixels; ++i)
                {
                    *out++ = hi;
                    *out++ = lo;
                }
                ok = queueRamBuffer(queue, (uint8_t)bufferIndex, pixels * 2, SPI_TRANS_MODE_QIO);
                row += rows;
                taskYIELD();
            }
            if (!flushRamQueue(queue))
                ok = false;
            deselect();
            return ok || fail();
        }

        bool PushImage(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t srcStride, bool sourceByteSwapped)
        {
            if (!ready_ || !pixels || w == 0 || h == 0)
                return false;
            if (srcStride == 0)
                srcStride = w;

            int32_t srcX = 0;
            int32_t srcY = 0;
            int32_t x0 = x;
            int32_t y0 = y;
            int32_t x1 = x0 + (int32_t)w - 1;
            int32_t y1 = y0 + (int32_t)h - 1;

            if (x0 < 0)
            {
                srcX = -x0;
                x0 = 0;
            }
            if (y0 < 0)
            {
                srcY = -y0;
                y0 = 0;
            }
            if (x1 >= PANEL_WIDTH)
                x1 = PANEL_WIDTH - 1;
            if (y1 >= PANEL_HEIGHT)
                y1 = PANEL_HEIGHT - 1;
            if (x0 > x1 || y0 > y1)
                return true;

            const uint16_t *clipped = pixels + (size_t)srcY * srcStride + srcX;
            return writePhysicalImage((uint16_t)x0,
                                      (uint16_t)y0,
                                      (uint16_t)(x1 - x0 + 1),
                                      (uint16_t)(y1 - y0 + 1),
                                      clipped,
                                      srcStride,
                                      sourceByteSwapped);
        }

        bool PushImageRotated(uint8_t rotation, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t srcStride, bool sourceByteSwapped)
        {
            if (!ready_ || !pixels || w == 0 || h == 0)
                return false;
            if (srcStride == 0)
                srcStride = w;

            rotation &= 0x03;
            if (rotation == 0)
                return PushImage(x, y, w, h, pixels, srcStride, sourceByteSwapped);

            int32_t physX0 = 0;
            int32_t physY0 = 0;
            int32_t physX1 = 0;
            int32_t physY1 = 0;

            switch (rotation)
            {
            case 1:
                physX0 = y;
                physY0 = (int32_t)PANEL_HEIGHT - 1 - ((int32_t)x + w - 1);
                physX1 = (int32_t)y + h - 1;
                physY1 = (int32_t)PANEL_HEIGHT - 1 - x;
                break;
            case 2:
                physX0 = (int32_t)PANEL_WIDTH - 1 - ((int32_t)x + w - 1);
                physY0 = (int32_t)PANEL_HEIGHT - 1 - ((int32_t)y + h - 1);
                physX1 = (int32_t)PANEL_WIDTH - 1 - x;
                physY1 = (int32_t)PANEL_HEIGHT - 1 - y;
                break;
            case 3:
            default:
                physX0 = (int32_t)PANEL_WIDTH - 1 - ((int32_t)y + h - 1);
                physY0 = x;
                physX1 = (int32_t)PANEL_WIDTH - 1 - y;
                physY1 = (int32_t)x + w - 1;
                break;
            }

            if (!clipPhysicalRect(physX0, physY0, physX1, physY1))
                return true;

            return writeRotatedImage(rotation, x, y, w, h, pixels, srcStride, sourceByteSwapped,
                                     (uint16_t)physX0,
                                     (uint16_t)physY0,
                                     (uint16_t)(physX1 - physX0 + 1),
                                     (uint16_t)(physY1 - physY0 + 1));
        }

        void sleep()
        {
            if (ready_)
                writeCommand(0x10);
        }

        void wakeup()
        {
            if (!ready_)
                return;
            writeCommand(0x11);
            delay(120);
            writeCommand(0x29);
        }

    private:
        spi_device_handle_t commandDevice_ = nullptr;
        spi_device_handle_t ramDevice_ = nullptr;
        esp_err_t lastErr_ = ESP_OK;
        bool ready_ = false;

        struct RamDmaQueue
        {
            spi_transaction_t trans[LCD_TX_BUFFER_COUNT];
            bool inFlight[LCD_TX_BUFFER_COUNT] = {false, false};
            uint8_t queued = 0;
        };

        static bool clipPhysicalRect(int32_t &x0, int32_t &y0, int32_t &x1, int32_t &y1)
        {
            if (x1 < 0 || y1 < 0 || x0 >= PANEL_WIDTH || y0 >= PANEL_HEIGHT)
                return false;
            if (x0 < 0)
                x0 = 0;
            if (y0 < 0)
                y0 = 0;
            if (x1 >= PANEL_WIDTH)
                x1 = PANEL_WIDTH - 1;
            if (y1 >= PANEL_HEIGHT)
                y1 = PANEL_HEIGHT - 1;
            return x0 <= x1 && y0 <= y1;
        }

        static uint16_t normalizeSourceColor(uint16_t color, bool sourceByteSwapped)
        {
            if (sourceByteSwapped)
                return (uint16_t)((color >> 8) | (color << 8));
            return color;
        }

        bool waitQueuedRam(RamDmaQueue &queue)
        {
            if (queue.queued == 0)
                return true;

            spi_transaction_t *ret = nullptr;
            lastErr_ = spi_device_get_trans_result(ramDevice_, &ret, portMAX_DELAY);
            if (lastErr_ != ESP_OK)
                return false;

            for (uint8_t i = 0; i < LCD_TX_BUFFER_COUNT; ++i)
            {
                if (ret == &queue.trans[i])
                {
                    queue.inFlight[i] = false;
                    --queue.queued;
                    return true;
                }
            }

            lastErr_ = ESP_FAIL;
            return false;
        }

        int reserveRamBuffer(RamDmaQueue &queue)
        {
            for (;;)
            {
                for (uint8_t i = 0; i < LCD_TX_BUFFER_COUNT; ++i)
                {
                    if (!queue.inFlight[i])
                        return i;
                }

                if (!waitQueuedRam(queue))
                    return -1;
            }
        }

        bool queueRamBuffer(RamDmaQueue &queue, uint8_t bufferIndex, size_t len, uint32_t flags)
        {
            if (!ramDevice_ || len == 0)
                return true;

            spi_transaction_t &trans = queue.trans[bufferIndex];
            memset(&trans, 0, sizeof(trans));
            trans.flags = flags;
            trans.length = len * 8;
            trans.tx_buffer = s_txChunk[bufferIndex];

            lastErr_ = spi_device_queue_trans(ramDevice_, &trans, portMAX_DELAY);
            if (lastErr_ != ESP_OK)
                return false;

            queue.inFlight[bufferIndex] = true;
            ++queue.queued;
            return true;
        }

        bool flushRamQueue(RamDmaQueue &queue)
        {
            while (queue.queued > 0)
            {
                if (!waitQueuedRam(queue))
                    return false;
            }
            return true;
        }

        bool writePhysicalImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t srcStride, bool sourceByteSwapped)
        {
            if (!WaitTearEffectPhase())
                return false;

            if (!setWindow(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1)))
                return fail();

            select();
            uint8_t header[4] = {0x32, 0x00, 0x2C, 0x00};
            bool ok = txCommand(header, sizeof(header), 0);
            RamDmaQueue queue;
            for (uint16_t row = 0; row < h && ok;)
            {
                uint16_t rows = LCD_TX_ROWS;
                if (row + rows > h)
                    rows = h - row;

                int bufferIndex = reserveRamBuffer(queue);
                if (bufferIndex < 0)
                {
                    ok = false;
                    break;
                }

                for (uint16_t localY = 0; localY < rows; ++localY)
                {
                    const uint16_t *src = pixels + (size_t)(row + localY) * srcStride;
                    uint8_t *out = s_txChunk[bufferIndex] + (size_t)localY * w * 2;
                    for (uint16_t col = 0; col < w; ++col)
                    {
                        uint16_t color = src[col];
                        if (sourceByteSwapped)
                        {
                            *out++ = (uint8_t)(color & 0xFF);
                            *out++ = (uint8_t)(color >> 8);
                        }
                        else
                        {
                            *out++ = (uint8_t)(color >> 8);
                            *out++ = (uint8_t)(color & 0xFF);
                        }
                    }
                }

                ok = queueRamBuffer(queue, (uint8_t)bufferIndex, (size_t)rows * w * 2, SPI_TRANS_MODE_QIO);
                row += rows;
                taskYIELD();
            }
            if (!flushRamQueue(queue))
                ok = false;
            deselect();
            return ok || fail();
        }

        bool writeRotatedImage(uint8_t rotation,
                               int16_t logicalX,
                               int16_t logicalY,
                               uint16_t srcW,
                               uint16_t srcH,
                               const uint16_t *pixels,
                               uint16_t srcStride,
                               bool sourceByteSwapped,
                               uint16_t physX,
                               uint16_t physY,
                               uint16_t physW,
                               uint16_t physH)
        {
            if (rotation == 1)
                return writeRotatedImage90(logicalX, logicalY, srcW, srcH, pixels, srcStride, sourceByteSwapped,
                                           physX, physY, physW, physH);
            if (rotation == 3)
                return writeRotatedImage270(logicalX, logicalY, srcW, srcH, pixels, srcStride, sourceByteSwapped,
                                            physX, physY, physW, physH);

            if (!WaitTearEffectPhase())
                return false;

            if (!setWindow(physX, physY, (uint16_t)(physX + physW - 1), (uint16_t)(physY + physH - 1)))
                return fail();

            select();
            uint8_t header[4] = {0x32, 0x00, 0x2C, 0x00};
            bool ok = txCommand(header, sizeof(header), 0);
            RamDmaQueue queue;
            for (uint16_t row = 0; row < physH && ok;)
            {
                uint16_t rows = LCD_TX_ROWS;
                if (row + rows > physH)
                    rows = physH - row;

                int bufferIndex = reserveRamBuffer(queue);
                if (bufferIndex < 0)
                {
                    ok = false;
                    break;
                }

                for (uint16_t localY = 0; localY < rows; ++localY)
                {
                    int32_t py = (int32_t)physY + row + localY;
                    uint8_t *out = s_txChunk[bufferIndex] + (size_t)localY * physW * 2;
                    for (uint16_t col = 0; col < physW; ++col)
                    {
                        int32_t px = (int32_t)physX + col;
                        int32_t sx = -1;
                        int32_t sy = -1;

                        switch (rotation)
                        {
                        case 1:
                            sx = (int32_t)PANEL_HEIGHT - 1 - py - logicalX;
                            sy = px - logicalY;
                            break;
                        case 2:
                            sx = (int32_t)PANEL_WIDTH - 1 - px - logicalX;
                            sy = (int32_t)PANEL_HEIGHT - 1 - py - logicalY;
                            break;
                        case 3:
                        default:
                            sx = py - logicalX;
                            sy = (int32_t)PANEL_WIDTH - 1 - px - logicalY;
                            break;
                        }

                        uint16_t color = 0x0000;
                        if (sx >= 0 && sy >= 0 && sx < srcW && sy < srcH)
                            color = pixels[(size_t)sy * srcStride + sx];
                        if (sourceByteSwapped)
                        {
                            *out++ = (uint8_t)(color & 0xFF);
                            *out++ = (uint8_t)(color >> 8);
                        }
                        else
                        {
                            *out++ = (uint8_t)(color >> 8);
                            *out++ = (uint8_t)(color & 0xFF);
                        }
                    }
                }

                ok = queueRamBuffer(queue, (uint8_t)bufferIndex, (size_t)rows * physW * 2, SPI_TRANS_MODE_QIO);
                row += rows;
                taskYIELD();
            }
            if (!flushRamQueue(queue))
                ok = false;
            deselect();
            return ok || fail();
        }

        bool writeRotatedImage270(int16_t logicalX,
                                  int16_t logicalY,
                                  uint16_t srcW,
                                  uint16_t srcH,
                                  const uint16_t *pixels,
                                  uint16_t srcStride,
                                  bool sourceByteSwapped,
                                  uint16_t physX,
                                  uint16_t physY,
                                  uint16_t physW,
                                  uint16_t physH)
        {
            if (!WaitTearEffectPhase())
                return false;

            if (!setWindow(physX, physY, (uint16_t)(physX + physW - 1), (uint16_t)(physY + physH - 1)))
                return fail();

            select();
            uint8_t header[4] = {0x32, 0x00, 0x2C, 0x00};
            bool ok = txCommand(header, sizeof(header), 0);
            RamDmaQueue queue;
            for (uint16_t row = 0; row < physH && ok;)
            {
                uint16_t rows = LCD_TX_ROWS;
                if (row + rows > physH)
                    rows = physH - row;

                int bufferIndex = reserveRamBuffer(queue);
                if (bufferIndex < 0)
                {
                    ok = false;
                    break;
                }

                for (uint16_t localY = 0; localY < rows; ++localY)
                {
                    int32_t py = (int32_t)physY + row + localY;
                    int32_t sx = py - logicalX;
                    int32_t sy = (int32_t)PANEL_WIDTH - 1 - physX - logicalY;
                    uint8_t *out = s_txChunk[bufferIndex] + (size_t)localY * physW * 2;

                    if (sx < 0 || sx >= srcW)
                    {
                        memset(out, 0, (size_t)physW * 2);
                        continue;
                    }

                    for (uint16_t col = 0; col < physW; ++col, --sy)
                    {
                        uint16_t color = 0x0000;
                        if (sy >= 0 && sy < srcH)
                            color = pixels[(size_t)sy * srcStride + sx];

                        if (sourceByteSwapped)
                        {
                            *out++ = (uint8_t)(color & 0xFF);
                            *out++ = (uint8_t)(color >> 8);
                        }
                        else
                        {
                            *out++ = (uint8_t)(color >> 8);
                            *out++ = (uint8_t)(color & 0xFF);
                        }
                    }
                }

                ok = queueRamBuffer(queue, (uint8_t)bufferIndex, (size_t)rows * physW * 2, SPI_TRANS_MODE_QIO);
                row += rows;
                taskYIELD();
            }
            if (!flushRamQueue(queue))
                ok = false;
            deselect();
            return ok || fail();
        }

        bool writeRotatedImage90(int16_t logicalX,
                                 int16_t logicalY,
                                 uint16_t srcW,
                                 uint16_t srcH,
                                 const uint16_t *pixels,
                                 uint16_t srcStride,
                                 bool sourceByteSwapped,
                                 uint16_t physX,
                                 uint16_t physY,
                                 uint16_t physW,
                                 uint16_t physH)
        {
            if (!WaitTearEffectPhase())
                return false;

            if (!setWindow(physX, physY, (uint16_t)(physX + physW - 1), (uint16_t)(physY + physH - 1)))
                return fail();

            select();
            uint8_t header[4] = {0x32, 0x00, 0x2C, 0x00};
            bool ok = txCommand(header, sizeof(header), 0);
            RamDmaQueue queue;
            for (uint16_t row = 0; row < physH && ok;)
            {
                uint16_t rows = LCD_TX_ROWS;
                if (row + rows > physH)
                    rows = physH - row;

                int bufferIndex = reserveRamBuffer(queue);
                if (bufferIndex < 0)
                {
                    ok = false;
                    break;
                }

                for (uint16_t localY = 0; localY < rows; ++localY)
                {
                    int32_t py = (int32_t)physY + row + localY;
                    int32_t sx = (int32_t)PANEL_HEIGHT - 1 - py - logicalX;
                    int32_t sy = (int32_t)physX - logicalY;
                    uint8_t *out = s_txChunk[bufferIndex] + (size_t)localY * physW * 2;

                    if (sx < 0 || sx >= srcW)
                    {
                        memset(out, 0, (size_t)physW * 2);
                        continue;
                    }

                    int32_t srcStartY = sy;
                    if (srcStartY < 0 || srcStartY >= srcH)
                        srcStartY = 0;
                    const uint16_t *src = pixels + (size_t)srcStartY * srcStride + sx;
                    for (uint16_t col = 0; col < physW; ++col, ++sy)
                    {
                        uint16_t color = 0x0000;
                        if (sy >= 0 && sy < srcH)
                        {
                            color = *src;
                            src += srcStride;
                        }

                        if (sourceByteSwapped)
                        {
                            *out++ = (uint8_t)(color & 0xFF);
                            *out++ = (uint8_t)(color >> 8);
                        }
                        else
                        {
                            *out++ = (uint8_t)(color >> 8);
                            *out++ = (uint8_t)(color & 0xFF);
                        }
                    }
                }

                ok = queueRamBuffer(queue, (uint8_t)bufferIndex, (size_t)rows * physW * 2, SPI_TRANS_MODE_QIO);
                row += rows;
                taskYIELD();
            }
            if (!flushRamQueue(queue))
                ok = false;
            deselect();
            return ok || fail();
        }

        void reset()
        {
            digitalWrite(BSP::Pins::LCD_RST, LOW);
            delay(30);
            digitalWrite(BSP::Pins::LCD_RST, HIGH);
            delay(160);
        }

        void select()
        {
            digitalWrite(BSP::Pins::LCD_CS, LOW);
        }

        void deselect()
        {
            digitalWrite(BSP::Pins::LCD_CS, HIGH);
        }

        bool addDevice(uint32_t clockHz, spi_device_handle_t *out)
        {
            spi_device_interface_config_t dev = {};
            dev.clock_speed_hz = clockHz;
            dev.mode = 0;
            dev.spics_io_num = -1;
            dev.queue_size = LCD_TX_BUFFER_COUNT;
            dev.flags = SPI_DEVICE_HALFDUPLEX;

            lastErr_ = spi_bus_add_device(LCD_HOST, &dev, out);
            return lastErr_ == ESP_OK;
        }

        bool txCommand(const void *data, size_t len, uint32_t flags)
        {
            return tx(commandDevice_, data, len, flags);
        }

        bool txRam(const void *data, size_t len, uint32_t flags)
        {
            return tx(ramDevice_, data, len, flags);
        }

        bool tx(spi_device_handle_t device, const void *data, size_t len, uint32_t flags)
        {
            if (!device || len == 0)
                return true;

            spi_transaction_t trans = {};
            trans.flags = flags;
            trans.length = len * 8;
            if (len <= sizeof(trans.tx_data))
            {
                trans.flags |= SPI_TRANS_USE_TXDATA;
                memcpy(trans.tx_data, data, len);
            }
            else
            {
                trans.tx_buffer = data;
            }

            lastErr_ = spi_device_polling_transmit(device, &trans);
            return lastErr_ == ESP_OK;
        }

        bool writeCommand(uint8_t cmd)
        {
            return writeCommand(cmd, nullptr, 0);
        }

        bool writeCommand(uint8_t cmd, std::initializer_list<uint8_t> data)
        {
            return writeCommand(cmd, data.begin(), data.size());
        }

        bool writeCommand(uint8_t cmd, const uint8_t *data, size_t len)
        {
            uint8_t header[4] = {0x02, 0x00, cmd, 0x00};
            select();
            bool ok = txCommand(header, sizeof(header), 0);
            if (ok && len > 0)
                ok = txCommand(data, len, 0);
            deselect();
            if (!ok)
                fail();
            return ok;
        }

        bool setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
        {
            x0 += PANEL_X_GAP;
            x1 += PANEL_X_GAP;
            y0 += PANEL_Y_GAP;
            y1 += PANEL_Y_GAP;
            uint8_t col[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
            uint8_t row[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
            return writeCommand(0x2A, col, sizeof(col)) && writeCommand(0x2B, row, sizeof(row));
        }

        bool initSequence()
        {
            bool ok = true;
            auto cmd = [&](uint8_t c) { ok = writeCommand(c) && ok; };
            auto reg = [&](uint8_t c, std::initializer_list<uint8_t> d) { ok = writeCommand(c, d) && ok; };

            cmd(0x11);
            delay(100);
            reg(0x36, {0x00});
            reg(0x3A, {0x05});

            reg(0xFF, {0xA5});
            reg(0x9A, {0x08});
            reg(0x9B, {0x08});
            reg(0x9C, {0xB0});
            reg(0x9D, {0x16});
            reg(0x9E, {0xC4});
            reg(0x8F, {0x55, 0x04});
            reg(0x84, {0x90});
            reg(0x83, {0x7B});
            reg(0x85, {0x33});
            reg(0x60, {0x00});
            reg(0x70, {0x00});
            reg(0x61, {0x02});
            reg(0x71, {0x02});
            reg(0x62, {0x04});
            reg(0x72, {0x04});
            reg(0x6C, {0x29});
            reg(0x7C, {0x29});
            reg(0x6D, {0x31});
            reg(0x7D, {0x31});
            reg(0x6E, {0x0F});
            reg(0x7E, {0x0F});
            reg(0x66, {0x21});
            reg(0x76, {0x21});
            reg(0x68, {0x3A});
            reg(0x78, {0x3A});
            reg(0x63, {0x07});
            reg(0x73, {0x07});
            reg(0x64, {0x05});
            reg(0x74, {0x05});
            reg(0x65, {0x02});
            reg(0x75, {0x02});
            reg(0x67, {0x23});
            reg(0x77, {0x23});
            reg(0x69, {0x08});
            reg(0x79, {0x08});
            reg(0x6A, {0x13});
            reg(0x7A, {0x13});
            reg(0x6B, {0x13});
            reg(0x7B, {0x13});
            reg(0x6F, {0x00});
            reg(0x7F, {0x00});
            reg(0x50, {0x00});
            reg(0x52, {0xD6});
            reg(0x53, {0x08});
            reg(0x54, {0x08});
            reg(0x55, {0x1E});
            reg(0x56, {0x1C});
            reg(0xA0, {0x2B, 0x24, 0x00});
            reg(0xA1, {0x87});
            reg(0xA2, {0x86});
            reg(0xA5, {0x00});
            reg(0xA6, {0x00});
            reg(0xA7, {0x00});
            reg(0xA8, {0x36});
            reg(0xA9, {0x7E});
            reg(0xAA, {0x7E});
            reg(0xB9, {0x85});
            reg(0xBA, {0x84});
            reg(0xBB, {0x83});
            reg(0xBC, {0x82});
            reg(0xBD, {0x81});
            reg(0xBE, {0x80});
            reg(0xBF, {0x01});
            reg(0xC0, {0x02});
            reg(0xC1, {0x00});
            reg(0xC2, {0x00});
            reg(0xC3, {0x00});
            reg(0xC4, {0x33});
            reg(0xC5, {0x7E});
            reg(0xC6, {0x7E});
            reg(0xC8, {0x33, 0x33});
            reg(0xC9, {0x68});
            reg(0xCA, {0x69});
            reg(0xCB, {0x6A});
            reg(0xCC, {0x6B});
            reg(0xCD, {0x33, 0x33});
            reg(0xCE, {0x6C});
            reg(0xCF, {0x6D});
            reg(0xD0, {0x6E});
            reg(0xD1, {0x6F});
            reg(0xAB, {0x03, 0x67});
            reg(0xAC, {0x03, 0x6B});
            reg(0xAD, {0x03, 0x68});
            reg(0xAE, {0x03, 0x6C});
            reg(0xB3, {0x00});
            reg(0xB4, {0x00});
            reg(0xB5, {0x00});
            reg(0xB6, {0x32});
            reg(0xB7, {0x7E});
            reg(0xB8, {0x7E});
            reg(0xE0, {0x00});
            reg(0xE1, {0x03, 0x0F});
            reg(0xE2, {0x04});
            reg(0xE3, {0x01});
            reg(0xE4, {0x0E});
            reg(0xE5, {0x01});
            reg(0xE6, {0x19});
            reg(0xE7, {0x10});
            reg(0xE8, {0x10});
            reg(0xEA, {0x12});
            reg(0xEB, {0xD0});
            reg(0xEC, {0x04});
            reg(0xED, {0x07});
            reg(0xEE, {0x07});
            reg(0xEF, {0x09});
            reg(0xF0, {0xD0});
            reg(0xF1, {0x0E, 0x17});
            reg(0xF2, {0x2C, 0x1B, 0x0B, 0x20});
            reg(0xE9, {0x29});
            reg(0xEC, {0x04});
            reg(0x35, {0x00});
            reg(0x44, {0x00, 0x10});
            reg(0x46, {0x10});
            reg(0xFF, {0x00});

            cmd(0x11);
            delay(220);
            cmd(0x29);
            delay(200);
            return ok;
        }

        bool fail()
        {
            ready_ = false;
            return false;
        }
    };

    static Nv3007Qspi s_lcd;

    static void IRAM_ATTR onTeRise()
    {
        uint32_t nowUs = micros();
        uint32_t lastUs = s_teLastIsrUs;
        if (lastUs != 0)
        {
            uint32_t periodUs = nowUs - lastUs;
            if (periodUs >= 8000 && periodUs <= 30000)
                s_tePeriodUs = (s_tePeriodUs * 7U + periodUs) / 8U;
        }
        s_teLastIsrUs = nowUs;
        ++s_teIsrCount;

        BaseType_t higherPriorityTaskWoken = pdFALSE;
        if (s_teSemaphore)
            xSemaphoreGiveFromISR(s_teSemaphore, &higherPriorityTaskWoken);
        if (higherPriorityTaskWoken == pdTRUE)
            portYIELD_FROM_ISR();
    }

    static void initTeSync()
    {
        if (s_teReady)
            return;

        pinMode(BSP::Pins::LCD_TE, INPUT);
        s_teSemaphore = xSemaphoreCreateBinary();
        if (!s_teSemaphore)
        {
            Serial.printf("[BSP][显示] TE GPIO%d 信号量创建失败。\n", BSP::Pins::LCD_TE);
            return;
        }

        attachInterrupt(digitalPinToInterrupt(BSP::Pins::LCD_TE), onTeRise, RISING);
        s_teReady = true;
        Serial.printf("[BSP][显示] TE GPIO%d 已启用，默认相位 %luus。\n",
                      BSP::Pins::LCD_TE,
                      (unsigned long)s_tePhaseUs);
    }

    static uint32_t tePhaseWaitUs()
    {
        uint32_t lastUs = s_teLastIsrUs;
        if (lastUs == 0)
            return 0;

        uint32_t periodUs = s_tePeriodUs;
        if (periodUs < 8000 || periodUs > 30000)
            periodUs = LCD_DEFAULT_PERIOD_US;

        uint32_t targetUs = s_tePhaseUs % periodUs;
        uint32_t phaseUs = (micros() - lastUs) % periodUs;
        if (phaseUs <= targetUs)
            return targetUs - phaseUs;
        return periodUs - phaseUs + targetUs;
    }
}

namespace BSP::DisplayNv3007
{
    bool Begin()
    {
        bool ok = s_lcd.begin();
        if (ok)
            initTeSync();

        Serial.printf("[BSP][显示] NV3007 QSPI 初始化%s：CS=%d SCL=%d SDA0=%d SDA1=%d SDA2=%d SDA3=%d RST=%d BLK=%d TE=%d init=%luHz ram=%luHz。\n",
                      ok ? "完成" : "失败",
                      BSP::Pins::LCD_CS,
                      BSP::Pins::LCD_SCL,
                      BSP::Pins::LCD_SDA0,
                      BSP::Pins::LCD_SDA1,
                      BSP::Pins::LCD_SDA2,
                      BSP::Pins::LCD_SDA3,
                      BSP::Pins::LCD_RST,
                      BSP::Pins::LCD_BLK,
                      BSP::Pins::LCD_TE,
                      (unsigned long)NV3007_INIT_SPI_FREQUENCY,
                      (unsigned long)NV3007_RAM_SPI_FREQUENCY);
        return ok;
    }

    bool IsReady()
    {
        return s_lcd.ready();
    }

    esp_err_t LastError()
    {
        return s_lcd.lastError();
    }

    void SetBacklight(bool on)
    {
        pinMode(BSP::Pins::LCD_BLK, OUTPUT);
        digitalWrite(BSP::Pins::LCD_BLK, on ? HIGH : LOW);
    }

    bool FillScreen(uint16_t color)
    {
        return s_lcd.FillScreen(color);
    }

    bool FillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color)
    {
        return s_lcd.FillRect(x, y, w, h, color);
    }

    bool PushImage(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t srcStride, bool sourceByteSwapped)
    {
        return s_lcd.PushImage(x, y, w, h, pixels, srcStride, sourceByteSwapped);
    }

    bool PushImageRotated(uint8_t rotation,
                          int16_t x,
                          int16_t y,
                          uint16_t w,
                          uint16_t h,
                          const uint16_t *pixels,
                          uint16_t srcStride,
                          bool sourceByteSwapped)
    {
        return s_lcd.PushImageRotated(rotation, x, y, w, h, pixels, srcStride, sourceByteSwapped);
    }

    bool WaitTearEffectPhase(uint32_t timeoutMs)
    {
        if (!s_teReady || !s_teSemaphore)
            return true;

        if (s_teLastIsrUs == 0)
        {
            if (xSemaphoreTake(s_teSemaphore, pdMS_TO_TICKS(timeoutMs)) != pdTRUE)
            {
                ++s_teTimeoutCount;
                return false;
            }
        }

        uint32_t startMs = millis();
        for (;;)
        {
            uint32_t waitUs = tePhaseWaitUs();
            if (waitUs <= 120)
                break;

            if (millis() - startMs > timeoutMs)
            {
                ++s_teTimeoutCount;
                return false;
            }

            if (waitUs > 2000)
                vTaskDelay(pdMS_TO_TICKS((waitUs - 800) / 1000));
            else
                delayMicroseconds(waitUs - 100);
        }

        ++s_teWaitCount;
        return true;
    }

    void SetTearEffectPhaseUs(uint32_t phaseUs)
    {
        s_tePhaseUs = phaseUs;
    }

    Diagnostics GetDiagnostics()
    {
        uint32_t teLastUs = s_teLastIsrUs;
        Diagnostics diag = {};
        diag.ready = s_lcd.ready();
        diag.lastError = s_lcd.lastError();
        diag.teReady = s_teReady;
        diag.tePhaseUs = s_tePhaseUs;
        diag.tePeriodUs = s_tePeriodUs;
        diag.teWaitNextUs = tePhaseWaitUs();
        diag.teIrqAgeUs = teLastUs ? (uint32_t)(micros() - teLastUs) : 0;
        diag.teIrqCount = s_teIsrCount;
        diag.teWaitCount = s_teWaitCount;
        diag.teTimeoutCount = s_teTimeoutCount;
        return diag;
    }

    void PrintDiagnostics(Stream &out)
    {
        Diagnostics d = GetDiagnostics();
        uint32_t panelHz10 = d.tePeriodUs ? (10000000UL / d.tePeriodUs) : 0;
        out.printf("[BSP][显示] ready=%s err=%d panel=%ux%u init=%luHz ram=%luHz BLK%d=%d TE%d=%d te_ready=%s te_phase=%luus te_period=%luus panel=%lu.%luHz te_wait_next=%luus te_irq_age=%luus te_irq=%lu te_wait=%lu te_timeout=%lu\n",
                   d.ready ? "yes" : "no",
                   (int)d.lastError,
                   PANEL_WIDTH,
                   PANEL_HEIGHT,
                   (unsigned long)NV3007_INIT_SPI_FREQUENCY,
                   (unsigned long)NV3007_RAM_SPI_FREQUENCY,
                   BSP::Pins::LCD_BLK,
                   digitalRead(BSP::Pins::LCD_BLK),
                   BSP::Pins::LCD_TE,
                   digitalRead(BSP::Pins::LCD_TE),
                   d.teReady ? "yes" : "no",
                   (unsigned long)d.tePhaseUs,
                   (unsigned long)d.tePeriodUs,
                   (unsigned long)(panelHz10 / 10),
                   (unsigned long)(panelHz10 % 10),
                   (unsigned long)d.teWaitNextUs,
                   (unsigned long)d.teIrqAgeUs,
                   (unsigned long)d.teIrqCount,
                   (unsigned long)d.teWaitCount,
                   (unsigned long)d.teTimeoutCount);
    }

    void Sleep()
    {
        s_lcd.sleep();
    }

    void Wakeup()
    {
        s_lcd.wakeup();
    }
}
