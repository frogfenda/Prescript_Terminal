/*
【模块职责】定义Furioso图片的运行路径、固定尺寸和常驻PSRAM所有权。
【实现边界】这里只校验并加载文件，不决定哪张图属于普通武器或失败后的特殊武器。
*/
#include "sys/sys_caduceus_resources.h"

#include "sys/sys_constants.h"

namespace
{
    struct CaduceusImageRecord
    {
        const char *fileName;
        const char *logLabel;
        SysLoadedRgb565Asset loaded;
    };

    CaduceusImageRecord g_images[] = {
        {"axe.bin", "双蛇杖图片 axe", {}},
        {"cone.bin", "双蛇杖图片 cone", {}},
        {"giant_sword.bin", "双蛇杖图片 giant_sword", {}},
        {"hammer.bin", "双蛇杖图片 hammer", {}},
        {"rapier.bin", "双蛇杖图片 rapier", {}},
        {"scythe.bin", "双蛇杖图片 scythe", {}},
        {"spear.bin", "双蛇杖图片 spear", {}},
        {"spoon.bin", "双蛇杖图片 spoon", {}},
        {"sword.bin", "双蛇杖图片 sword", {}},
        {"whip.bin", "双蛇杖图片 whip", {}},
    };

    static_assert(sizeof(g_images) / sizeof(g_images[0]) ==
                      (size_t)CaduceusImageId::Count,
                  "双蛇杖图片表必须与CaduceusImageId保持一致");

    constexpr size_t IMAGE_COUNT = sizeof(g_images) / sizeof(g_images[0]);
    size_t g_nextImage = 0;
}

bool SysCaduceusResources::PreloadStep(bool &complete)
{
    if (g_nextImage >= IMAGE_COUNT)
    {
        complete = true;
        return true;
    }

    CaduceusImageRecord &record = g_images[g_nextImage];
    String path = String(PrescriptConst::FURIOSO_IMAGE_DIR) + record.fileName;
    const bool ready = SysResourceIO::LoadRgb565({path.c_str()}, record.logLabel,
                                                  PrescriptConst::UI_SCREEN_WIDTH,
                                                  PrescriptConst::UI_SCREEN_HEIGHT,
                                                  record.loaded);
    ++g_nextImage;
    complete = g_nextImage >= IMAGE_COUNT;
    return ready;
}

SysRgb565View SysCaduceusResources::GetImage(CaduceusImageId id)
{
    const size_t index = (size_t)id;
    if (index >= (size_t)CaduceusImageId::Count)
        return SysRgb565View{};
    return g_images[index].loaded.view();
}
