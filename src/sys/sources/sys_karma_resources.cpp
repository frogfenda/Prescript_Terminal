/*
【模块职责】实现业力图片路径绑定、启动预加载和只读访问。
【设计说明】三个模式是独立资源槽；即使当前素材内容相同，也分别加载image1~3，方便以后直接替换单个模式。
*/
#include "sys/sys_karma_resources.h"

#include "sys/sys_constants.h"
#include "sys/sys_resource_io.h"

namespace
{
    SysLoadedRgb565Asset g_images[PrescriptConst::MAX_KARMA_MODES];
    const char *IMAGE_FILES[PrescriptConst::MAX_KARMA_MODES] = {
        "image1.bin", "image2.bin", "image3.bin"};

    bool LoadImage(uint8_t mode)
    {
        const String path = String(PrescriptConst::KARMA_IMAGE_DIR) + IMAGE_FILES[mode];
        const String label = String("业力模式图片-") + (mode + 1);
        return SysResourceIO::LoadSquareRgb565({path.c_str()}, label.c_str(), 32, 128,
                                               g_images[mode]);
    }
}

bool SysKarmaResources::Preload()
{
    bool allReady = true;
    for (uint8_t mode = 0; mode < PrescriptConst::MAX_KARMA_MODES; ++mode)
    {
        if (!LoadImage(mode))
            allReady = false;
    }
    return allReady;
}

SysRgb565View SysKarmaResources::GetImage(uint8_t mode)
{
    if (mode >= PrescriptConst::MAX_KARMA_MODES)
        return SysRgb565View{};
    return g_images[mode].view();
}
