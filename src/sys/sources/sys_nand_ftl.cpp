/*
【实现说明】Dhara的一个逻辑扇区等于W25N01GV的一个2048字节主数据页。物理区域与对外容量
均由sys_nand_ftl.h固定，不能把Dhara会随
坏块估计变化的最大容量直接当作FAT或USB磁盘几何。
【坏块说明】启动时合并Spare Byte0..1出厂标记与内部NVS中的运行期坏块位图。主区Byte0
可能是正常文件数据，芯片使用后不能再把它当坏块标记。Dhara发现的新坏块会立即写入NVS，
使下一次启动仍排除同一物理块；记录损坏时拒绝挂载，绝不冒险复用已知坏块。
*/
#include "sys/sys_nand_ftl.h"

#include <cstddef>
#include <cstring>
#include <Preferences.h>

#include "bsp/bsp_flash_w25n01.h"

extern "C"
{
#include <map.h>
}

namespace
{
    constexpr uint8_t LOG2_PAGE_SIZE = 11; // 2^11 = 2048字节。
    constexpr uint8_t LOG2_PAGES_PER_BLOCK = 6; // 2^6 = 64页。
    constexpr uint8_t GC_RATIO = 8;
    constexpr uint32_t BAD_BLOCK_RECORD_MAGIC = 0x42424E44UL; // "DNBB"。
    constexpr uint16_t BAD_BLOCK_RECORD_VERSION = 1;
    constexpr size_t BAD_BLOCK_BITMAP_BYTES = (SysNandFtl::PHYSICAL_BLOCK_COUNT + 7U) / 8U;
    constexpr const char *BAD_BLOCK_NVS_KEY = "bad_prod";
    constexpr const char *BAD_BLOCK_NVS_NAMESPACE = "nand_ftl";

    struct BadBlockRecord
    {
        uint32_t magic;
        uint16_t version;
        uint16_t firstBlock;
        uint16_t blockCount;
        uint16_t bitmapBytes;
        uint8_t bitmap[BAD_BLOCK_BITMAP_BYTES];
        uint32_t crc32;
    };

    dhara_nand s_nand = {
        LOG2_PAGE_SIZE,
        LOG2_PAGES_PER_BLOCK,
        SysNandFtl::PHYSICAL_BLOCK_COUNT,
    };
    dhara_map s_map = {};
    alignas(4) uint8_t s_map_page_buffer[BSP::W25n01::PAGE_DATA_SIZE] = {};
    alignas(4) uint8_t s_io_page_buffer[BSP::W25n01::PAGE_DATA_SIZE] = {};
    bool s_bad_blocks[SysNandFtl::PHYSICAL_BLOCK_COUNT] = {};
    bool s_ready = false;
    bool s_has_persistent_map = false;
    bool s_array_unprotected = false;
    dhara_error_t s_last_error = DHARA_E_NONE;

    uint16_t PhysicalBlock(dhara_block_t virtualBlock);

    uint32_t Crc32Update(uint32_t crc, const uint8_t *data, size_t length)
    {
        for (size_t index = 0; index < length; ++index)
        {
            crc ^= data[index];
            for (uint8_t bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
        return crc;
    }

    uint32_t RecordCrc32(const BadBlockRecord &record)
    {
        return ~Crc32Update(0xFFFFFFFFUL, reinterpret_cast<const uint8_t *>(&record),
                            offsetof(BadBlockRecord, crc32));
    }

    void SetBadBlockBit(uint8_t *bitmap, uint16_t block)
    {
        bitmap[block >> 3] |= static_cast<uint8_t>(1U << (block & 7U));
    }

    bool BadBlockBitIsSet(const uint8_t *bitmap, uint16_t block)
    {
        return (bitmap[block >> 3] & static_cast<uint8_t>(1U << (block & 7U))) != 0;
    }

    bool LoadPersistentBadBlocks(bool *found)
    {
        if (found)
            *found = false;
        Preferences preferences;
        /*
         * 正式卷第一次初始化时命名空间必然还不存在。Preferences以只读模式打开不存在的
         * 命名空间会直接失败，不能把这个正常的“尚无记录”状态误判成NVS故障；这里使用
         * 可写模式只负责创建空命名空间，真正的坏块位图仍要在全区扫描成功后才写入。
         */
        if (!preferences.begin(BAD_BLOCK_NVS_NAMESPACE, false))
        {
            Serial.println("[NAND-FTL] 无法打开或创建运行期坏块NVS，拒绝启动映射。");
            return false;
        }

        const size_t stored_bytes = preferences.getBytesLength(BAD_BLOCK_NVS_KEY);
        if (stored_bytes == 0)
        {
            preferences.end();
            return true;
        }
        if (stored_bytes != sizeof(BadBlockRecord))
        {
            preferences.end();
            Serial.printf("[NAND-FTL] 运行期坏块记录长度异常：实际=%u，期望=%u；拒绝挂载。\n",
                          static_cast<unsigned int>(stored_bytes),
                          static_cast<unsigned int>(sizeof(BadBlockRecord)));
            return false;
        }

        BadBlockRecord record = {};
        const size_t read_bytes = preferences.getBytes(BAD_BLOCK_NVS_KEY, &record, sizeof(record));
        preferences.end();
        if (read_bytes != sizeof(record) || record.magic != BAD_BLOCK_RECORD_MAGIC ||
            record.version != BAD_BLOCK_RECORD_VERSION ||
            record.firstBlock != SysNandFtl::PHYSICAL_FIRST_BLOCK ||
            record.blockCount != SysNandFtl::PHYSICAL_BLOCK_COUNT ||
            record.bitmapBytes != BAD_BLOCK_BITMAP_BYTES ||
            record.crc32 != RecordCrc32(record))
        {
            Serial.println("[NAND-FTL] 运行期坏块记录校验失败，拒绝挂载以免复用已隔离块。");
            return false;
        }

        uint16_t restored = 0;
        for (uint16_t block = 0; block < SysNandFtl::PHYSICAL_BLOCK_COUNT; ++block)
        {
            if (!BadBlockBitIsSet(record.bitmap, block))
                continue;
            s_bad_blocks[block] = true;
            ++restored;
        }
        if (restored > 0)
            Serial.printf("[NAND-FTL] 已从NVS恢复运行期坏块：%u块。\n",
                          static_cast<unsigned int>(restored));
        if (found)
            *found = true;
        return true;
    }

    bool PersistBadBlocks()
    {
        BadBlockRecord record = {};
        record.magic = BAD_BLOCK_RECORD_MAGIC;
        record.version = BAD_BLOCK_RECORD_VERSION;
        record.firstBlock = SysNandFtl::PHYSICAL_FIRST_BLOCK;
        record.blockCount = SysNandFtl::PHYSICAL_BLOCK_COUNT;
        record.bitmapBytes = BAD_BLOCK_BITMAP_BYTES;
        for (uint16_t block = 0; block < SysNandFtl::PHYSICAL_BLOCK_COUNT; ++block)
        {
            if (s_bad_blocks[block])
                SetBadBlockBit(record.bitmap, block);
        }
        record.crc32 = RecordCrc32(record);

        Preferences preferences;
        if (!preferences.begin(BAD_BLOCK_NVS_NAMESPACE, false))
            return false;
        const size_t written = preferences.putBytes(BAD_BLOCK_NVS_KEY, &record, sizeof(record));
        preferences.end();
        return written == sizeof(record);
    }

    void MarkRuntimeBadBlock(uint16_t block)
    {
        if (block >= SysNandFtl::PHYSICAL_BLOCK_COUNT || s_bad_blocks[block])
            return;
        s_bad_blocks[block] = true;
        if (!PersistBadBlocks())
            Serial.printf("[NAND-FTL] 物理块%u已在本次运行隔离，但坏块NVS写入失败；重启前请停止继续写入。\n",
                          static_cast<unsigned int>(PhysicalBlock(block)));
    }

    uint16_t PhysicalBlock(dhara_block_t virtualBlock)
    {
        return static_cast<uint16_t>(SysNandFtl::PHYSICAL_FIRST_BLOCK + virtualBlock);
    }

    uint16_t PhysicalPage(dhara_page_t virtualPage)
    {
        const uint32_t first_page =
            static_cast<uint32_t>(SysNandFtl::PHYSICAL_FIRST_BLOCK) * BSP::W25n01::PAGES_PER_BLOCK;
        return static_cast<uint16_t>(first_page + virtualPage);
    }

    void SetDharaError(dhara_error_t *out, dhara_error_t value)
    {
        s_last_error = value;
        dhara_set_error(out, value);
    }

    bool EnsureArrayWritable()
    {
        if (s_array_unprotected)
            return true;
        if (!BSP::W25n01::SetArrayWriteProtected(false))
        {
            Serial.printf("[NAND-FTL] 无法解除阵列写保护：错误=%s。\n",
                          BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
            return false;
        }
        s_array_unprotected = true;
        return true;
    }

    bool ScanSpareBadBlockMarkers()
    {
        memset(s_bad_blocks, 0, sizeof(s_bad_blocks));
        bool persistent_record_found = false;
        if (!LoadPersistentBadBlocks(&persistent_record_found))
            return false;
        if (persistent_record_found)
        {
            uint16_t bad_count = 0;
            for (bool bad : s_bad_blocks)
                bad_count += bad ? 1U : 0U;
            Serial.printf("[NAND-FTL] 已使用持久化坏块表：物理块=%u～%u，坏块=%u；跳过全区备用标记扫描。\n",
                          static_cast<unsigned int>(SysNandFtl::PHYSICAL_FIRST_BLOCK),
                          static_cast<unsigned int>(SysNandFtl::PHYSICAL_LAST_BLOCK),
                          static_cast<unsigned int>(bad_count));
            return bad_count < SysNandFtl::PHYSICAL_BLOCK_COUNT;
        }

        uint16_t bad_count = 0;
        for (uint16_t index = 0; index < SysNandFtl::PHYSICAL_BLOCK_COUNT; ++index)
        {
            uint8_t markers[3] = {};
            bool legacy_bad_result = false;
            if (!BSP::W25n01::ReadFactoryBadBlockMarker(PhysicalBlock(index),
                                                        &legacy_bad_result,
                                                        markers))
            {
                Serial.printf("[NAND-FTL] 读取物理块%u坏块标记失败，已按坏块隔离：错误=%s。\n",
                              static_cast<unsigned int>(PhysicalBlock(index)),
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                s_bad_blocks[index] = true;
                ++bad_count;
                continue;
            }

            // 芯片用过以后主区Byte0可以是任意用户数据；运行期只能继续信任备用区标记。
            s_bad_blocks[index] = s_bad_blocks[index] || markers[1] != 0xFF || markers[2] != 0xFF;
            if (s_bad_blocks[index])
            {
                ++bad_count;
                Serial.printf("[NAND-FTL] 物理块%u备用区存在坏块标记，已从映射中排除。\n",
                              static_cast<unsigned int>(PhysicalBlock(index)));
            }
        }
        Serial.printf("[NAND-FTL] 管理区首次坏块扫描完成：物理块=%u～%u，坏块=%u。\n",
                      static_cast<unsigned int>(SysNandFtl::PHYSICAL_FIRST_BLOCK),
                      static_cast<unsigned int>(SysNandFtl::PHYSICAL_LAST_BLOCK),
                      static_cast<unsigned int>(bad_count));
        if (!PersistBadBlocks())
        {
            Serial.println("[NAND-FTL] 首次坏块表写入NVS失败，拒绝启动映射。");
            return false;
        }
        return bad_count < SysNandFtl::PHYSICAL_BLOCK_COUNT;
    }

    void InitEmptyMapObject()
    {
        memset(&s_map, 0, sizeof(s_map));
        memset(s_map_page_buffer, 0xFF, sizeof(s_map_page_buffer));
        dhara_map_init(&s_map, &s_nand, s_map_page_buffer, GC_RATIO);
        s_last_error = DHARA_E_NONE;
    }
}

extern "C" int dhara_nand_is_bad(const struct dhara_nand *, dhara_block_t block)
{
    if (block >= SysNandFtl::PHYSICAL_BLOCK_COUNT)
        return 1;
    return s_bad_blocks[block] ? 1 : 0;
}

extern "C" void dhara_nand_mark_bad(const struct dhara_nand *, dhara_block_t block)
{
    if (block >= SysNandFtl::PHYSICAL_BLOCK_COUNT)
        return;
    const bool already_bad = s_bad_blocks[block];
    MarkRuntimeBadBlock(static_cast<uint16_t>(block));
    Serial.printf("[NAND-FTL] Dhara已隔离运行期坏块：虚拟块=%lu，物理块=%u，NVS=%s。\n",
                  static_cast<unsigned long>(block),
                  static_cast<unsigned int>(PhysicalBlock(block)),
                  already_bad ? "已有记录" : "已更新");
}

extern "C" int dhara_nand_erase(const struct dhara_nand *,
                                 dhara_block_t block,
                                 dhara_error_t *error)
{
    if (block >= SysNandFtl::PHYSICAL_BLOCK_COUNT || s_bad_blocks[block] ||
        !EnsureArrayWritable() ||
        !BSP::W25n01::EraseBlock(PhysicalBlock(block)))
    {
        SetDharaError(error, DHARA_E_BAD_BLOCK);
        return -1;
    }
    return 0;
}

extern "C" int dhara_nand_prog(const struct dhara_nand *,
                                dhara_page_t page,
                                const uint8_t *data,
                                dhara_error_t *error)
{
    if (!data || page >= static_cast<dhara_page_t>(SysNandFtl::PHYSICAL_BLOCK_COUNT) *
                                      BSP::W25n01::PAGES_PER_BLOCK ||
        !EnsureArrayWritable() ||
        !BSP::W25n01::ProgramPage(PhysicalPage(page), data, BSP::W25n01::PAGE_DATA_SIZE))
    {
        SetDharaError(error, DHARA_E_BAD_BLOCK);
        return -1;
    }
    return 0;
}

extern "C" int dhara_nand_is_free(const struct dhara_nand *, dhara_page_t page)
{
    if (page >= static_cast<dhara_page_t>(SysNandFtl::PHYSICAL_BLOCK_COUNT) *
                    BSP::W25n01::PAGES_PER_BLOCK)
        return 0;
    BSP::W25n01::EccStatus ecc = BSP::W25n01::EccStatus::Unknown;
    if (!BSP::W25n01::ReadPage(PhysicalPage(page), 0, s_io_page_buffer,
                               sizeof(s_io_page_buffer), &ecc))
        return 0;
    for (uint8_t value : s_io_page_buffer)
    {
        if (value != 0xFF)
            return 0;
    }
    return 1;
}

extern "C" int dhara_nand_read(const struct dhara_nand *,
                                dhara_page_t page,
                                size_t offset,
                                size_t length,
                                uint8_t *data,
                                dhara_error_t *error)
{
    if (!data || page >= static_cast<dhara_page_t>(SysNandFtl::PHYSICAL_BLOCK_COUNT) *
                            BSP::W25n01::PAGES_PER_BLOCK ||
        offset >= BSP::W25n01::PAGE_DATA_SIZE ||
        length > BSP::W25n01::PAGE_DATA_SIZE - offset)
    {
        SetDharaError(error, DHARA_E_ECC);
        return -1;
    }
    BSP::W25n01::EccStatus ecc = BSP::W25n01::EccStatus::Unknown;
    if (!BSP::W25n01::ReadPage(PhysicalPage(page), static_cast<uint16_t>(offset), data, length, &ecc))
    {
        SetDharaError(error, DHARA_E_ECC);
        return -1;
    }
    return 0;
}

extern "C" int dhara_nand_copy(const struct dhara_nand *nand,
                                dhara_page_t source,
                                dhara_page_t destination,
                                dhara_error_t *error)
{
    if (dhara_nand_read(nand, source, 0, sizeof(s_io_page_buffer), s_io_page_buffer, error) < 0)
        return -1;
    return dhara_nand_prog(nand, destination, s_io_page_buffer, error);
}

namespace SysNandFtl
{
    ResumeResult Begin()
    {
        s_ready = false;
        s_has_persistent_map = false;
        s_array_unprotected = false;
        s_last_error = DHARA_E_NONE;

        if (!BSP::W25n01::Begin())
        {
            Serial.printf("[NAND-FTL] NAND初始化失败：错误=%s。\n",
                          BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
            return ResumeResult::Failed;
        }
        if (!ScanSpareBadBlockMarkers())
        {
            Serial.println("[NAND-FTL] 验证区没有可用物理块，无法启动映射。");
            return ResumeResult::Failed;
        }
        InitEmptyMapObject();
        s_ready = true;

        const uint32_t raw_capacity = dhara_map_capacity(&s_map);
        if (raw_capacity < LOGICAL_SECTOR_COUNT)
        {
            Serial.printf("[NAND-FTL] 可用容量低于固定逻辑容量：Dhara=%lu，固定=%lu；拒绝挂载。\n",
                          static_cast<unsigned long>(raw_capacity),
                          static_cast<unsigned long>(LOGICAL_SECTOR_COUNT));
            s_ready = false;
            return ResumeResult::Failed;
        }

        dhara_error_t error = DHARA_E_NONE;
        if (dhara_map_resume(&s_map, &error) == 0)
        {
            s_has_persistent_map = true;
            s_last_error = DHARA_E_NONE;
            Serial.printf("[NAND-FTL] Dhara映射恢复成功：固定逻辑扇区=%lu，Dhara上限=%lu，已分配=%lu，逻辑容量=%luKiB。\n",
                          static_cast<unsigned long>(SectorCount()),
                          static_cast<unsigned long>(dhara_map_capacity(&s_map)),
                          static_cast<unsigned long>(AllocatedSectorCount()),
                          static_cast<unsigned long>(SectorCount() * (LOGICAL_SECTOR_SIZE / 1024U)));
            return ResumeResult::Resumed;
        }

        s_last_error = error;
        Serial.printf("[NAND-FTL] 未发现可恢复的Dhara映射：状态=%s；不会自动格式化。\n",
                      LastErrorName());
        return ResumeResult::NoMap;
    }

    bool IsReady() { return s_ready; }
    bool HasPersistentMap() { return s_has_persistent_map; }
    uint32_t SectorCount() { return s_ready ? LOGICAL_SECTOR_COUNT : 0; }
    uint32_t AllocatedSectorCount() { return s_ready ? dhara_map_size(&s_map) : 0; }

    bool Read(uint32_t sector, uint8_t *data)
    {
        if (!s_ready || !data || sector >= SectorCount())
            return false;
        dhara_error_t error = DHARA_E_NONE;
        if (dhara_map_read(&s_map, sector, data, &error) < 0)
        {
            s_last_error = error;
            return false;
        }
        s_last_error = DHARA_E_NONE;
        return true;
    }

    bool Write(uint32_t sector, const uint8_t *data)
    {
        if (!s_ready || !data || sector >= SectorCount())
            return false;
        dhara_error_t error = DHARA_E_NONE;
        if (dhara_map_write(&s_map, sector, data, &error) < 0)
        {
            s_last_error = error;
            return false;
        }
        s_last_error = DHARA_E_NONE;
        return true;
    }

    bool Trim(uint32_t firstSector, uint32_t lastSector)
    {
        if (!s_ready || firstSector > lastSector || lastSector >= SectorCount())
            return false;
        for (uint32_t sector = firstSector; sector <= lastSector; ++sector)
        {
            dhara_error_t error = DHARA_E_NONE;
            if (dhara_map_trim(&s_map, sector, &error) < 0 && error != DHARA_E_NOT_FOUND)
            {
                s_last_error = error;
                return false;
            }
        }
        s_last_error = DHARA_E_NONE;
        return true;
    }

    bool Sync()
    {
        if (!s_ready)
            return false;
        dhara_error_t error = DHARA_E_NONE;
        if (dhara_map_sync(&s_map, &error) < 0)
        {
            s_last_error = error;
            return false;
        }
        s_has_persistent_map = true;
        s_last_error = DHARA_E_NONE;
        return true;
    }

    void End()
    {
        // 没找到映射时不能为了“收尾”写入一个新检查点，否则普通挂载失败会改变原始介质。
        if (s_ready && s_has_persistent_map)
            (void)Sync();
        if (s_array_unprotected)
        {
            if (!BSP::W25n01::SetArrayWriteProtected(true))
                Serial.printf("[NAND-FTL] 恢复阵列写保护失败：错误=%s。\n",
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
            s_array_unprotected = false;
        }
        s_ready = false;
    }

    const char *LastErrorName()
    {
        switch (s_last_error)
        {
        case DHARA_E_NONE: return "无";
        case DHARA_E_BAD_BLOCK: return "坏块或擦写失败";
        case DHARA_E_ECC: return "ECC读取失败";
        case DHARA_E_TOO_BAD: return "可用块不足或未发现映射";
        case DHARA_E_RECOVER: return "恢复流程失败";
        case DHARA_E_JOURNAL_FULL: return "日志区已满";
        case DHARA_E_NOT_FOUND: return "逻辑扇区未映射";
        case DHARA_E_MAP_FULL: return "逻辑映射已满";
        case DHARA_E_CORRUPT_MAP: return "逻辑映射损坏";
        default: return "未知Dhara错误";
        }
    }
}
