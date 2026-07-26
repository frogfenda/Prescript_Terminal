/*
【模块职责】实现硬币图片的 /Resources 路径绑定、启动预加载和只读访问。
【设计说明】彩色图片缺失时只在访问层回退普通图片，不重复加载同一普通文件，避免浪费PSRAM。
*/
#include "sys/sys_coin_resources.h"

#include "sys/sys_constants.h"
#include "sys/sys_resource_io.h"

namespace
{
    constexpr uint8_t MATERIAL_COUNT = 3;
    SysLoadedRgb565Asset g_heads[MATERIAL_COUNT];
    SysLoadedRgb565Asset g_tails[MATERIAL_COUNT];

    const char *HEAD_FILES[MATERIAL_COUNT] = {"heads.bin", "rheads.bin", "gheads.bin"};
    const char *TAIL_FILES[MATERIAL_COUNT] = {"tails.bin", "rtails.bin", "gtails.bin"};

    bool LoadFace(const char *fileName, const char *label, SysLoadedRgb565Asset &out)
    {
        const String primary = String(PrescriptConst::COIN_ASSET_DIR) + fileName;
        return SysResourceIO::LoadSquareRgb565({primary.c_str()}, label, 32, 128, out);
    }

    SysRgb565View ToView(const SysLoadedRgb565Asset &asset)
    {
        return asset.view();
    }
}

bool SysCoinResources::Preload()
{
    bool allReady = true;
    for (uint8_t i = 0; i < MATERIAL_COUNT; ++i)
    {
        const String headsLabel = String("硬币正面-") + i;
        const String tailsLabel = String("硬币反面-") + i;
        if (!LoadFace(HEAD_FILES[i], headsLabel.c_str(), g_heads[i]))
            allReady = false;
        if (!LoadFace(TAIL_FILES[i], tailsLabel.c_str(), g_tails[i]))
            allReady = false;
    }
    return allReady;
}

SysRgb565View SysCoinResources::GetFace(uint8_t material, bool tails)
{
    if (material >= MATERIAL_COUNT)
        material = 0;
    const SysLoadedRgb565Asset *assets = tails ? g_tails : g_heads;
    if (assets[material].valid())
        return ToView(assets[material]);
    return ToView(assets[0]);
}
