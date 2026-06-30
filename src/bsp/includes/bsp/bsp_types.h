/*
【模块职责】BSP 通用类型。给板级外设驱动提供统一的状态语义，避免每个外设各写一套错误码。
【阅读提示】BSP 只描述硬件是否可用，不承载业务含义；上层 SYS/HAL 决定如何降级。
*/
#pragma once
#include <Arduino.h>

namespace BSP
{
    // 【类型说明】BSP 层统一状态码。只表达硬件访问结果，不表达上层业务处理策略。
    enum class Status : uint8_t
    {
        Ok = 0,
        NotFound,
        BusError,
        InvalidState,
        Unsupported
    };

    // 【函数说明】把 BSP 状态码转换成中文短文本，便于串口日志和调试界面直接输出。
    inline const char *StatusName(Status status)
    {
        switch (status)
        {
        case Status::Ok:
            return "正常";
        case Status::NotFound:
            return "未发现";
        case Status::BusError:
            return "总线错误";
        case Status::InvalidState:
            return "状态无效";
        case Status::Unsupported:
            return "暂不支持";
        default:
            return "未知";
        }
    }
}
