/*
【模块职责】FAT 分区在 ESP/PC 两个访问方之间的互斥所有权与块读写实现。
【实现来源】wear-levelling 挂载和整擦除扇区 RMW 沿用 TestTFT 的已验证逻辑。
*/
#include "hal/hal_fat_storage.h"

#include <FFat.h>
#include <cstring>
#include <memory>
#include <new>

extern "C"
{
#include "esp_partition.h"
#include "wear_levelling.h"
}

namespace
{
    HAL::FatStorage::Owner s_owner = HAL::FatStorage::Owner::None;
    wl_handle_t s_wlHandle = WL_INVALID_HANDLE;
    const esp_partition_t *s_partition = nullptr;
    HAL::FatStorage::Geometry s_geometry;

    bool rangeIsValid(size_t address, size_t length)
    {
        if (s_owner != HAL::FatStorage::Owner::UsbMassStorage ||
            s_wlHandle == WL_INVALID_HANDLE ||
            length > s_geometry.usableBytes)
        {
            return false;
        }
        return address <= s_geometry.usableBytes - length;
    }

    bool writeReadModifyErase(size_t address, const uint8_t *source, size_t length)
    {
        if (!source || !rangeIsValid(address, length))
            return false;

        const size_t eraseSize = wl_sector_size(s_wlHandle);
        if (eraseSize == 0)
            return false;

        std::unique_ptr<uint8_t[]> cache(new (std::nothrow) uint8_t[eraseSize]);
        if (!cache)
            return false;

        while (length > 0)
        {
            const size_t sectorBase = (address / eraseSize) * eraseSize;
            const size_t offsetInSector = address - sectorBase;
            size_t chunk = eraseSize - offsetInSector;
            if (chunk > length)
                chunk = length;

            if (wl_read(s_wlHandle, sectorBase, cache.get(), eraseSize) != ESP_OK)
                return false;

            memcpy(cache.get() + offsetInSector, source, chunk);

            if (wl_erase_range(s_wlHandle, sectorBase, eraseSize) != ESP_OK)
                return false;
            if (wl_write(s_wlHandle, sectorBase, cache.get(), eraseSize) != ESP_OK)
                return false;

            address += chunk;
            source += chunk;
            length -= chunk;
        }

        return true;
    }
}

namespace HAL::FatStorage
{
    bool MountForEsp(const char *mountPoint, uint8_t maxOpenFiles, const char *partitionLabel)
    {
        if (s_owner == Owner::EspFileSystem)
            return true;
        if (s_owner != Owner::None)
            return false;
        if (!mountPoint || mountPoint[0] == '\0' || !partitionLabel || partitionLabel[0] == '\0')
            return false;

        // formatOnFail=false：ESP 不创建、不修复 FAT 卷，格式化只交给 Windows。
        if (!FFat.begin(false, mountPoint, maxOpenFiles, partitionLabel))
            return false;

        s_owner = Owner::EspFileSystem;
        return true;
    }

    void UnmountFromEsp()
    {
        if (s_owner != Owner::EspFileSystem)
            return;
        FFat.end();
        s_owner = Owner::None;
    }

    bool OpenForUsb(const char *partitionLabel)
    {
        if (s_owner == Owner::UsbMassStorage)
            return true;
        if (s_owner != Owner::None)
            return false;
        if (!partitionLabel || partitionLabel[0] == '\0')
            return false;

        s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                               ESP_PARTITION_SUBTYPE_DATA_FAT,
                                               partitionLabel);
        if (!s_partition)
            return false;

        if (wl_mount(s_partition, &s_wlHandle) != ESP_OK)
        {
            s_partition = nullptr;
            s_wlHandle = WL_INVALID_HANDLE;
            return false;
        }

        const size_t blockSize = wl_sector_size(s_wlHandle);
        const size_t usableBytes = wl_size(s_wlHandle);
        const size_t blockCount = blockSize == 0 ? 0 : usableBytes / blockSize;

        if (blockSize == 0 || blockSize > UINT16_MAX ||
            blockCount == 0 || blockCount > UINT32_MAX)
        {
            wl_unmount(s_wlHandle);
            s_partition = nullptr;
            s_wlHandle = WL_INVALID_HANDLE;
            return false;
        }

        s_geometry.usableBytes = usableBytes;
        s_geometry.blockCount = static_cast<uint32_t>(blockCount);
        s_geometry.blockSize = static_cast<uint16_t>(blockSize);
        s_owner = Owner::UsbMassStorage;
        return true;
    }

    void CloseForUsb()
    {
        if (s_owner != Owner::UsbMassStorage)
            return;

        if (s_wlHandle != WL_INVALID_HANDLE)
            wl_unmount(s_wlHandle);

        s_wlHandle = WL_INVALID_HANDLE;
        s_partition = nullptr;
        s_geometry = Geometry{};
        s_owner = Owner::None;
    }

    int32_t Read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufferSize)
    {
        if (!buffer || s_owner != Owner::UsbMassStorage)
            return -1;

        const uint64_t address64 = static_cast<uint64_t>(lba) * s_geometry.blockSize + offset;
        if (address64 > SIZE_MAX)
            return -1;

        const size_t address = static_cast<size_t>(address64);
        if (!rangeIsValid(address, bufferSize))
            return -1;
        if (wl_read(s_wlHandle, address, buffer, bufferSize) != ESP_OK)
            return -1;
        return static_cast<int32_t>(bufferSize);
    }

    int32_t Write(uint32_t lba, uint32_t offset, const uint8_t *buffer, uint32_t bufferSize)
    {
        if (!buffer || s_owner != Owner::UsbMassStorage)
            return -1;

        const uint64_t address64 = static_cast<uint64_t>(lba) * s_geometry.blockSize + offset;
        if (address64 > SIZE_MAX)
            return -1;

        if (!writeReadModifyErase(static_cast<size_t>(address64), buffer, bufferSize))
            return -1;
        return static_cast<int32_t>(bufferSize);
    }

    Owner CurrentOwner()
    {
        return s_owner;
    }

    bool IsMountedForEsp()
    {
        return s_owner == Owner::EspFileSystem;
    }

    bool IsOpenForUsb()
    {
        return s_owner == Owner::UsbMassStorage;
    }

    Geometry GetGeometry()
    {
        return s_geometry;
    }
}
