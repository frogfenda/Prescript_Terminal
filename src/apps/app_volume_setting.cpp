// 文件：src/apps/app_volume_setting.cpp
#include "app_base.h"
#include "sys/app_manager.h"
#include "sys/sys_config.h"
#include "hal/hal.h"

// ==========================================
// 【全局音量调节 APP (0-100 精细游标风格)】
// ==========================================
class AppVolumeSetting : public AppBase {
private:
    int current_vol;
    int old_vol; 

    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        bool zh = appManager.getLanguage() == LANG_ZH;
        
        // 1. 修复标题：中文必须调用 u8g2 中文字库接口！
        // 因为 HAL_Screen_ShowChineseLine 内部自带了 y+12 的偏移，所以这里传 y=-2 大概刚好贴顶
        if (zh) {
            HAL_Screen_ShowChineseLine(10, -2, "[ 系统音量等级 ]");
        } else {
            HAL_Screen_ShowTextLine(10, 8, "[ SYSTEM VOLUME ]");
        }
        
        // 2. 绘制连续型科技感滑块 (全屏宽幅)
        int bar_w = 220;
        int bar_h = 6;
        int start_x = (sw - bar_w) / 2;
        int start_y = 55;

        // 画一条极细的背景轨道线
        HAL_Draw_Rect(start_x, start_y, bar_w, bar_h, 0x07FF); 

        // 绘制实心音量填充
        int fill_w = (current_vol * bar_w) / 100;
        if (fill_w > 0) {
            HAL_Fill_Rect(start_x, start_y, fill_w, bar_h, 0x07FF);
        }

        // 【新增】：在当前进度位置画一个“倒三角”动态游标指示器
        if (current_vol > 0 && current_vol < 100) {
            int pointer_x = start_x + fill_w;
            HAL_Fill_Triangle(pointer_x - 4, start_y - 5, pointer_x + 4, start_y - 5, pointer_x, start_y - 1, 0x07FF);
        }

        // 3. 居中显示 0-100% 的精准数值
        char vol_str[32];
        if (current_vol == 0) {
            sprintf(vol_str, zh ? "- 静音 (MUTE) -" : "- MUTE -");
        } else {
            sprintf(vol_str, "LVL: %d %%", current_vol);
        }
        
        int text_w = HAL_Get_Text_Width(vol_str);
        // 数值依然用中文接口，以兼容中英双语
        HAL_Screen_ShowChineseLine((sw - text_w) / 2, 20, vol_str);
        
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        old_vol = sysConfig.volume;
        current_vol = old_vol;
        drawUI();
    }
    
    void onResume() override { drawUI(); }
    void onLoop() override {}
    void onDestroy() override {}
    
    void onKnob(int delta) override {
        // 【修改】：既然范围是 100，转一格就加减 5，手感最好
        current_vol += (delta * 5);
        if (current_vol < 0) current_vol = 0;
        if (current_vol > 100) current_vol = 100;
        
        sysConfig.volume = current_vol; 
        if (current_vol > 0) SYS_SOUND_NAV(); 
        
        drawUI();
    }
    
    void onKeyShort() override {
        sysConfig.volume = current_vol;
        sysConfig.save();
        SYS_SOUND_CONFIRM();
        appManager.popApp();
    }
    
    void onKeyLong() override {
        sysConfig.volume = old_vol; 
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

AppVolumeSetting instanceVolumeSetting;
AppBase* appVolumeSetting = &instanceVolumeSetting;