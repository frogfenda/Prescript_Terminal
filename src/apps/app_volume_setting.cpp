// 文件：src/apps/app_volume_setting.cpp
#include "app_base.h"
#include "sys/app_manager.h"
#include "sys/sys_config.h"
#include "hal/hal.h"
#include "sys/sys_audio.h"
#include "sys/sys_haptic.h"

// 外部引用系统硬件对象
extern SysHaptic sysHaptic;

class AppVolumeSetting : public AppBase
{
private:
    float target_vol;
    float target_haptic;
    float display_vol;
    float display_haptic;
    uint8_t focus_idx = 0;

    // ==========================================
    // 【战术进度条引擎】
    // ==========================================
    void drawTacticalSlider(int x, int y, int w, int h, float val, bool is_focus, const char *label, bool is_volume)
    {
        // 1. 标题居中绘制
        if (is_focus)
        {
            char buf[32];
            sprintf(buf, "[ %s ]", label);
            HAL_Screen_ShowChineseLine(x + (w - HAL_Get_Text_Width(buf)) / 2, y - 22, buf);
        }
        else
        {
            HAL_Screen_ShowChineseLine(x + (w - HAL_Get_Text_Width(label)) / 2, y - 22, label);
        }

        // 2. 绘制战术外框
        HAL_Draw_Rect(x, y, w, h, is_focus ? 0x07FF : 0x8410);

        // 3. 计算并绘制填充条
        int fill_w = (int)((val / 100.0f) * (w - 4));
        if (fill_w < 0)
            fill_w = 0;

        uint16_t color = is_volume ? 0x07FF : 0xFBE0; // 音量青色，震动橙色
        if (!is_focus)
            color = 0x8410; // 失去焦点变灰

        if (fill_w > 0)
        {
            HAL_Fill_Rect(x + 2, y + 2, fill_w, h - 4, color);
        }

        // 4. 百分比与档位文字显示
        char p_str[10];
        if (!is_volume)
        {
            // 震动条：显示物理档位
            if (val <= 5.0f)
                sprintf(p_str, "OFF");
            else if (val <= 40.0f)
                sprintf(p_str, "MIN"); // 1档
            else if (val <= 70.0f)
                sprintf(p_str, "MID"); // 2档
            else
                sprintf(p_str, "MAX"); // 3档
        }
        else
        {
            // 音量条：显示真实百分比
            if (val <= 0.5f)
                sprintf(p_str, "OFF");
            else
                sprintf(p_str, "%d%%", (int)val);
        }

        HAL_Screen_ShowChineseLine(x + w + 5, y, p_str);
    }

public:
    void onCreate() override
    {
        // 音量目标值初始化
        target_vol = sysConfig.volume;

        // 震动目标值初始化：映射三级强度 (0, 33.3, 66.6, 100)
        if (!sysConfig.haptic_enable)
        {
            target_haptic = 0.0f;
        }
        else
        {
            target_haptic = (sysConfig.haptic_intensity * 100.0f) / 3.0f;
        }

        display_vol = target_vol;
        display_haptic = target_haptic;
        focus_idx = 0;
    }

    void onResume() override {}
    void onDestroy() override {}

    void onKnob(int delta) override
    {
        if (focus_idx == 0)
        {
            // --- 音量：无级调节 ---
            target_vol += (delta * 5);
            if (target_vol < 0)
                target_vol = 0;
            if (target_vol > 100)
                target_vol = 100;

            sysConfig.volume = (int)target_vol;
            SYS_SOUND_NAV();
        }
        else
        {
            // --- 震动：磁吸段落调节 ---
            // 1. 根据当前进度反推当前档位 (0, 1, 2, 3)
            int current_level = 0;
            if (target_haptic > 80)
                current_level = 3;
            else if (target_haptic > 40)
                current_level = 2;
            else if (target_haptic > 10)
                current_level = 1;

            // 2. 档位加减
            current_level += delta;
            if (current_level < 0)
                current_level = 0;
            if (current_level > 3)
                current_level = 3;

            // 3. 映射回目标进度值
            target_haptic = (current_level * 100.0f) / 3.0f;

            // 4. 同步更新底层系统配置
            sysConfig.haptic_enable = (current_level > 0);
            if (current_level > 0)
            {
                sysConfig.haptic_intensity = current_level;
            }

            // 5. 立即给予声震反馈，让用户体验新档位的力度
            SYS_SOUND_NAV();
            sysHaptic.playConfirm();
        }
    }

    void onKeyShort() override
    {
        // 切换焦点
        focus_idx = (focus_idx + 1) % 2;
        SYS_SOUND_NAV();
        sysHaptic.playTick();
    }

    void onKeyLong() override
    {
        // 长按退出：执行真正的文件系统持久化存储
        sysConfig.save();
        SYS_SOUND_LONG();
        appManager.popApp();
    }

    void onLoop() override
    {
        // 核心动画：每帧向目标逼近 20%
        display_vol += (target_vol - display_vol) * 0.2f;
        display_haptic += (target_haptic - display_haptic) * 0.2f;

        HAL_Sprite_Clear();
        bool zh = appManager.getLanguage() == LANG_ZH;

        // 绘制屏幕中线
        HAL_Draw_Line(142, 15, 142, 63, 0x2104);

        // 渲染进度条 (统一锚点在 Y=42，实现居中对齐)
        int base_y = 42;
        drawTacticalSlider(10, base_y, 100, 10, display_vol, focus_idx == 0, zh ? "音量输出" : "VOLUME", true);
        drawTacticalSlider(155, base_y, 100, 10, display_haptic, focus_idx == 1, zh ? "触感反馈" : "HAPTIC", false);

        HAL_Screen_Update();
    }
};

AppVolumeSetting instanceVolumeSetting;
AppBase *appVolumeSetting = &instanceVolumeSetting;