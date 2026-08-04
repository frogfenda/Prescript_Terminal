/*
【模块职责】为长期常驻的可编辑文本提供显式PSRAM所有权，避免Arduino String和std::vector的小额malloc持续挤占内部DRAM。
【适用范围】普通指令、身份目录、纺织机、特殊指令和叙事目录等主循环只读/低频修改的数据。
【重要约束】中断、DMA、任务栈和Flash缓存关闭期间仍需访问的数据不得使用本容器；分配失败时回退内部RAM以保留基本功能。
*/
#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <vector>

namespace SysPsramTextDetail
{
    /** 优先从PSRAM申请8位可访问内存；PSRAM异常时回退内部RAM，避免文本资源失败拖垮整个启动流程。 */
    inline void *Allocate(size_t bytes)
    {
        if (bytes == 0)
            return nullptr;
        return heap_caps_malloc_prefer(bytes, 2,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    inline void Free(void *pointer)
    {
        heap_caps_free(pointer);
    }
}

/**
 * 【类型说明】拥有一份以NUL结尾的UTF-8文本，字符缓冲优先位于PSRAM。
 * 【复制语义】复制会建立独立缓冲；移动会转移所有权，适合std::vector扩容和业务模型重载。
 * 【失败语义】赋值申请失败时保留原值并返回false；构造阶段失败则得到空字符串。
 */
class SysPsramString
{
public:
    SysPsramString() = default;

    SysPsramString(const char *text)
    {
        (void)assign(text);
    }

    SysPsramString(const String &text)
    {
        (void)assign(text.c_str(), text.length());
    }

    SysPsramString(const SysPsramString &other)
    {
        (void)assign(other.c_str(), other.length());
    }

    SysPsramString(SysPsramString &&other) noexcept
        : data_(other.data_), length_(other.length_)
    {
        other.data_ = nullptr;
        other.length_ = 0;
    }

    ~SysPsramString()
    {
        SysPsramTextDetail::Free(data_);
    }

    SysPsramString &operator=(const SysPsramString &other)
    {
        if (this != &other)
            (void)assign(other.c_str(), other.length());
        return *this;
    }

    SysPsramString &operator=(SysPsramString &&other) noexcept
    {
        if (this == &other)
            return *this;
        SysPsramTextDetail::Free(data_);
        data_ = other.data_;
        length_ = other.length_;
        other.data_ = nullptr;
        other.length_ = 0;
        return *this;
    }

    SysPsramString &operator=(const char *text)
    {
        (void)assign(text);
        return *this;
    }

    SysPsramString &operator=(const String &text)
    {
        (void)assign(text.c_str(), text.length());
        return *this;
    }

    bool assign(const char *text)
    {
        return assign(text, text ? strlen(text) : 0);
    }

    bool assign(const char *text, size_t length)
    {
        if (!text || length == 0)
        {
            clear();
            return true;
        }

        char *replacement = static_cast<char *>(SysPsramTextDetail::Allocate(length + 1U));
        if (!replacement)
            return false;

        memcpy(replacement, text, length);
        replacement[length] = '\0';
        SysPsramTextDetail::Free(data_);
        data_ = replacement;
        length_ = length;
        return true;
    }

    void clear()
    {
        SysPsramTextDetail::Free(data_);
        data_ = nullptr;
        length_ = 0;
    }

    /** 原地移除ASCII控制字符和空格，不重新分配PSRAM；语义与Arduino String::trim()保持一致。 */
    void trim()
    {
        if (!data_ || length_ == 0)
            return;
        size_t begin = 0;
        while (begin < length_ && static_cast<uint8_t>(data_[begin]) <= 0x20U)
            ++begin;
        size_t end = length_;
        while (end > begin && static_cast<uint8_t>(data_[end - 1U]) <= 0x20U)
            --end;
        const size_t trimmedLength = end - begin;
        if (begin > 0 && trimmedLength > 0)
            memmove(data_, data_ + begin, trimmedLength);
        data_[trimmedLength] = '\0';
        length_ = trimmedLength;
    }

    const char *c_str() const { return data_ ? data_ : ""; }
    size_t length() const { return length_; }
    bool isEmpty() const { return length_ == 0; }

    bool operator==(const SysPsramString &other) const
    {
        return length_ == other.length_ && memcmp(c_str(), other.c_str(), length_) == 0;
    }

    bool operator!=(const SysPsramString &other) const { return !(*this == other); }
    bool operator==(const char *other) const { return strcmp(c_str(), other ? other : "") == 0; }
    bool operator!=(const char *other) const { return !(*this == other); }
    bool operator==(const String &other) const
    {
        return length_ == other.length() && memcmp(c_str(), other.c_str(), length_) == 0;
    }
    bool operator!=(const String &other) const { return !(*this == other); }

private:
    char *data_ = nullptr;
    size_t length_ = 0;
};

/**
 * 【类型说明】让std::vector的元素数组优先位于PSRAM；元素自己的动态字段仍须使用SysPsramString等显式所有权类型。
 * 【失败策略】只有PSRAM和内部RAM都耗尽时才终止；这与当前默认std::vector在全堆耗尽时的失败边界一致。
 */
template <typename T>
class SysPsramAllocator
{
public:
    using value_type = T;

    SysPsramAllocator() noexcept = default;
    template <typename U>
    SysPsramAllocator(const SysPsramAllocator<U> &) noexcept {}

    T *allocate(size_t count)
    {
        if (count == 0)
            return nullptr;
        if (count > SIZE_MAX / sizeof(T))
            abort();
        void *storage = SysPsramTextDetail::Allocate(count * sizeof(T));
        if (!storage)
            abort();
        return static_cast<T *>(storage);
    }

    void deallocate(T *pointer, size_t) noexcept
    {
        SysPsramTextDetail::Free(pointer);
    }

    template <typename U>
    struct rebind
    {
        using other = SysPsramAllocator<U>;
    };
};

template <typename T, typename U>
inline bool operator==(const SysPsramAllocator<T> &, const SysPsramAllocator<U> &) { return true; }

template <typename T, typename U>
inline bool operator!=(const SysPsramAllocator<T> &, const SysPsramAllocator<U> &) { return false; }

template <typename T>
using SysPsramVector = std::vector<T, SysPsramAllocator<T>>;

using SysPsramTextList = SysPsramVector<SysPsramString>;
