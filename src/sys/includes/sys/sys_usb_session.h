#pragma once

/*
【模块职责】编排开机 USB 模式选择与独占 MSC 会话。

- 普通固件由 BTN2 选择 CDC-only 或 CDC+MSC；需要导出 FAT 数据的隔离测试可显式允许 MSC。
- 允许 MSC 的脱线采集测试在 BTN2 未按下时完全不启动 USB，避免 TinyUSB 高优先级任务干扰采样。
- MSC 模式直接启动 USB 原始块后端，不在枚举前挂载 FAT，避免 U 盘延迟出现。
- MSC 会话内只运行连接页、CDC 服务和安全弹出处理，不进入普通应用初始化。
- 安全弹出后返回普通启动流程，由统一的 FAT 初始化入口检查更新并加载应用资源。

底层 USB 描述符与回调由 SysUsbMode 管理，更新包细节由 SysFatUpdate 管理。
*/
namespace SysUsbSession
{
    /**
     * 【参数】bootTestEnabled 表示后续进入隔离测试；allowMscForBootTest 只允许需要导出 FAT 数据的
     * 测试使用。其他测试仍强制 CDC-only，避免无关硬件测试意外取得 FAT 原始块所有权。
     * 【返回】普通 CDC-only 直接返回；脱线采集的无侧键启动也直接返回，但不会初始化 USB。
     * 普通固件 MSC 安全弹出后继续启动；脱线采集 MSC 安全弹出后先复用 /Update 检查，
     * 没有更新包才自动重启到无 USB 记录状态。
     */
    void BeginAndHandleBootMode(bool bootTestEnabled, bool allowMscForBootTest = false);
}
