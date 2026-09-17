#pragma once

#include <Arduino.h>
#include "bluetooth_modes.h"
#include "bluetooth_functions.h"
#include "network_profiles.h"

// Runtime settings changed by the ESP web interface, including four OTAA keys.
// RUI keeps the legacy credential tuple for migration and global DevEUI edits.
// Network selection itself uses RAM; security counters have separate persistence.

struct RuntimeConfig
{
    char victronMac[18];
    int8_t victronAddressType;
    uint8_t victronDeviceInstance;
    uint8_t victronUseSmpPin;
    char victronPin[7];
    uint32_t configWindowSeconds;
    uint32_t statusIntervalMinutes;
    uint8_t bleMaxAttempts;
    uint8_t lorawanCredentialsFromPortal;
    char wifiApPassword[64];
    char wifiApSsid[33];
    uint8_t language; // 0 Dutch, 1 English
    uint8_t deviceProfile; // 1 SmartSolar MPPT, 2 Generic GATT, 3 Smart BatteryProtect
    uint8_t loadOutputEnabled;
    uint8_t loraRegion;
    uint8_t loraClass; // 0 A, 2 C
    uint8_t loraFport;
    uint8_t loraSubband; // 0 all, 1..8 US/AU915
    uint8_t rx2Custom;
    uint8_t rx2DataRate;
    uint32_t rx2Frequency;
    uint8_t adrEnabled;
    uint8_t inputEnabled;
    uint8_t relayEnabled;
    uint8_t risingActions;
    uint8_t fallingActions;
    uint32_t relayPulseMs;
    uint8_t ioBoard; // 0 none, 1 RAK13001 input+relay, 2 RAK13007 relay only
    uint8_t wifiTriggers; // bit 0 input edges, 1 hold while input active, 2 accepted downlink
    uint32_t wifiAfterInputSeconds, wifiAfterLoraSeconds;
    uint8_t downlinkAllowed; // bits 0/1 BLE functions, 2 relay OFF, 3 ON, 4 pulse
    BleFunction bleFunctions[MAX_BLE_FUNCTIONS];
    uint8_t functionCount,risingFunction,fallingFunction; // 1-based function IDs, 0=no edge function
    uint16_t downlinkFunctions; // Independent permissions for functions 1..10
    NetworkProfile networks[MAX_NETWORKS];
    uint8_t networkOrder[MAX_NETWORKS];
    uint8_t networksInitialized; // Old firmware migrates the existing RUI tuple once.
    uint16_t networkHealthMinutes; // 0 disabled; TTN is clamped to at least 240 min.
};

static const size_t RUNTIME_CONFIG_WIRE_SIZE = 1212;

struct PendingLorawanCredentials
{
    uint8_t active;
    uint8_t devEui[8];
    uint8_t joinEui[8];
    uint8_t appKey[16];
};

void runtimeConfigDefaults(RuntimeConfig &config);
bool runtimeConfigValid(const RuntimeConfig &config);

// Uses RUI's documented api.system.flash user partition. Two independent
// records and a CRC make interruption during a save recoverable.
bool runtimeConfigLoad(RuntimeConfig &config);
bool runtimeConfigSave(const RuntimeConfig &config);
uint32_t runtimeConfigRevision();

// Stage writes config + the complete target OTAA tuple into one new A/B
// record before any RUI credential setter is called. Commit writes the next
// generation without the tuple. A boot between those steps can safely reapply
// the staged tuple and commit it idempotently.
bool runtimeConfigStageLorawanUpdate(const RuntimeConfig &config,
                                     const uint8_t devEui[8],
                                     const uint8_t joinEui[8],
                                     const uint8_t appKey[16]);
bool runtimeConfigPendingLorawanUpdate(PendingLorawanCredentials &pending);
bool runtimeConfigCommitLorawanUpdate(const RuntimeConfig &config);

void runtimeConfigEncode(const RuntimeConfig &config,
                         uint8_t output[RUNTIME_CONFIG_WIRE_SIZE]);
bool runtimeConfigDecode(const uint8_t *input, size_t length,
                         RuntimeConfig &config);
