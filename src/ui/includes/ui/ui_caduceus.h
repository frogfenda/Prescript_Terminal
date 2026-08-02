/*
【模块职责】绘制双蛇杖全屏武器图、九个进度数字和非阻塞的青/红反馈边框。
【分层边界】UI只接收图片视图、步骤进度位图和颜色状态，不知道武器动作、随机顺序、音频或页面导航。
【性能约束】每帧直接复用系统Sprite与PSRAM图片，不申请堆内存；动画由App主循环按帧推进。
*/
#pragma once

#include <Arduino.h>

#include "sys/sys_resource_io.h"

class UICaduceusFeedbackAnimator
{
public:
    /** 从当前时刻启动一次边框闪光；重复触发会用新颜色重新开始，不阻塞主循环。 */
    void trigger(uint16_t color, uint32_t nowMs, uint16_t durationMs = 320);

    /** 清除反馈状态，供页面进入、退出或切换阶段时避免残留边框。 */
    void reset();

    /** 返回当前是否仍需刷新动画帧。millis回绕按无符号差值自然处理。 */
    bool active(uint32_t nowMs) const;

    /** 在当前Sprite顶层绘制本帧边框；不清屏、不推屏。 */
    void draw(uint32_t nowMs) const;

private:
    uint32_t startedMs_ = 0;
    uint16_t durationMs_ = 0;
    uint16_t color_ = 0;
};

namespace UICaduceus
{
    constexpr uint8_t WEAPON_COUNT = 9;

    /**
     * 绘制教学阶段的一帧：先铺满428x142图片，再叠加底部1~9和可选反馈边框。
     * progressMask的bit0~bit8分别对应流程步骤1~9；置位数字显示青色描边，不绑定武器ID。
     */
    void DrawWeaponFrame(const SysRgb565View &image,
                         uint16_t progressMask,
                         const UICaduceusFeedbackAnimator &feedback,
                         uint32_t nowMs);

    /** 绘制不带教学数字的全屏图片，供Furioso节拍、失败特殊武器和最终画面复用。 */
    void DrawImageOnlyFrame(const SysRgb565View &image,
                            const UICaduceusFeedbackAnimator &feedback,
                            uint32_t nowMs);

    /** 图片缺失时绘制可诊断的占位画面；固定文案由调用方传入以支持中英文。 */
    void DrawMissingImage(const char *message,
                          uint16_t progressMask,
                          const UICaduceusFeedbackAnimator &feedback,
                          uint32_t nowMs,
                          bool showProgress = true);
}
