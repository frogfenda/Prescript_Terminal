/* 时钟业务共用范围；配置、协议、服务与滚轮使用同一规则，技术上限与产品范围分开。 */
#pragma once
#include <stdint.h>
namespace SysClockRules {
constexpr int WorkMaxMinutes = 120, RestMaxMinutes = 60, CountdownMaxMinutes = 99, PomodoroMaxRounds = 12;
inline bool PomodoroDuration(int work, int rest)
{ return work >= 1 && work <= WorkMaxMinutes && rest >= 1 && rest <= RestMaxMinutes; }
inline bool PomodoroRounds(int rounds)
{ return rounds >= 1 && rounds <= PomodoroMaxRounds; }
inline bool CountdownDuration(int minutes, int seconds)
{ return minutes >= 0 && minutes <= CountdownMaxMinutes && seconds >= 0 && seconds <= 59 && (minutes || seconds); }
inline bool AlarmTime(int hour, int minute)
{ return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59; }
}
