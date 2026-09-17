#pragma once
#include <Arduino.h>
// Main-loop only. No Flash/NVS, no secrets, fixed 24 entries (~288 bytes).
struct ActivityEntry { uint32_t seconds, value; uint8_t code; };
void activityTick();
uint32_t activitySeconds();
void activityAdd(uint8_t code, uint32_t value=0);
String activityJson();
