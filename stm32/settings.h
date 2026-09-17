#pragma once
// Local installation secrets must never be committed or distributed.
#if !defined(LORABLE_PUBLIC_BUILD) && __has_include("settings.local.h")
#include "settings.local.h"
#endif

// ESP-AT mode does not add BLE support to WiFi-only ESP firmware.
// See README.md before enabling the experimental SmartSolar LOAD action.
#ifndef LEGACY_BLE_AT
#define LEGACY_BLE_AT 0
#endif
#ifndef CONFIG_WINDOW_SECONDS
#define CONFIG_WINDOW_SECONDS 3600UL
#endif
#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID "Victron LoRaBLE Remote"
#endif
#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD "CHANGE-ME-FIRST"
#endif
#ifndef LORAWAN_DEVEUI
// Normally leave zero: setup reads the board's provisioned/current RUI DevEUI.
// This value is only a fallback when the RUI identity cannot be read.
#define LORAWAN_DEVEUI {0}
#endif
#ifndef LORAWAN_APPEUI
#define LORAWAN_APPEUI {0}
#endif
#ifndef LORAWAN_APPKEY
#define LORAWAN_APPKEY {0}
#endif
#ifndef LORAWAN_REGION
#define LORAWAN_REGION RAK_REGION_EU868
#endif
#ifndef LORAWAN_FPORT
#define LORAWAN_FPORT 10
#endif
#ifndef VICTRON_MAC
#define VICTRON_MAC "00:00:00:00:00:00"
#endif
#ifndef VICTRON_ADDRESS_TYPE
#define VICTRON_ADDRESS_TYPE -1
#endif
#ifndef VICTRON_DEVICE_INSTANCE
#define VICTRON_DEVICE_INSTANCE 3
#endif
#ifndef VICTRON_USE_SMP_PIN
#define VICTRON_USE_SMP_PIN 1
#endif
#ifndef VICTRON_PIN
#define VICTRON_PIN "000000"
#endif
#ifndef STATUS_INTERVAL_MINUTES
#define STATUS_INTERVAL_MINUTES 15UL
#endif
#ifndef CONTACT_INPUT_PIN
#define CONTACT_INPUT_PIN WB_IO3
#endif
#ifndef CONTACT_ACTIVE_LEVEL
#define CONTACT_ACTIVE_LEVEL LOW
#endif
#ifndef CONTACT_DEBOUNCE_MS
#define CONTACT_DEBOUNCE_MS 80UL
#endif
#ifndef BLE_MAX_ATTEMPTS
#define BLE_MAX_ATTEMPTS 3
#endif
#ifndef RELAY_OUTPUT_PIN
#define RELAY_OUTPUT_PIN WB_IO4
#endif
