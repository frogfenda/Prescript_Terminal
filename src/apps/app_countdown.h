#pragma once
#include <Arduino.h>

void Countdown_Start(int min, int sec, const char* custom_cmd = nullptr);
bool Countdown_IsActive();
int Countdown_GetRemainingSeconds();
