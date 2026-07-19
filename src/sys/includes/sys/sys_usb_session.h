#pragma once

/*
【模块职责】编排开机 USB 模式选择与独占 MSC 会话。

- 测试固件固定 CDC-only；普通固件由 BTN2 选择 CDC-only 或 CDC+MSC。
- MSC 枚举前短暂独占 FAT 以创建 /Update，随后切换给 USB 原始块后端。
- MSC 会话内只运行连接页、CDC 服务和安全弹出处理，不进入普通应用初始化。
- 安全弹出后切回 ESP 挂载、检查更新，并通过干净重启重新加载应用资源。

底层 USB 描述符与回调由 SysUsbMode 管理，更新包细节由 SysFatUpdate 管理。
*/
namespace SysUsbSession
{
    // CDC-only 时完成初始化后返回；MSC 模式正常不会返回。
    void BeginAndHandleBootMode(bool bootTestEnabled);
}
