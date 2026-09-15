/*
【模块职责】设备身份绑定交互页。

页面只负责用户意图和结果展示：本地已有凭据时会先点名在线验证任务，只有服务器重新签发
Token 后才显示已绑定；首次绑定与八秒长按恢复绑定仍由用户明确触发。挑战、证明、永久凭据
原子落盘和临时认证全部留在 net 层。
*/
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "net/net_service.h"
#include "net/services/net_device_auth_prepare.h"
#include "net/services/net_device_binding.h"
#include "sys/sys_device_identity.h"
#include "sys/sys_audio.h"
#include "lang/ui_strings.h"
#include "ui/ui_frame.h"

namespace
{
    enum class BindingPageState : uint8_t
    {
        Ready,
        Offline,
        CheckingAuthentication,
        Binding,
        BindingRequired,
        Success,
        Failure,
        AlreadyBound
    };

    constexpr uint32_t kRebindHoldMs = 8UL * 1000UL;
}

class AppDeviceBinding : public AppBase
{
    BindingPageState state_ = BindingPageState::Ready;
    uint32_t baseline_sequence_ = 0;
    uint16_t pending_task_id_ = 0;
    bool task_submit_failed_ = false;
    bool task_not_executed_ = false;
    bool rebind_hold_active_ = false;
    bool rebind_started_ = false;
    uint32_t rebind_hold_started_ms_ = 0;

    /** 将网络层稳定错误类型转换为固定双语文案；页面不解析 HTTP 状态或服务端正文。 */
    const char *failureDetail(SystemLang_t lang) const
    {
        if (task_submit_failed_)
            return UIStrings::DeviceBindingTaskBusy(lang);
        if (task_not_executed_)
            return UIStrings::DeviceBindingDisconnected(lang);

        switch (NetDeviceBinding_GetLastFailure())
        {
        case NetDeviceBindingFailure::IdentityUnavailable:
            return UIStrings::DeviceBindingIdentityUnavailable(lang);
        case NetDeviceBindingFailure::BindingSecretUnavailable:
            return UIStrings::DeviceBindingSecretUnavailable(lang);
        case NetDeviceBindingFailure::RequestTimeout:
            return UIStrings::DeviceBindingRequestTimeout(lang);
        case NetDeviceBindingFailure::SecureConnectionFailed:
            return UIStrings::DeviceBindingSecureConnectionFailed(lang);
        case NetDeviceBindingFailure::ServerConnectionFailed:
            return UIStrings::DeviceBindingServerConnectionFailed(lang);
        case NetDeviceBindingFailure::RequestSendFailed:
            return UIStrings::DeviceBindingRequestSendFailed(lang);
        case NetDeviceBindingFailure::ConnectionLost:
            return UIStrings::DeviceBindingConnectionLost(lang);
        case NetDeviceBindingFailure::InsufficientMemory:
            return UIStrings::DeviceBindingInsufficientMemory(lang);
        case NetDeviceBindingFailure::NetworkTransportFailed:
            return UIStrings::DeviceBindingNetworkFailed(lang);
        case NetDeviceBindingFailure::ServerAlreadyBound:
            return UIStrings::DeviceBindingServerAlreadyBound(lang);
        case NetDeviceBindingFailure::DeviceDisabled:
            return UIStrings::DeviceBindingDeviceDisabled(lang);
        case NetDeviceBindingFailure::ServerRejected:
            return UIStrings::DeviceBindingServerRejected(lang);
        case NetDeviceBindingFailure::ServerUnavailable:
            return UIStrings::DeviceBindingServerUnavailable(lang);
        case NetDeviceBindingFailure::InvalidServerResponse:
            return UIStrings::DeviceBindingInvalidResponse(lang);
        case NetDeviceBindingFailure::ProofGenerationFailed:
            return UIStrings::DeviceBindingProofFailed(lang);
        case NetDeviceBindingFailure::CredentialSaveFailed:
            return UIStrings::DeviceBindingSaveFailed(lang);
        case NetDeviceBindingFailure::SessionAuthenticationFailed:
            return UIStrings::DeviceBindingSessionFailed(lang);
        case NetDeviceBindingFailure::RebindAuthorizationFailed:
            return UIStrings::DeviceBindingRebindAuthorizationFailed(lang);
        case NetDeviceBindingFailure::None:
        case NetDeviceBindingFailure::Unknown:
        default:
            return UIStrings::DeviceBindingFailureUnknown(lang);
        }
    }

    void drawPage()
    {
        const SystemLang_t lang = appManager.getLanguage();
        HAL_Sprite_Clear();
        drawAppWindow(UIStrings::DeviceBindingTitle(lang));

        const char *message = UIStrings::DeviceBindingPrompt(lang);
        const char *detail = "";
        const char *hint = UIStrings::DeviceBindingReadyHint(lang);
        uint16_t color = TFT_CYAN;
        switch (state_)
        {
        case BindingPageState::Offline:
            message = UIStrings::DeviceBindingOffline(lang);
            hint = UIStrings::DeviceBindingCloseHint(lang);
            color = TFT_RED;
            break;
        case BindingPageState::CheckingAuthentication:
            message = UIStrings::DeviceBindingAuthenticating(lang);
            hint = UIStrings::DeviceBindingWaitingHint(lang);
            break;
        case BindingPageState::Binding:
            message = UIStrings::DeviceBindingRunning(lang);
            hint = UIStrings::DeviceBindingWaitingHint(lang);
            break;
        case BindingPageState::BindingRequired:
            message = UIStrings::DeviceBindingAuthenticationFailed(lang);
            hint = UIStrings::DeviceBindingReadyHint(lang);
            color = TFT_RED;
            break;
        case BindingPageState::Success:
            message = UIStrings::DeviceBindingSuccess(lang);
            hint = UIStrings::DeviceBindingCloseHint(lang);
            color = TFT_GREEN;
            break;
        case BindingPageState::Failure:
            message = UIStrings::DeviceBindingFailure(lang);
            detail = failureDetail(lang);
            hint = UIStrings::DeviceBindingCloseHint(lang);
            color = TFT_RED;
            break;
        case BindingPageState::AlreadyBound:
            message = UIStrings::DeviceBindingAlreadyBound(lang);
            hint = UIStrings::DeviceBindingRebindHint(lang);
            color = TFT_GREEN;
            break;
        case BindingPageState::Ready:
            break;
        }

        const int sw = HAL_Get_Screen_Width();
        const int sh = HAL_Get_Screen_Height();
        const int y = sh / 2 - HAL_Get_Font_Line_Height(HAL_FONT_BODY) / 2;
        const int x = (sw - HAL_Get_Text_Width(message)) / 2;
        HAL_Screen_ShowLine_Font(x, y, message, HAL_FONT_BODY, color);
        UIFrame::DrawTip(hint);

        if (state_ == BindingPageState::Offline || state_ == BindingPageState::Success ||
            state_ == BindingPageState::Failure || state_ == BindingPageState::AlreadyBound)
        {
            UIFrame::DrawDialog(message, detail, hint, color);
        }
        HAL_Screen_Update();
    }

    /**
     * 在当前常驻 WiFi 会话上投递一个明确的身份任务，并记录结果序列。
     * 页面只消费自己投递后的新结果，避免把进入页面前的历史任务结果误当成本次结论。
     */
    bool startIdentityTask(uint16_t task_id, BindingPageState running_state)
    {
        baseline_sequence_ = NetService_GetLastOnlineTaskResult().sequence;
        pending_task_id_ = task_id;
        task_submit_failed_ = false;
        task_not_executed_ = false;
        state_ = running_state;
        drawPage();

        if (NetService_StartOnlineTask(task_id))
            return true;

        pending_task_id_ = 0;
        task_submit_failed_ = true;
        state_ = BindingPageState::Failure;
        drawPage();
        return false;
    }

    /**
     * 已通过服务器验证的设备才开放八秒主键恢复入口。计时直接读取主键当前电平，
     * 不修改全局 800 ms 长按规则；松手立即清零，侧键也不会触发重新绑定。
     */
    void updateRebindHold()
    {
        if (state_ != BindingPageState::AlreadyBound)
        {
            rebind_hold_active_ = false;
            return;
        }

        if (!HAL_Is_Key_Pressed())
        {
            rebind_hold_active_ = false;
            rebind_started_ = false;
            return;
        }

        if (!rebind_hold_active_)
        {
            rebind_hold_active_ = true;
            rebind_hold_started_ms_ = millis();
            return;
        }
        if (rebind_started_ || millis() - rebind_hold_started_ms_ < kRebindHoldMs)
            return;

        rebind_started_ = true;
        if (!NetService_IsOnline())
        {
            state_ = BindingPageState::Offline;
            drawPage();
            return;
        }
        sysAudio.playTone(1200, 100);
        startIdentityTask(NET_TASK_DEVICE_REBIND, BindingPageState::Binding);
    }

public:
    void onCreate() override
    {
        const SysDeviceIdentitySnapshot identity = SysDeviceIdentity_GetSnapshot();
        state_ = NetService_IsOnline() ? BindingPageState::Ready : BindingPageState::Offline;
        task_submit_failed_ = false;
        task_not_executed_ = false;
        pending_task_id_ = 0;
        rebind_hold_active_ = false;
        rebind_started_ = false;
        baseline_sequence_ = NetService_GetLastOnlineTaskResult().sequence;
        drawPage();

        /* 本地记录只说明曾经绑定过；最终状态必须以服务器重新签发 Token 的结果为准。 */
        if (identity.provisioned && NetService_IsOnline())
            startIdentityTask(NET_TASK_DEVICE_AUTH_VERIFY, BindingPageState::CheckingAuthentication);
    }

    void onLoop() override
    {
        updateRebindHold();

        if (state_ == BindingPageState::CheckingAuthentication ||
            state_ == BindingPageState::Binding)
        {
            const NetOnlineTaskResult result = NetService_GetLastOnlineTaskResult();
            if (result.sequence != baseline_sequence_)
            {
                const bool completed = result.task_id == pending_task_id_ && result.task_found &&
                                       result.disposition == NetTaskDisposition::Complete;
                const bool was_authentication_check =
                    state_ == BindingPageState::CheckingAuthentication;
                if (was_authentication_check)
                {
                    if (completed)
                        state_ = BindingPageState::AlreadyBound;
                    else if (!NetService_IsOnline())
                        state_ = BindingPageState::Offline;
                    else
                        state_ = BindingPageState::BindingRequired;
                }
                else
                {
                    state_ = completed ? BindingPageState::Success : BindingPageState::Failure;
                }
                pending_task_id_ = 0;
                task_submit_failed_ = false;
                task_not_executed_ = !result.task_found;
                sysAudio.playTone(
                    state_ == BindingPageState::Success || state_ == BindingPageState::AlreadyBound
                        ? 2000
                        : 500,
                    100);
                drawPage();
            }
        }
    }

    void onDestroy() override {}
    void onKnob(int) override {}

    void onKeyShort() override
    {
        if (state_ == BindingPageState::CheckingAuthentication ||
            state_ == BindingPageState::Binding)
            return;
        if (state_ == BindingPageState::Offline || state_ == BindingPageState::Success ||
            state_ == BindingPageState::Failure || state_ == BindingPageState::AlreadyBound)
        {
            appManager.popApp();
            return;
        }
        if (!NetService_IsOnline())
        {
            state_ = BindingPageState::Offline;
            drawPage();
            return;
        }

        startIdentityTask(NET_TASK_DEVICE_BINDING, BindingPageState::Binding);
    }

    void onKeyLong() override
    {
        /* 已绑定页的主键长按由本页累计到八秒；全局 800 ms 长按事件不得提前退出。 */
        if (state_ != BindingPageState::AlreadyBound)
            appManager.popApp();
    }
    /** 绑定是明确的主键操作，阻止 AppBase 把侧键短按自动映射到主键。 */
    void onBtn2Short() override {}
    /** 八秒恢复只允许主键触发，侧键长按保持普通返回语义。 */
    void onBtn2Long() override { appManager.popApp(); }
};

AppDeviceBinding instanceDeviceBinding;
AppBase *appDeviceBinding = &instanceDeviceBinding;
