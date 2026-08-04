/*
【模块职责】把ArduinoJson解析树的动态内存优先放入PSRAM，降低加载可编辑文本资源时的内部堆峰值与碎片。
【生命周期】分配器是无状态单例；JsonDocument仍按原有局部作用域自动释放，不改变JSON节点或字符串视图寿命。
*/
#pragma once

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

class SysPsramJsonAllocator final : public ArduinoJson::Allocator
{
public:
    void *allocate(size_t size) override
    {
        return heap_caps_malloc_prefer(size, 2,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    void deallocate(void *pointer) override
    {
        heap_caps_free(pointer);
    }

    void *reallocate(void *pointer, size_t newSize) override
    {
        return heap_caps_realloc_prefer(pointer, newSize, 2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    static ArduinoJson::Allocator *Instance()
    {
        static SysPsramJsonAllocator allocator;
        return &allocator;
    }
};
