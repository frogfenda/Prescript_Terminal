#pragma once

namespace SysBootTest
{
    /** 返回是否启用了TM6605或IMU采集隔离固件。 */
    bool Enabled();

    /**
     * 【接口说明】返回当前测试固件是否允许侧键启动 USB MSC。
     * IMU脱线采集需要从FAT导出文件，因此允许；TM6605测试固定为CDC-only。
     */
    bool AllowsMscBoot();

    void Setup();
    void Loop();
}

