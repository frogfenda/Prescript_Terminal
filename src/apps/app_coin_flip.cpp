// 文件：src/apps/app_coin_flip.cpp
#include "app_menu_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include <LittleFS.h>

#include "sys/sys_config.h"

// TODO: 引入你的系统配置对象头文件，以便调用保存功能
// #include "sys_config.h"
// extern SysConfig sysConfig;

#define COIN_SIZE 64

// ==========================================
// 【第一维：视觉数据字典】
// ==========================================
struct CoinConfig
{
    const char *title_zh;
    const char *title_en;
    const char *path_heads;
    const char *path_tails;
    const char *res_heads_zh;
    const char *res_heads_en;
    const char *res_tails_zh;
    const char *res_tails_en;
};

// ==========================================
// 【第二维：物理与概率引擎】
// ==========================================
class AppCoinAnim : public AppBase
{
private:
    CoinConfig config;

    uint16_t *img_heads = nullptr;
    uint16_t *img_tails = nullptr;
    uint16_t *coin_buffer = nullptr;

    float current_angle = 0;
    float target_angle = 0;
    bool is_flipping = false;
    bool is_stopping = false;

    int last_half_turn = 0;
    uint32_t last_frame_time = 0;
    bool ui_needs_full_redraw = true;

    void loadCoinData()
    {
        if (!img_heads)
            img_heads = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!img_tails)
            img_tails = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!coin_buffer)
            coin_buffer = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);

        File f1 = LittleFS.open(config.path_heads, "r");
        if (f1)
        {
            f1.read((uint8_t *)img_heads, COIN_SIZE * COIN_SIZE * 2);
            f1.close();
        }

        File f2 = LittleFS.open(config.path_tails, "r");
        if (f2)
        {
            f2.read((uint8_t *)img_tails, COIN_SIZE * COIN_SIZE * 2);
            f2.close();
        }
    }

    void drawScaledCoinToBuffer(float scaleX)
    {
        memset(coin_buffer, 0, COIN_SIZE * COIN_SIZE * 2);

        uint16_t *img = (scaleX >= 0) ? img_heads : img_tails;
        if (!img)
            return;

        int drawW = COIN_SIZE * fabs(scaleX);
        if (drawW % 2 != 0)
            drawW++;
        if (drawW <= 1)
            return;

        int startX = (COIN_SIZE - drawW) / 2;
        bool is_back = (scaleX < 0);
        uint8_t x_map[COIN_SIZE];

        for (int x = 0; x < drawW; x++)
        {
            int origX = x * (COIN_SIZE - 1) / (drawW - 1);
            if (is_back)
                origX = (COIN_SIZE - 1) - origX;
            x_map[x] = origX;
        }

        for (int y = 0; y < COIN_SIZE; y++)
        {
            int y_offset = y * COIN_SIZE;
            for (int x = 0; x < drawW; x++)
            {
                uint16_t color = img[y_offset + x_map[x]];
                if (color != 0x0000)
                {
                    coin_buffer[y_offset + startX + x] = color;
                }
            }
        }
    }

    void drawUI()
    {
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        bool zh = appManager.getLanguage() == LANG_ZH;

        int target_x = 50 - 32;
        int target_y = sh / 2 - 32;

        if (ui_needs_full_redraw)
        {
            HAL_Sprite_Clear();
            int text_x = 100;
            HAL_Screen_ShowChineseLine(text_x, 8, zh ? config.title_zh : config.title_en);

            const char *tip = "";
            if (is_flipping)
            {
                if (sysConfig.coin_data.mode == 1 && !is_stopping)
                {
                    tip = zh ? ">> 单击: 锁定决断" : ">> CLICK: LOCK";
                }
                else
                {
                    tip = zh ? "[ 推演中... ]" : "[ CALCULATING... ]";
                }
            }
            else if (current_angle == 0)
            {
                tip = zh ? "单击: 抛掷 / 长按: 退出" : "CLICK: FLIP / LONG: EXIT";
            }
            else
            {
                tip = zh ? "[ 决断已定 ]" : "[ DECISION MADE ]";
            }
            HAL_Screen_ShowChineseLine_Faded(text_x, 32, tip, 0.4f);

            // 显示理智偏差警告
            if (sysConfig.coin_data.sanity != 0)
            {
                char san_str[32];
                if (sysConfig.coin_data.sanity > 0)
                    sprintf(san_str, "理智波动: +%d", sysConfig.coin_data.sanity);
                else
                    sprintf(san_str, "理智波动: %d", sysConfig.coin_data.sanity);
                HAL_Screen_ShowChineseLine_Faded_Color(sw - 80, 8, san_str, 0.0f, 0xF800); // 红色警告
            }

            if (!is_flipping && current_angle > 0)
            {
                int current_half_turn = round(current_angle / PI);
                bool is_heads = (current_half_turn % 2 == 0);
                const char *res = zh ? (is_heads ? config.res_heads_zh : config.res_tails_zh)
                                     : (is_heads ? config.res_heads_en : config.res_tails_en);
                HAL_Screen_ShowChineseLine_Faded_Color(text_x, 56, res, 0.0f, 0x07FF);
            }
            ui_needs_full_redraw = false;
        }

        float scale = cos(current_angle);
        drawScaledCoinToBuffer(scale);
        HAL_Sprite_PushImage(target_x, target_y, 64, 64, coin_buffer);
        HAL_Screen_Update();
    }

    // 【极其硬核的判定算法】：根据当前理智值进行加权骰子判定
    void rollCoinWithSanity()
    {
        int heads_chance = 50 + sysConfig.coin_data.sanity; // 理智修正后的胜率
        int roll = random(100);                       // 0 ~ 99
        int target_face = (roll < heads_chance) ? 0 : 1;

        int spins = random(8, 14);
        int current_n = round(current_angle / PI);
        int final_n = current_n + spins;

        if (target_face == 0 && (final_n % 2 != 0))
            final_n++;
        if (target_face == 1 && (final_n % 2 == 0))
            final_n++;

        target_angle = final_n * PI;
    }

public:
    AppCoinAnim(CoinConfig cfg) : config(cfg) {}

    void onCreate() override
    {
        loadCoinData();
        current_angle = 0;
        target_angle = 0;
        is_flipping = false;
        is_stopping = false;
        ui_needs_full_redraw = true;
        drawUI();
    }

    void onResume() override
    {
        ui_needs_full_redraw = true;
        drawUI();
    }

    void onLoop() override
    {
        if (is_flipping)
        {
            uint32_t now = millis();
            if (now - last_frame_time < 16)
                return;
            last_frame_time = now;

            float step = 0;
            if (sysConfig.coin_data.mode == 1 && !is_stopping)
            {
                step = 0.28f;
            }
            else
            {
                float dist = target_angle - current_angle;
                step = dist * 0.06f;
                if (step > 0.3f)
                    step = 0.3f;
                if (step < 0.03f)
                    step = 0.03f;
            }

            current_angle += step;

            if ((sysConfig.coin_data.mode == 0 || is_stopping) && current_angle >= target_angle)
            {
                current_angle = target_angle;
                is_flipping = false;
                is_stopping = false;
                ui_needs_full_redraw = true;
                SYS_SOUND_CONFIRM();
            }

            int current_half_turn = (int)(current_angle / PI);
            if (current_half_turn != last_half_turn)
            {
                SYS_SOUND_NAV();
                last_half_turn = current_half_turn;
            }

            drawUI();
        }
    }

    void onDestroy() override
    {
        if (img_heads)
        {
            free(img_heads);
            img_heads = nullptr;
        }
        if (img_tails)
        {
            free(img_tails);
            img_tails = nullptr;
        }
        if (coin_buffer)
        {
            free(coin_buffer);
            coin_buffer = nullptr;
        }
    }

    void onKnob(int delta) override {}

    void onKeyShort() override
    {
        if (is_flipping)
        {
            // 手动模式急停刹车
            if (sysConfig.coin_data.mode == 1 && !is_stopping)
            {
                SYS_SOUND_CONFIRM();
                is_stopping = true;
                ui_needs_full_redraw = true;
                rollCoinWithSanity(); // 在刹车瞬间进行理智加权判定！
            }
            return;
        }

        SYS_SOUND_CONFIRM();
        is_flipping = true;
        is_stopping = false;
        ui_needs_full_redraw = true;
        last_half_turn = (int)(current_angle / PI);

        if (sysConfig.coin_data.mode == 0)
        {
            rollCoinWithSanity(); // 自动模式起步判定
        }
        last_frame_time = millis();
    }

    void onKeyLong() override
    {
        if (!is_flipping)
        {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};

CoinConfig configQuantum = {
    ">> 量子决策模块", ">> QUANTUM FLIP",
    "/assets/coins/heads.bin", "/assets/coins/tails.bin",
    "结果: [ 正面 ]", "RES: [ HEADS ]",
    "结果: [ 反面 ]", "RES: [ TAILS ]"};
AppCoinAnim instanceCoinAnim(configQuantum);

// ==========================================
// 【第三维：机甲风格设置界面】
// ==========================================
class AppCoinSettings : public AppMenuBase
{
private:
    bool is_editing = false; // 编辑模式锁

protected:
    int getMenuCount() override { return 2; }

    const char *getTitle() override
    {
        return appManager.getLanguage() == LANG_ZH ? ">> 决策参数设置" : ">> FLIP SETTINGS";
    }

    // 【常规渲染】：没有被选中或者没在编辑时，显示的完整字符串
    const char *getItemText(int index) override
    {
        static char text_buf[32];
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == 0)
        {
            sprintf(text_buf, zh ? "运行模式 [ %s ]" : "MODE: [ %s ]", sysConfig.coin_data.mode == 0 ? (zh ? "自动" : "AUTO") : (zh ? "手动" : "MANUAL"));
        }
        else if (index == 1)
        {
            if (sysConfig.coin_data.sanity > 0)
                sprintf(text_buf, zh ? "理智波动 [ +%d ]" : "SANITY: [ +%d ]", sysConfig.coin_data.sanity);
            else
                sprintf(text_buf, zh ? "理智波动 [ %d ]" : "SANITY: [ %d ]", sysConfig.coin_data.sanity);
        }
        return text_buf;
    }

    // 【机甲动画渲染】：选中时，剥离核心数值交给底层引擎做上下滑动特效
    bool getItemEditParts(int index, const char **prefix, const char **anim_val, const char **suffix) override
    {
        static char val_str[16];
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == 0)
        {
            *prefix = zh ? "运行模式 [ " : "MODE: [ ";
            *anim_val = sysConfig.coin_data.mode == 0 ? (zh ? "自动" : "AUTO") : (zh ? "手动" : "MANUAL");
            *suffix = " ]";
            return true;
        }
        else if (index == 1)
        {
            *prefix = zh ? "理智波动 [ " : "SANITY: [ ";
            if (sysConfig.coin_data.sanity > 0)
                sprintf(val_str, "+%d", sysConfig.coin_data.sanity);
            else
                sprintf(val_str, "%d", sysConfig.coin_data.sanity);
            *anim_val = val_str;
            *suffix = " ]";
            return true;
        }
        return false; // 返回 true 则底层会调用 drawSegmentedAnimatedText
    }

    // 动态变色：进入编辑状态时变为极其耀眼的警示黄
    uint16_t getItemColor(int index) override
    {
        if (is_editing && index == current_selection)
            return 0xFFE0; // 黄色
        return 0x07FF;     // 默认青色
    }

    void onItemClicked(int index) override
    {
        SYS_SOUND_CONFIRM();
        is_editing = !is_editing;
        if (!is_editing)
        {
            // TODO: 在这里调用全局保存代码，例如：
            // sysConfig.save();
        }
    }

    void onKnob(int delta) override
    {
        if (is_editing)
        {
            // 触发机甲视觉引擎的数值跳动残影
            triggerEditAnimation(delta);

            if (current_selection == 0)
            {
                sysConfig.coin_data.mode = sysConfig.coin_data.mode == 0 ? 1 : 0; // 0和1来回切换
            }
            else if (current_selection == 1)
            {
                sysConfig.coin_data.sanity += delta;
                if (sysConfig.coin_data.sanity > 45)
                    sysConfig.coin_data.sanity = 45;
                if (sysConfig.coin_data.sanity < -45)
                    sysConfig.coin_data.sanity = -45;
            }
        }
        else
        {
            AppMenuBase::onKnob(delta); // 没在编辑时，正常滚动菜单
        }
    }

    void onLongPressed() override
    {
        if (is_editing)
        {
            is_editing = false;
            SYS_SOUND_CONFIRM();
        }
        else
        {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};
AppCoinSettings instanceCoinSettings;

// ==========================================
// 【主入口菜单】
// ==========================================
class AppCoinMenu : public AppMenuBase
{
protected:
    int getMenuCount() override { return 2; }

    const char *getTitle() override
    {
        return appManager.getLanguage() == LANG_ZH ? ">> 量子决策模块" : ">> QUANTUM FLIP";
    }

    const char *getItemText(int index) override
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        switch (index)
        {
        case 0:
            return zh ? "开始推演协议" : "START PROTOCOL";
        case 1:
            return zh ? "终端模块设置" : "MODULE SETTINGS";
        }
        return "";
    }

    void onItemClicked(int index) override
    {
        if (index == 0)
            appManager.pushApp(&instanceCoinAnim);
        else if (index == 1)
            appManager.pushApp(&instanceCoinSettings);
    }

    void onLongPressed() override
    {
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

AppCoinMenu instanceCoinMenu;
AppBase *appCoinFlip = &instanceCoinMenu;