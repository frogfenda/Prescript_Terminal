#pragma once

/*
【模块职责】编排开机 USB 模式选择与独占 MSC 会话。

- 测试固件固定 CDC-only；普通固件由 BTN2 选择 CDC-only 或 CDC+MSC。
- MSC 模式直接启动 USB 原始块后端，不在枚举前挂载 FAT，避免 U 盘延迟出现。
- MSC 会话内只运行连接页、CDC 服务和安全弹出处理，不进入普通应用初始化。
- 安全弹出后返回普通启动流程，由统一的 FAT 初始化入口检查更新并加载应用资源。

底层 USB 描述符与回调由 SysUsbMode 管理，更新包细节由 SysFatUpdate 管理。
*/
namespace SysUsbSession
{
    // CDC-only 直接返回；MSC 模式在安全弹出后也返回普通启动流程。
    void BeginAndHandleBootMode(bool bootTestEnabled);
}
