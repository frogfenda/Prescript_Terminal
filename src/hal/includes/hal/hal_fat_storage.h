#pragma once

#include <Arduino.h>

/*
【模块职责】管理分区表中 fatfs 分区的唯一访问权。

- ESP 应用模式：通过 FFat 挂载文件系统；formatOnFail 永远为 false，格式化只交给 Windows。
- PC 磁盘模式：通过 wear-levelling 暴露原始逻辑块读写，供 USB MSC 驱动调用。
- 两种模式严格互斥，避免 Windows 与 ESP 同时写入导致 FAT 卷损坏。

本模块不包含 USB、CDC、按键或启动策略，也没有静态初始化副作用。
*/
namespace HAL::FatStorage
{
    constexpr const char *DEFAULT_PARTITION_LABEL = "fatfs";
    constexpr const char *DEFAULT_MOUNT_POINT = "/fat";

    enum class Owner : uint8_t
    {
        None,
        EspFileSystem,
        UsbMassStorage,
    };

    struct Geometry
    {
        size_t usableBytes = 0;
        uint32_t blockCount = 0;
        uint16_t blockSize = 0;
    };

    // ESP 侧挂载。失败时绝不格式化；空白卷必须先由 Windows 创建 FAT 文件系统。
    bool MountForEsp(const char *mountPoint = DEFAULT_MOUNT_POINT,
                     uint8_t maxOpenFiles = 10,
                     const char *partitionLabel = DEFAULT_PARTITION_LABEL);
    void UnmountFromEsp();

    // PC 侧原始块后端。仅挂载 wear-levelling，不解析或格式化 FAT 文件系统。
    bool OpenForUsb(const char *partitionLabel = DEFAULT_PARTITION_LABEL);
    void CloseForUsb();

    // USB MSC 回调使用的逻辑块读写接口。
    int32_t Read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufferSize);
    int32_t Write(uint32_t lba, uint32_t offset, const uint8_t *buffer, uint32_t bufferSize);

    Owner CurrentOwner();
    bool IsMountedForEsp();
    bool IsOpenForUsb();
    Geometry GetGeometry();
}
