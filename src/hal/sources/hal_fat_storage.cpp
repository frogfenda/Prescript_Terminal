/*
【模块职责】把外挂W25N01GV的Dhara固定逻辑块统一提供给ESP侧FatFs/VFS或USB MSC。
【所有权】ESP文件系统与USB块设备严格互斥；切换前必须卸载/同步，任何路径都不能让两方并发访问。
【安全边界】正式固件只挂载已经完成身份提交的卷；失败时绝不格式化，也不提供整区擦除入口。
*/
#include "hal/hal_fat_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <Preferences.h>

#include "sys/sys_nand_ftl.h"

#include "diskio_impl.h"
#include "esp_vfs_fat.h"
#include "vfs_api.h"

namespace
{
    class NandFatFs final : public fs::FS
    {
    public:
        NandFatFs() : fs::FS(fs::FSImplPtr(new VFSImpl())) {}

        void Attach(const char *mountPoint) { _impl->mountpoint(mountPoint); }
        void Detach() { _impl->mountpoint(nullptr); }
    };

    HAL::FatStorage::Owner s_owner = HAL::FatStorage::Owner::None;
    HAL::FatStorage::Geometry s_geometry;
    NandFatFs s_file_system;
    BYTE s_drive = FF_DRV_NOT_USED;
    char s_drive_path[4] = {};
    char s_mount_path[16] = {};
    FATFS *s_fatfs = nullptr;
    bool s_vfs_registered = false;
    uint8_t *s_sector_cache = nullptr;

    constexpr const char *VOLUME_NVS_NAMESPACE = "nand_fat";
    constexpr const char *VOLUME_NVS_KEY = "volume_prod";
    constexpr uint32_t VOLUME_RECORD_MAGIC = 0x5441464EUL; // "NFAT"。
    constexpr uint16_t VOLUME_RECORD_VERSION = 1;

    struct VolumeRecord
    {
        uint32_t magic;
        uint16_t version;
        uint16_t firstBlock;
        uint16_t blockCount;
        uint16_t sectorSize;
        uint32_t sectorCount;
        uint32_t checksum;
    };

    uint32_t VolumeRecordChecksum(const VolumeRecord &record)
    {
        return record.magic ^ (static_cast<uint32_t>(record.version) << 16) ^
               record.firstBlock ^ (static_cast<uint32_t>(record.blockCount) << 16) ^
               record.sectorSize ^ record.sectorCount ^ 0xA55A39C3UL;
    }

    bool VolumeRecordIsValid()
    {
        Preferences preferences;
        if (!preferences.begin(VOLUME_NVS_NAMESPACE, true))
            return false;
        VolumeRecord record = {};
        const size_t bytes = preferences.getBytesLength(VOLUME_NVS_KEY);
        const size_t read_bytes = bytes == sizeof(record)
                                      ? preferences.getBytes(VOLUME_NVS_KEY, &record, sizeof(record))
                                      : 0;
        preferences.end();
        return read_bytes == sizeof(record) && record.magic == VOLUME_RECORD_MAGIC &&
               record.version == VOLUME_RECORD_VERSION &&
               record.firstBlock == SysNandFtl::PHYSICAL_FIRST_BLOCK &&
               record.blockCount == SysNandFtl::PHYSICAL_BLOCK_COUNT &&
               record.sectorSize == SysNandFtl::LOGICAL_SECTOR_SIZE &&
               record.sectorCount == SysNandFtl::LOGICAL_SECTOR_COUNT &&
               record.checksum == VolumeRecordChecksum(record);
    }

    bool RangeIsValid(uint64_t address, uint32_t length)
    {
        if (s_owner != HAL::FatStorage::Owner::UsbMassStorage ||
            !SysNandFtl::IsReady() || length > s_geometry.usableBytes)
            return false;
        return address <= static_cast<uint64_t>(s_geometry.usableBytes - length);
    }

    DSTATUS DiskInitialize(BYTE)
    {
        return SysNandFtl::IsReady() ? 0 : STA_NOINIT;
    }

    DSTATUS DiskStatus(BYTE)
    {
        return SysNandFtl::IsReady() ? 0 : STA_NOINIT;
    }

    DRESULT DiskRead(BYTE, BYTE *buffer, DWORD sector, UINT count)
    {
        if (!buffer || count == 0 || !SysNandFtl::IsReady() ||
            sector >= SysNandFtl::SectorCount() || count > SysNandFtl::SectorCount() - sector)
            return RES_PARERR;
        for (UINT index = 0; index < count; ++index)
        {
            if (!SysNandFtl::Read(sector + index,
                                  buffer + static_cast<size_t>(index) * SysNandFtl::LOGICAL_SECTOR_SIZE))
                return RES_ERROR;
        }
        return RES_OK;
    }

    DRESULT DiskWrite(BYTE, const BYTE *buffer, DWORD sector, UINT count)
    {
        if (!buffer || count == 0 || !SysNandFtl::IsReady() ||
            sector >= SysNandFtl::SectorCount() || count > SysNandFtl::SectorCount() - sector)
            return RES_PARERR;
        for (UINT index = 0; index < count; ++index)
        {
            if (!SysNandFtl::Write(sector + index,
                                   buffer + static_cast<size_t>(index) * SysNandFtl::LOGICAL_SECTOR_SIZE))
                return RES_ERROR;
        }
        return RES_OK;
    }

    DRESULT DiskIoctl(BYTE, BYTE command, void *buffer)
    {
        if (!SysNandFtl::IsReady())
            return RES_NOTRDY;
        switch (command)
        {
        case CTRL_SYNC:
            return SysNandFtl::Sync() ? RES_OK : RES_ERROR;
        case GET_SECTOR_COUNT:
            if (!buffer) return RES_PARERR;
            *static_cast<DWORD *>(buffer) = SysNandFtl::SectorCount();
            return RES_OK;
        case GET_SECTOR_SIZE:
            if (!buffer) return RES_PARERR;
            *static_cast<WORD *>(buffer) = SysNandFtl::LOGICAL_SECTOR_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            if (!buffer) return RES_PARERR;
            *static_cast<DWORD *>(buffer) = 64; // W25N01每个擦除块含64个逻辑页。
            return RES_OK;
        case CTRL_TRIM:
            if (!buffer) return RES_PARERR;
            {
                const DWORD *range = static_cast<const DWORD *>(buffer);
                return SysNandFtl::Trim(range[0], range[1]) ? RES_OK : RES_ERROR;
            }
        default:
            return RES_PARERR;
        }
    }

    const ff_diskio_impl_t DISK_IO = {
        DiskInitialize,
        DiskStatus,
        DiskRead,
        DiskWrite,
        DiskIoctl,
    };

    void ReleaseSectorCache()
    {
        heap_caps_free(s_sector_cache);
        s_sector_cache = nullptr;
    }

    void UnregisterDiskAndVfs(bool unmount)
    {
        if (unmount && s_fatfs)
            (void)f_mount(nullptr, s_drive_path, 0);
        s_file_system.Detach();
        if (s_vfs_registered)
            (void)esp_vfs_fat_unregister_path(s_mount_path);
        if (s_drive != FF_DRV_NOT_USED)
            ff_diskio_unregister(s_drive);
        s_drive = FF_DRV_NOT_USED;
        s_drive_path[0] = '\0';
        s_mount_path[0] = '\0';
        s_fatfs = nullptr;
        s_vfs_registered = false;
    }

    bool RegisterDiskAndVfs(const char *mountPoint, uint8_t maxOpenFiles)
    {
        if (!mountPoint || mountPoint[0] != '/' || strlen(mountPoint) >= sizeof(s_mount_path))
            return false;
        if (ff_diskio_get_drive(&s_drive) != ESP_OK || s_drive == FF_DRV_NOT_USED || s_drive > 9)
        {
            Serial.println("[FATFS] 没有可用FatFs盘符，NAND卷注册失败。");
            return false;
        }

        snprintf(s_drive_path, sizeof(s_drive_path), "%u:", static_cast<unsigned int>(s_drive));
        strlcpy(s_mount_path, mountPoint, sizeof(s_mount_path));
        ff_diskio_register(s_drive, &DISK_IO);
        const esp_err_t result = esp_vfs_fat_register(s_mount_path, s_drive_path,
                                                       maxOpenFiles, &s_fatfs);
        if (result != ESP_OK)
        {
            Serial.printf("[FATFS] NAND卷VFS注册失败：esp_err=0x%X。\n",
                          static_cast<unsigned int>(result));
            ff_diskio_unregister(s_drive);
            s_drive = FF_DRV_NOT_USED;
            return false;
        }
        s_vfs_registered = true;
        return true;
    }

    bool MountRegisteredVolume()
    {
        if (!s_fatfs)
            return false;
        const FRESULT result = f_mount(s_fatfs, s_drive_path, 1);
        if (result != FR_OK)
        {
            Serial.printf("[FATFS] 外挂NAND FAT卷挂载失败：FRESULT=%u。\n",
                          static_cast<unsigned int>(result));
            return false;
        }
        s_file_system.Attach(s_mount_path);
        return true;
    }

    bool BeginExistingMap()
    {
        if (!VolumeRecordIsValid())
        {
            Serial.println("[FATFS] 未找到匹配当前正式布局的NAND卷记录；不会尝试挂载或自动格式化。");
            return false;
        }
        const SysNandFtl::ResumeResult result = SysNandFtl::Begin();
        if (result == SysNandFtl::ResumeResult::Resumed)
            return true;
        if (result == SysNandFtl::ResumeResult::NoMap)
            Serial.println("[FATFS] 外挂NAND尚未建立FTL/FAT卷；不会自动格式化。");
        else
            Serial.printf("[FATFS] 外挂NAND FTL初始化失败：Dhara=%s。\n",
                          SysNandFtl::LastErrorName());
        SysNandFtl::End();
        return false;
    }
}

namespace HAL::FatStorage
{
    bool MountForEsp(const char *mountPoint, uint8_t maxOpenFiles)
    {
        if (s_owner == Owner::EspFileSystem)
            return true;
        if (s_owner != Owner::None || !mountPoint || mountPoint[0] == '\0')
            return false;
        if (!BeginExistingMap())
            return false;
        if (!RegisterDiskAndVfs(mountPoint, maxOpenFiles) || !MountRegisteredVolume())
        {
            UnregisterDiskAndVfs(true);
            SysNandFtl::End();
            return false;
        }

        s_owner = Owner::EspFileSystem;
        s_geometry.usableBytes = static_cast<size_t>(SysNandFtl::SectorCount()) *
                                 SysNandFtl::LOGICAL_SECTOR_SIZE;
        s_geometry.blockCount = SysNandFtl::SectorCount();
        s_geometry.blockSize = SysNandFtl::LOGICAL_SECTOR_SIZE;
        Serial.printf("[FATFS] 外挂NAND卷已挂载：路径=%s，容量=%luKiB，扇区=%u字节。\n",
                      mountPoint,
                      static_cast<unsigned long>(s_geometry.usableBytes / 1024U),
                      static_cast<unsigned int>(s_geometry.blockSize));
        return true;
    }

    void UnmountFromEsp()
    {
        if (s_owner != Owner::EspFileSystem)
            return;
        UnregisterDiskAndVfs(true);
        SysNandFtl::End();
        s_geometry = Geometry{};
        s_owner = Owner::None;
    }

    bool OpenForUsb()
    {
        if (s_owner == Owner::UsbMassStorage)
            return true;
        if (s_owner != Owner::None || !BeginExistingMap())
            return false;

        s_sector_cache = static_cast<uint8_t *>(heap_caps_malloc(
            SysNandFtl::LOGICAL_SECTOR_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!s_sector_cache)
        {
            Serial.println("[FATFS][MSC] 无法申请NAND扇区缓存。");
            SysNandFtl::End();
            return false;
        }

        s_geometry.usableBytes = static_cast<size_t>(SysNandFtl::SectorCount()) *
                                 SysNandFtl::LOGICAL_SECTOR_SIZE;
        s_geometry.blockCount = SysNandFtl::SectorCount();
        s_geometry.blockSize = SysNandFtl::LOGICAL_SECTOR_SIZE;
        s_owner = Owner::UsbMassStorage;
        return true;
    }

    void CloseForUsb()
    {
        if (s_owner != Owner::UsbMassStorage)
            return;
        SysNandFtl::End();
        ReleaseSectorCache();
        s_geometry = Geometry{};
        s_owner = Owner::None;
    }

    int32_t Read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufferSize)
    {
        if (!buffer || !s_sector_cache || s_owner != Owner::UsbMassStorage)
            return -1;
        uint64_t address = static_cast<uint64_t>(lba) * s_geometry.blockSize + offset;
        if (!RangeIsValid(address, bufferSize))
            return -1;

        uint8_t *destination = static_cast<uint8_t *>(buffer);
        uint32_t remaining = bufferSize;
        while (remaining > 0)
        {
            const uint32_t sector = static_cast<uint32_t>(address / s_geometry.blockSize);
            const uint16_t sector_offset = static_cast<uint16_t>(address % s_geometry.blockSize);
            const uint32_t chunk = std::min<uint32_t>(remaining, s_geometry.blockSize - sector_offset);
            if (sector_offset == 0 && chunk == s_geometry.blockSize)
            {
                if (!SysNandFtl::Read(sector, destination))
                    return -1;
            }
            else
            {
                if (!SysNandFtl::Read(sector, s_sector_cache))
                    return -1;
                memcpy(destination, s_sector_cache + sector_offset, chunk);
            }
            destination += chunk;
            address += chunk;
            remaining -= chunk;
        }
        return static_cast<int32_t>(bufferSize);
    }

    int32_t Write(uint32_t lba, uint32_t offset, const uint8_t *buffer, uint32_t bufferSize)
    {
        if (!buffer || !s_sector_cache || s_owner != Owner::UsbMassStorage)
            return -1;
        uint64_t address = static_cast<uint64_t>(lba) * s_geometry.blockSize + offset;
        if (!RangeIsValid(address, bufferSize))
            return -1;

        const uint8_t *source = buffer;
        uint32_t remaining = bufferSize;
        while (remaining > 0)
        {
            const uint32_t sector = static_cast<uint32_t>(address / s_geometry.blockSize);
            const uint16_t sector_offset = static_cast<uint16_t>(address % s_geometry.blockSize);
            const uint32_t chunk = std::min<uint32_t>(remaining, s_geometry.blockSize - sector_offset);
            if (sector_offset == 0 && chunk == s_geometry.blockSize)
            {
                if (!SysNandFtl::Write(sector, source))
                    return -1;
            }
            else
            {
                if (!SysNandFtl::Read(sector, s_sector_cache))
                    return -1;
                memcpy(s_sector_cache + sector_offset, source, chunk);
                if (!SysNandFtl::Write(sector, s_sector_cache))
                    return -1;
            }
            source += chunk;
            address += chunk;
            remaining -= chunk;
        }
        return static_cast<int32_t>(bufferSize);
    }

    Owner CurrentOwner() { return s_owner; }
    bool IsMountedForEsp() { return s_owner == Owner::EspFileSystem; }
    bool IsOpenForUsb() { return s_owner == Owner::UsbMassStorage; }
    Geometry GetGeometry() { return s_geometry; }
    fs::FS &FileSystem() { return s_file_system; }

}
