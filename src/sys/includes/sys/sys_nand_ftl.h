/*
【模块职责】把 W25N01GV 的原始页/块接口适配为 Dhara 逻辑扇区映射，向上层提供可覆盖写、
磨损均衡、坏块跳过和掉电恢复所需的块设备语义。
【存储布局】正式固件管理物理块0～999，以固定51200个2048字节逻辑扇区向上层提供
100MiB块设备；物理块1000～1023保留给后续维护和坏块替换策略。
【分层约束】本模块不认识FAT目录和业务文件；FatFs/MSC适配由HAL::FatStorage统一拥有。
接口同步且非线程安全，调用方必须保证ESP文件系统或USB MSC只有一个存储所有者。
*/
#pragma once

#include <Arduino.h>

namespace SysNandFtl
{
    static constexpr uint16_t LOGICAL_SECTOR_SIZE = 2048;
    static constexpr uint16_t PHYSICAL_FIRST_BLOCK = 0;
    static constexpr uint16_t PHYSICAL_BLOCK_COUNT = 1000;
    static constexpr uint32_t LOGICAL_SECTOR_COUNT = 51200; // 固定100MiB，不随坏块估计增减。
    static constexpr uint16_t PHYSICAL_LAST_BLOCK = PHYSICAL_FIRST_BLOCK + PHYSICAL_BLOCK_COUNT - 1;

    enum class ResumeResult : uint8_t
    {
        Resumed,
        NoMap,
        Failed,
    };

    /**
     * 初始化NAND，合并备用区坏块标记与内部NVS中的运行期坏块表，并尝试恢复Dhara映射。
     * 仅当Dhara当前原始容量不小于固定逻辑容量时才允许对外就绪。
     */
    ResumeResult Begin();

    bool IsReady();
    bool HasPersistentMap();
    uint32_t SectorCount();
    uint32_t AllocatedSectorCount();

    bool Read(uint32_t sector, uint8_t *data);
    bool Write(uint32_t sector, const uint8_t *data);
    bool Trim(uint32_t firstSector, uint32_t lastSector);
    bool Sync();

    /** 同步已存在的映射并恢复NAND阵列写保护；文件系统卸载或MSC关闭前调用。 */
    void End();

    /** 返回最后一次 Dhara 错误的中文名称，便于稳定的模块化串口日志。 */
    const char *LastErrorName();
}
