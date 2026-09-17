#pragma once
#include <stdint.h>

// Bits describe actions, never a physical pin level. A rising input means
// external 12 V becomes present, even though the optocoupler GPIO is active LOW.
enum InputAction : uint8_t {
    ACTION_UPLINK = 1,
    ACTION_WIFI = 2,
    ACTION_LOAD_ON = 4,
    ACTION_LOAD_OFF = 8,
    ACTION_RELAY_ON = 16,
    ACTION_RELAY_OFF = 32,
    ACTION_RELAY_PULSE = 64
};

inline bool inputActionsValid(uint8_t actions)
{
    if (actions & (0x80 | ACTION_WIFI)) return false; // WiFi hold time is independent.
    if ((actions & (ACTION_LOAD_ON | ACTION_LOAD_OFF)) == (ACTION_LOAD_ON | ACTION_LOAD_OFF)) return false;
    const uint8_t relay = actions & (ACTION_RELAY_ON | ACTION_RELAY_OFF | ACTION_RELAY_PULSE);
    return relay == 0 || relay == ACTION_RELAY_ON || relay == ACTION_RELAY_OFF || relay == ACTION_RELAY_PULSE;
}

inline uint8_t enabledInputActions(uint8_t actions, bool input, bool load, bool relay)
{
    if (!input) return 0;
    if (!load) actions &= ~(ACTION_LOAD_ON | ACTION_LOAD_OFF);
    if (!relay) actions &= ~(ACTION_RELAY_ON | ACTION_RELAY_OFF | ACTION_RELAY_PULSE);
    return actions;
}
