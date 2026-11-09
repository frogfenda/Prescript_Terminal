/*
【模块职责】统一指令接收和提醒队列；公共格式负责内容准备，各独立库负责抽取/进度。
【线程约束】仅Arduino主循环调用。请求借用的文字在Submit返回前有效即可；队列只保存拥有文字的快照。
休眠截止时间和计时业务事实继续由原模块管理，不进入本服务。
*/
#pragma once
#include <Arduino.h>
#include "sys/sys_mailbox.h"
#include "sys/resources/sys_prescript.h"

/** 内容来源是选择方式；属性始终使用同一个内容模型。特殊ID接受UUID或现有别名。 */
enum class SysPrescriptSource : uint8_t { Content, Random, Special };
struct PrescriptRequest
{
    SysPrescriptSource source = SysPrescriptSource::Content;
    PrescriptContentView content;
    const char *special_id = nullptr;

    static PrescriptRequest Direct(const PrescriptContentView &content) {
        PrescriptRequest request; request.content = content; return request;
    }
    static PrescriptRequest Random() {
        PrescriptRequest request; request.source = SysPrescriptSource::Random; return request;
    }
    static PrescriptRequest Special(const char *id) {
        PrescriptRequest request; request.source = SysPrescriptSource::Special; request.special_id = id; return request;
    }
};

/** CurrentPage只发布快照，不导航；仅供手动抽取页保留按键松开/乱码/特殊警报流程。 */
enum class SysReminderDelivery : uint8_t { Queue, Immediate, CurrentPage };
/** 页面返回策略与投递时机独立；排队也可以选择中断原页面。 */
enum class SysReminderPresentation : uint8_t { Interrupt, ReturnToPrevious };
enum class SysClockReminderSource : uint8_t { None, Countdown, Pomodoro, Alarm, Schedule };
/**
 * 只描述本次投递，不写入内容JSON。source/event_id非零组合对排队提醒去重。
 * mailbox_sequence非零时，弹窗取得内容后确认邮件；CurrentPage不接受邮箱/计时元数据。
 */
struct DeliveryOptions
{
    SysReminderDelivery delivery = SysReminderDelivery::Queue;
    SysReminderPresentation presentation = SysReminderPresentation::Interrupt;
    SysClockReminderSource source = SysClockReminderSource::None;
    uint64_t event_id = 0;
    uint64_t mailbox_sequence = 0;
    SysMailboxKind mailbox = SysMailboxKind::Private;
};

/**
 * 唯一提交入口：先检查选项/去重/容量，再准备快照，最后持久化链条进度并无分配交接。
 * 直接内容只复制不入库；随机/特殊由规则模块选择。失败不覆盖展示、不推进、不确认邮件。
 * 返回QUEUED/PRESENTED/PREPARED，特殊立即触发沿用TRIGGERED或NOT_FOUND警告；
 * 错误保留QUEUE_FULL、OUT_OF_MEMORY、SAVE_FAILED以及公共属性/投递选项校验结果。
 */
SysOpResult SysReminder_Submit(const PrescriptRequest &request, const DeliveryOptions &options = {});
/** 显式取消计时会话时移除同来源尚未展示的提示；不撤销展示，不影响邮箱和其它来源。 */
void SysReminder_CancelClock(SysClockReminderSource source);
/** 主循环串行展示；PushNotify/Prescript忙时等待，队列中的全部内容属性已经冻结。 */
void SysReminder_Update();

/** 当前已交付的完整快照；由提醒出口拥有，库编辑/语言重载不会修改它。 */
const PrescriptContent &SysReminder_Presented();
