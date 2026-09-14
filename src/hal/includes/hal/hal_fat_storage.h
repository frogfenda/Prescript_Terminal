#pragma once

#include <Arduino.h>
#include <FS.h>

/*
【模块职责】管理外挂W25N01GV上FAT卷的唯一访问权。

- ESP应用模式：把Dhara固定逻辑块注册为FatFs/VFS并挂载到/fat；失败时绝不自动格式化。
- PC磁盘模式：卸载VFS后把同一组固定逻辑块直接提供给USB MSC。
- 两种模式严格互斥，避免 Windows 与 ESP 同时写入导致 FAT 卷损坏。
- 正式固件只接受已完成卷身份提交的介质，不保留格式化或整区擦除入口。

本模块不包含 USB、CDC、按键或启动策略，也没有静态初始化副作用。
*/
namespace HAL::FatStorage
{
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

    // ESP侧挂载。失败时绝不格式化，也不会修改没有正式卷身份的介质。
    bool MountForEsp(const char *mountPoint = DEFAULT_MOUNT_POINT,
                     uint8_t maxOpenFiles = 10);
    void UnmountFromEsp();

    // PC侧原始块后端。不挂载FatFs，仅恢复Dhara映射并暴露固定2048字节逻辑块。
    bool OpenForUsb();
    void CloseForUsb();

    // USB MSC 回调使用的逻辑块读写接口。
    int32_t Read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufferSize);
    int32_t Write(uint32_t lba, uint32_t offset, const uint8_t *buffer, uint32_t bufferSize);

    Owner CurrentOwner();
    bool IsMountedForEsp();
    bool IsOpenForUsb();
    Geometry GetGeometry();

    /**
     * 返回当前FAT卷的Arduino FS视图。只有IsMountedForEsp()为true时才允许打开文件；
     * 调用方不得保存跨越UnmountFromEsp()的File对象。
     */
    fs::FS &FileSystem();

}
