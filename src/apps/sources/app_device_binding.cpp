/*
【模块职责】设备身份绑定交互页。

页面只负责用户意图和结果展示：进入页面不会发起网络请求，只有主键短按才会点名
NET_TASK_DEVICE_BINDING。真正的挑战、证明、永久凭据落盘和临时认证全部留在 net 层。
*/
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "net/net_service.h"
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
        Running,
        Success,
        Failure,
        AlreadyBound
    };
}

class AppDeviceBinding : public AppBase
{
    BindingPageState state_ = BindingPageState::Ready;
    uint32_t baseline_sequence_ = 0;
    bool task_submit_failed_ = false;
    bool task_not_executed_ = false;

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
        case BindingPageState::Running:
            message = UIStrings::DeviceBindingRunning(lang);
            hint = UIStrings::DeviceBindingWaitingHint(lang);
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
            hint = UIStrings::DeviceBindingCloseHint(lang);
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

public:
    void onCreate() override
    {
        const SysDeviceIdentitySnapshot identity = SysDeviceIdentity_GetSnapshot();
        state_ = identity.provisioned ? BindingPageState::AlreadyBound : BindingPageState::Ready;
        task_submit_failed_ = false;
        task_not_executed_ = false;
        baseline_sequence_ = NetService_GetLastOnlineTaskResult().sequence;
        drawPage();
    }

    void onLoop() override
    {
        if (state_ == BindingPageState::Running)
        {
            const NetOnlineTaskResult result = NetService_GetLastOnlineTaskResult();
            if (result.sequence != baseline_sequence_)
            {
                state_ = result.task_id == NET_TASK_DEVICE_BINDING && result.task_found &&
                                 result.disposition == NetTaskDisposition::Complete
                              ? BindingPageState::Success
                              : BindingPageState::Failure;
                task_submit_failed_ = false;
                task_not_executed_ = !result.task_found;
                sysAudio.playTone(state_ == BindingPageState::Success ? 2000 : 500, 100);
                drawPage();
            }
        }
    }

    void onDestroy() override {}
    void onKnob(int) override {}

    void onKeyShort() override
    {
        if (state_ == BindingPageState::Running)
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

        baseline_sequence_ = NetService_GetLastOnlineTaskResult().sequence;
        task_submit_failed_ = false;
        task_not_executed_ = false;
        if (!NetService_StartOnlineTask(NET_TASK_DEVICE_BINDING))
        {
            task_submit_failed_ = true;
            state_ = BindingPageState::Failure;
            drawPage();
            return;
        }
        state_ = BindingPageState::Running;
        drawPage();
    }

    void onKeyLong() override { appManager.popApp(); }
    /** 绑定是明确的主键操作，阻止 AppBase 把侧键短按自动映射到主键。 */
    void onBtn2Short() override {}
};

AppDeviceBinding instanceDeviceBinding;
AppBase *appDeviceBinding = &instanceDeviceBinding;
