/*
【模块职责】所有 App 的生命周期基类。AppManager 通过这些虚函数统一分发旋钮、主按键、副按键、后台 tick；这里也提供菜单数值跳动动画和通用顶部窗口绘制工具。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/app_base.h
#ifndef __APP_BASE_H
#define __APP_BASE_H

#include <Arduino.h>
#include "hal/hal.h"
#include "sys/sys_time.h"
#include "sys/sys_gesture.h"
#include "ui/ui_theme.h"
#include "ui/ui_value_animator.h"

class AppBase {
public:
    virtual ~AppBase() {}

    // 【接口说明】页面第一次进入时由 AppManager 调用；派生类在这里初始化本页状态并绘制首帧。
    virtual void onCreate() = 0;
    virtual void onResume() {}     
    // 【函数说明】当前页面被 push 到后台前调用；默认不处理，用于暂停动画和保存临时状态。
    virtual void onBackground() {} 
    virtual void onLoop() = 0;
    // 【接口说明】页面被关闭或替换时调用；派生类在这里停止音效、清理临时标志。
    virtual void onDestroy() = 0;

    virtual void onKnob(int delta) = 0;
    /**
     * 【接口说明】接收不等价于旋钮的离散运动手势；默认忽略。
     * AppManager 已把 ScrollUp/ScrollDown 统一转换为 onKnob()，因此具体页面通常只需处理
     * WeaponChange 等业务手势，不能在这里再次处理滚动造成一次动作执行两遍。
     */
    virtual void onGesture(const SysGestureEvent &event) { (void)event; }
// 主旋钮按键
    // 【函数说明】旋钮主按键短按入口；默认空实现。
    virtual void onKeyShort() {}
    virtual void onKeyLong() {}
    // 【函数说明】旋钮主按键双击入口；默认空实现。
    virtual void onKeyDouble() {} // 【新增】：旋钮也可以双击了！

    // 副按键 (7号引脚)
    // 【函数说明】侧边副按键短按入口；AppManager 全局拦截后才会下发到页面。
    virtual void onBtn2Short() {}
    virtual void onBtn2Long() {}
    // 【函数说明】侧边副按键双击入口；指令页内部使用，其他页面通常由 AppManager 全局进入指令页。
    virtual void onBtn2Double() {}
    
// 【新增】：后台滴答钩子。哪怕 App 没显示在屏幕上，系统也会在后台呼叫它！
    // 【函数说明】后台 tick 入口；注册为后台 App 后，即使不在屏幕上也会被 AppManager 每帧调用。
    virtual void onBackgroundTick() {}
    virtual void onSystemInit() {}
protected:
    /*
     * 菜单数值编辑动画引擎。
     *
     * 原来 triggerEditAnimation / updateEditAnimation / drawSegmentedAnimatedText
     * 的具体实现直接写在 AppBase 里。现在动画算法被抽到 src/ui/ui_value_animator.h，
     * AppBase 只保留这三个薄封装，保证 AppPushSetting / AppCoinSettings / AppTimeSetting
     * 等旧调用点不用改大量业务代码，同时 UI 引擎已经归入 ui 层统一维护。
     */
    UIValueAnimator value_animator;

    /**
     * 启动菜单动态值跳动动画。
     *
     * 调用场景：
     * - 指令推送配置编辑“开启/关闭、最短潜伏、最长潜伏”；
     * - 硬币设置编辑数值；
     * - 时间设置编辑“周期校时、校时间隔”。
     */
    void triggerEditAnimation(int delta) {
        value_animator.trigger(delta);
    }

    /**
     * 推进菜单动态值动画。
     *
     * AppMenuBase::onLoop() 每帧调用它；只有动画进入下一帧时才返回 true，
     * 这样菜单不会因为编辑动画而无节制刷新屏幕。
     */
    bool updateEditAnimation() {
        return value_animator.update();
    }

    /**
     * 绘制“前缀 + 动态值 + 后缀”的菜单项。
     *
     * 该函数是 UIValueAnimator::drawSegmentedText() 的兼容封装；
     * 子类只需要在 getItemEditParts() 中提供三段文本，就能得到和指令推送配置一致的编辑动画。
     */
    void drawSegmentedAnimatedText(int x, int y, const char* prefix, const char* anim_val, const char* suffix, float distance = 0.0f) {
        value_animator.drawSegmentedText(x, y, prefix, anim_val, suffix, distance);
    }

    // 【函数说明】绘制非菜单页面的通用页眉：标题在左，当前时间在右，下方一条横线分隔内容区。
    void drawAppWindow(const char* title) {
        HAL_Screen_DrawHeader();

        int sw = HAL_Get_Screen_Width();
        HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, title);
        
        char time_str[10];
        SysTime_GetTimeString(time_str);
        
        int time_x = sw - UI_TIME_SAFE_PAD - HAL_Get_Text_Width(time_str);
        HAL_Screen_ShowTextLine(time_x, UI_TEXT_Y_TOP, time_str);
        
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);
    }
};



#include "ui/ui_animators.h"


#endif
