#pragma once

#include <Arduino.h>

// Shared by the legacy BLE-AT path, the ESP-v4 protocol parser and the LoRaWAN
// decoder table. Codes 1..11 are ESP/native transaction results; code 12 is
// generated locally by STM32 when the companion link/protocol times out.
enum BleResult : uint8_t
{
    BLE_NOT_RUN = 0,
    BLE_OK = 1,
    BLE_BAD_SETTINGS = 2,
    BLE_STACK_INIT_FAILED = 3,
    BLE_AT_FIRMWARE_MISSING = BLE_STACK_INIT_FAILED,
    BLE_TARGET_NOT_FOUND = 4,
    BLE_CONNECT_FAILED = 5,
    BLE_GATT_NOT_FOUND = 6,
    BLE_INITIAL_READ_FAILED = 7,
    BLE_WRITE_FAILED = 8,
    BLE_VERIFY_FAILED = 9,
    BLE_SECURITY_FAILED = 10,
    BLE_BUSY = 11,
    BLE_COMPANION_TIMEOUT = 12
};
