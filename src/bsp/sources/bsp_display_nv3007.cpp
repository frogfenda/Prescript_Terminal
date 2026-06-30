/*
【模块职责】NV3007/NV3006A1 长条屏初始化序列实现。初始化序列来自当前已验证的 HAL 版本。
*/
#include "bsp/bsp_display_nv3007.h"

namespace
{
    // 【函数说明】发送屏幕控制器命令字。保留这个薄封装，便于初始化序列保持可读。
    static void SendCmd(TFT_eSPI &tft, uint8_t cmd)
    {
        tft.writecommand(cmd);
    }

    // 【函数说明】发送屏幕控制器数据字节。
    static void SendData(TFT_eSPI &tft, uint8_t data)
    {
        tft.writedata(data);
    }
}

namespace BSP::DisplayNv3007
{
    // 【函数说明】初始化 2.79 寸 142×428 长条屏控制器。
    void Init142x428(TFT_eSPI &tft)
    {
        auto nvCmd = [&](uint8_t cmd) { SendCmd(tft, cmd); };
        auto nvData = [&](uint8_t data) { SendData(tft, data); };

        Serial.println("[BSP][显示] 使用 NV3007/NV3006A1 142x428 初始化序列。");

        // 上电后给控制器和面板电源留稳定时间，避免第一批寄存器写入丢失。
        delay(100);
        delay(120);

        // 以下寄存器序列来自当前实机验证可用的屏幕厂家例程。
        // BSP 只保存这份板级初始化表，不在这里处理 UI 坐标、字体或刷新策略。
        nvCmd(0xff); nvData(0xa5);
        nvCmd(0x9a); nvData(0x08);
        nvCmd(0x9b); nvData(0x08);
        nvCmd(0x9c); nvData(0xb0);
        nvCmd(0x9d); nvData(0x16);
        nvCmd(0x9e); nvData(0xc4);
        nvCmd(0x8f); nvData(0x55); nvData(0x04);
        nvCmd(0x84); nvData(0x90);
        nvCmd(0x83); nvData(0x7b);
        nvCmd(0x85); nvData(0x33);
        nvCmd(0x60); nvData(0x00);
        nvCmd(0x70); nvData(0x00);
        nvCmd(0x61); nvData(0x02);
        nvCmd(0x71); nvData(0x02);
        nvCmd(0x62); nvData(0x04);
        nvCmd(0x72); nvData(0x04);
        nvCmd(0x6c); nvData(0x29);
        nvCmd(0x7c); nvData(0x29);
        nvCmd(0x6d); nvData(0x31);
        nvCmd(0x7d); nvData(0x31);
        nvCmd(0x6e); nvData(0x0f);
        nvCmd(0x7e); nvData(0x0f);
        nvCmd(0x66); nvData(0x21);
        nvCmd(0x76); nvData(0x21);
        nvCmd(0x68); nvData(0x3A);
        nvCmd(0x78); nvData(0x3A);
        nvCmd(0x63); nvData(0x07);
        nvCmd(0x73); nvData(0x07);
        nvCmd(0x64); nvData(0x05);
        nvCmd(0x74); nvData(0x05);
        nvCmd(0x65); nvData(0x02);
        nvCmd(0x75); nvData(0x02);
        nvCmd(0x67); nvData(0x23);
        nvCmd(0x77); nvData(0x23);
        nvCmd(0x69); nvData(0x08);
        nvCmd(0x79); nvData(0x08);
        nvCmd(0x6a); nvData(0x13);
        nvCmd(0x7a); nvData(0x13);
        nvCmd(0x6b); nvData(0x13);
        nvCmd(0x7b); nvData(0x13);
        nvCmd(0x6f); nvData(0x00);
        nvCmd(0x7f); nvData(0x00);
        nvCmd(0x50); nvData(0x00);
        nvCmd(0x52); nvData(0xd6);
        nvCmd(0x53); nvData(0x08);
        nvCmd(0x54); nvData(0x08);
        nvCmd(0x55); nvData(0x1e);
        nvCmd(0x56); nvData(0x1c);
        nvCmd(0xa0); nvData(0x2b); nvData(0x24); nvData(0x00);
        nvCmd(0xa1); nvData(0x87);
        nvCmd(0xa2); nvData(0x86);
        nvCmd(0xa5); nvData(0x00);
        nvCmd(0xa6); nvData(0x00);
        nvCmd(0xa7); nvData(0x00);
        nvCmd(0xa8); nvData(0x36);
        nvCmd(0xa9); nvData(0x7e);
        nvCmd(0xaa); nvData(0x7e);
        nvCmd(0xB9); nvData(0x85);
        nvCmd(0xBA); nvData(0x84);
        nvCmd(0xBB); nvData(0x83);
        nvCmd(0xBC); nvData(0x82);
        nvCmd(0xBD); nvData(0x81);
        nvCmd(0xBE); nvData(0x80);
        nvCmd(0xBF); nvData(0x01);
        nvCmd(0xC0); nvData(0x02);
        nvCmd(0xc1); nvData(0x00);
        nvCmd(0xc2); nvData(0x00);
        nvCmd(0xc3); nvData(0x00);
        nvCmd(0xc4); nvData(0x33);
        nvCmd(0xc5); nvData(0x7e);
        nvCmd(0xc6); nvData(0x7e);
        nvCmd(0xC8); nvData(0x33); nvData(0x33);
        nvCmd(0xC9); nvData(0x68);
        nvCmd(0xCA); nvData(0x69);
        nvCmd(0xCB); nvData(0x6a);
        nvCmd(0xCC); nvData(0x6b);
        nvCmd(0xCD); nvData(0x33); nvData(0x33);
        nvCmd(0xCE); nvData(0x6c);
        nvCmd(0xCF); nvData(0x6d);
        nvCmd(0xD0); nvData(0x6e);
        nvCmd(0xD1); nvData(0x6f);
        nvCmd(0xAB); nvData(0x03); nvData(0x67);
        nvCmd(0xAC); nvData(0x03); nvData(0x6b);
        nvCmd(0xAD); nvData(0x03); nvData(0x68);
        nvCmd(0xAE); nvData(0x03); nvData(0x6c);
        nvCmd(0xb3); nvData(0x00);
        nvCmd(0xb4); nvData(0x00);
        nvCmd(0xb5); nvData(0x00);
        nvCmd(0xB6); nvData(0x32);
        nvCmd(0xB7); nvData(0x7e);
        nvCmd(0xB8); nvData(0x7e);
        nvCmd(0xe0); nvData(0x00);
        nvCmd(0xe1); nvData(0x03); nvData(0x0f);
        nvCmd(0xe2); nvData(0x04);
        nvCmd(0xe3); nvData(0x01);
        nvCmd(0xe4); nvData(0x0e);
        nvCmd(0xe5); nvData(0x01);
        nvCmd(0xe6); nvData(0x19);
        nvCmd(0xe7); nvData(0x10);
        nvCmd(0xe8); nvData(0x10);
        nvCmd(0xea); nvData(0x12);
        nvCmd(0xeb); nvData(0xd0);
        nvCmd(0xec); nvData(0x04);
        nvCmd(0xed); nvData(0x07);
        nvCmd(0xee); nvData(0x07);
        nvCmd(0xef); nvData(0x09);
        nvCmd(0xf0); nvData(0xd0);
        nvCmd(0xf1); nvData(0x0e); nvData(0x17);
        nvCmd(0xf2); nvData(0x2c); nvData(0x1b); nvData(0x0b); nvData(0x20);
        nvCmd(0xe9); nvData(0x29);
        nvCmd(0xec); nvData(0x04);
        nvCmd(0x35); nvData(0x00);
        nvCmd(0x44); nvData(0x00); nvData(0x10);
        nvCmd(0x46); nvData(0x10);
        nvCmd(0xff); nvData(0x00);
        nvCmd(0x3a); nvData(0x05);

        // 退出 sleep 后面板需要较长稳定时间，然后再开启显示。
        nvCmd(0x11);
        delay(220);
        nvCmd(0x29);
    }

    // 【函数说明】让屏幕控制器进入 sleep in。背光关闭由电源模块负责。
    void Sleep(TFT_eSPI &tft)
    {
        tft.writecommand(0x10);
    }

    // 【函数说明】让屏幕控制器退出 sleep。HAL 会在稳定后重新刷新画面并点亮背光。
    void Wakeup(TFT_eSPI &tft)
    {
        tft.writecommand(0x11);
    }
}

