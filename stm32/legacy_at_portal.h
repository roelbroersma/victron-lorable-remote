#pragma once

#include <Arduino.h>
#include "settings.h"
#include "esp_companion.h"



// The portal runs on the stock ESP-AT firmware. The STM32 owns the HTTP
// protocol and durable settings; the ESP8684 only supplies SoftAP + TCP.
bool legacyPortalStart(const RuntimeConfig &config,
                       const uint8_t devEui[8],
                       const uint8_t joinEui[8]);
void legacyPortalStop();
void legacyPortalPause();
void legacyPortalResume();
void legacyPortalService();
bool legacyPortalIsRunning();
struct PortalLiveStatus {
    bool bleAvailable, joined, blePending, inputActive, relayOn, relayPulsing;
    uint8_t bleResult, lastEvent, lastActions, loadValue, bleAttempts;
    uint32_t txCount, risingCount, fallingCount, relayChangedAt, inputOverflow;
};
void legacyPortalSetStatus(const PortalLiveStatus &status);
bool legacyPortalTakeAction(uint8_t &action);

bool legacyPortalTakeConfigRequest(CompanionConfigRequest &request);
void legacyPortalConfigResult(uint16_t requestId, bool ok,
                              uint32_t revision, const char *code);
void legacyPortalUpdateSnapshot(const RuntimeConfig &config,
                                const uint8_t devEui[8],
                                const uint8_t joinEui[8]);

#if !LEGACY_BLE_AT
void portalNativeFrame(const char *type, uint16_t id, const char *payload);
#endif
