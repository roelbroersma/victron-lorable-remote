#pragma once

#include <Arduino.h>
#include "config_store.h"

enum CompanionCredentialMask : uint8_t
{
    COMPANION_SET_DEVEUI = 0x01,
    COMPANION_SET_JOINEUI = 0x02,
    COMPANION_SET_APPKEY = 0x04
};

struct CompanionConfigRequest
{
    uint16_t requestId;
    RuntimeConfig config;
    uint8_t credentialMask;
    uint8_t devEui[8];
    uint8_t joinEui[8];
    uint8_t appKey[16];
};

void companionBegin(const RuntimeConfig &config, uint32_t revision,
                    const uint8_t devEui[8], const uint8_t joinEui[8]);
void companionSetDemand(bool portalWanted, bool contactActive,
                        uint32_t secondsRemaining);
void companionService(uint32_t now);

// False means the active + four-waiting bounded queue is full.
bool companionRequestVictronLoad(uint8_t value);
bool companionScan();
void companionSendJson(uint16_t id, bool ok, const char *json);
bool companionTakeVictronResult(uint8_t &result, uint8_t &attempts,
                                uint8_t &loadValue);

bool companionTakeConfigRequest(CompanionConfigRequest &request);
void companionConfigResult(uint16_t requestId, bool ok, uint32_t revision,
                           const char *code);
void companionUpdateSnapshot(const RuntimeConfig &config, uint32_t revision,
                             const uint8_t devEui[8], const uint8_t joinEui[8]);
void companionWipeConfigRequest(CompanionConfigRequest &request);

bool companionIsReady();
bool companionIsPowered();
uint8_t companionState(); // 0 off, 1 boot, 2 portal, 3 BLE, 4 fault
