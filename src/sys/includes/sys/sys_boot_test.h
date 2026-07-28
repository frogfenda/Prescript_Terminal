#pragma once

namespace SysBootTest
{
    /** 返回是否启用了任一隔离测试固件。 */
    bool Enabled();

    /**
     * 【接口说明】返回当前测试固件是否允许侧键启动 USB MSC。
     * IMU 脱线采集需要从 FAT 导出文件，因此允许；纯硬件时序测试继续固定 CDC-only。
     */
    bool AllowsMscBoot();

    void Setup();
    void Loop();
}

