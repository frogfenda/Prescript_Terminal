/*
【模块职责】为已绑定设备建立本轮临时认证上下文。

首次绑定只能由系统设置页显式启动。本任务在普通联网会话的准备阶段运行：未绑定设备直接跳过，
已绑定设备才使用 identity 分区中的永久凭据换取 RAM Token，避免“连上 WiFi 就自动绑定”。
*/
#include "net/services/net_device_auth_prepare.h"

#include "net/net_auth_session.h"
#include "net/net_task_registry.h"
#include "sys/sys_device_identity.h"

namespace
{
    NetTaskExecutionResult EnsureBoundDeviceSession(const NetTaskContext &context)
    {
        const SysDeviceIdentitySnapshot identity = SysDeviceIdentity_GetSnapshot();
        if (!identity.storage_ready || identity.machine_code.isEmpty())
        {
            Serial.println("[设备认证准备] 身份分区或机器码不可用，无法建立临时会话。 ");
            return {NetTaskDisposition::PermanentFailure, 0};
        }

        if (!identity.provisioned)
        {
            Serial.println("[设备认证准备] 设备尚未绑定，跳过临时认证。 ");
            return {NetTaskDisposition::Complete, 0};
        }

        const NetAuthEnsureResult auth = NetAuthSession_Ensure(context);
        if (auth == NetAuthEnsureResult::Ready)
            return {NetTaskDisposition::Complete, 0};
        if (auth == NetAuthEnsureResult::CredentialsRejected ||
            auth == NetAuthEnsureResult::DeviceUnbound)
            return {NetTaskDisposition::AuthBlocked, 0};
        return {NetTaskDisposition::Retry, 5UL * 60UL};
    }

    /** 普通会话只补齐缺失 Token；同一 WiFi 会话内已经认证过时不重复请求服务器。 */
    NetTaskExecutionResult ExecutePrepare(const NetTaskInvocation &, const NetTaskContext &context)
    {
        return EnsureBoundDeviceSession(context);
    }

    /**
     * 身份绑定页必须取得服务器的当前判断，不能沿用 RAM 中可能早于后台重置的 Token。
     * 因此先清除本轮 Token，再用 identity 分区中的永久凭据重新创建一次设备会话。
     */
    NetTaskExecutionResult ExecuteVerify(const NetTaskInvocation &, const NetTaskContext &context)
    {
        NetAuthSession_Clear();
        return EnsureBoundDeviceSession(context);
    }
}

bool NetDeviceAuthPrepare_Register()
{
    NetTaskDefinition prepare;
    prepare.task_id = NET_TASK_DEVICE_AUTH_PREPARE;
    prepare.name = "设备临时认证准备";
    prepare.execute = ExecutePrepare;
    prepare.trigger_mask = NET_TASK_TRIGGER_SESSION_PREPARE;
    if (!NetTaskRegistry_Register(prepare))
        return false;

    NetTaskDefinition verify;
    verify.task_id = NET_TASK_DEVICE_AUTH_VERIFY;
    verify.name = "设备身份在线验证";
    verify.execute = ExecuteVerify;
    verify.trigger_mask = NET_TASK_TRIGGER_NONE;
    return NetTaskRegistry_Register(verify);
}
