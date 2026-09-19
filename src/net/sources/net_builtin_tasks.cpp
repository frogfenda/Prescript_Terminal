// 文件：src/net/sources/net_builtin_tasks.cpp
#include "net/net_builtin_tasks.h"

#include <Arduino.h>
#include "net/services/net_device_auth_prepare.h"
#include "net/services/net_device_binding.h"
#include "net/services/net_mailbox_receive.h"

void NetBuiltinTasks_RegisterAll()
{
    if (!NetDeviceBinding_Register())
        Serial.println("[网络任务] 设备身份绑定任务安装失败。 ");
    if (!NetDeviceAuthPrepare_Register())
        Serial.println("[网络任务] 设备临时认证准备任务安装失败。 ");
    if (!NetMailboxReceive_Register())
        Serial.println("[网络任务] 设备信箱任务安装失败。 ");
}
