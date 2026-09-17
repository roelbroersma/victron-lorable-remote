#include "config_store.h"

#include "settings.h"
#include "region_profile.h"
#include "action_rules.h"
#include <board.h>
#include <ctype.h>
#include <string.h>

namespace
{
// RAKSystem.h documents 0x7800 bytes, while service_nvm.c in the STM32WLE
// 4.2.4 BSP enforces an effective 0x6800-byte bound. Put the records on two
// separate 0x800-byte flash pages and remain inside both limits.
static const uint32_t CONFIG_SLOT_A_OFFSET = 0x5800;
static const uint32_t CONFIG_SLOT_B_OFFSET = 0x6000;
static const uint32_t CONFIG_MAGIC = 0x34474643UL; // "CFG4" little endian
static const uint16_t LEGACY_FORMAT_VERSION_V1 = 1;
static const uint16_t LEGACY_FORMAT_VERSION_V2 = 2;
static const uint16_t CONFIG_FORMAT_VERSION = 8;
static const uint16_t V4_RUNTIME_CONFIG_WIRE_SIZE = 147;
static const uint16_t V3_RUNTIME_CONFIG_WIRE_SIZE = 133;
static const uint16_t LEGACY_RUNTIME_CONFIG_WIRE_SIZE = 100;
static const uint16_t LEGACY_RECORD_PAYLOAD_SIZE = 136;
static const uint16_t RECORD_PAYLOAD_SIZE = RUNTIME_CONFIG_WIRE_SIZE + 36;
static const uint16_t LEGACY_PENDING_OFFSET = LEGACY_RUNTIME_CONFIG_WIRE_SIZE;
static const uint16_t PENDING_OFFSET = RUNTIME_CONFIG_WIRE_SIZE;
static uint32_t activeGeneration = 0;
static PendingLorawanCredentials activePending;

struct __attribute__((packed)) StoredHeader
{
    uint32_t magic;
    uint16_t formatVersion;
    uint16_t payloadLength;
    uint32_t generation;
};

struct __attribute__((packed)) StoredConfigV1
{
    StoredHeader header;
    uint8_t payload[LEGACY_RUNTIME_CONFIG_WIRE_SIZE];
    uint32_t crc;
};

struct __attribute__((packed)) StoredConfigV2
{
    StoredHeader header;
    uint8_t payload[LEGACY_RECORD_PAYLOAD_SIZE];
    uint32_t crc;
};

struct __attribute__((packed)) StoredConfigV3
{
    StoredHeader header;
    uint8_t payload[168];
    uint32_t crc;
};

struct __attribute__((packed)) StoredConfigV4
{
    StoredHeader header;
    uint8_t payload[184];
    uint32_t crc;
};

struct __attribute__((packed)) StoredConfigV5 {
    StoredHeader header;
    uint8_t payload[188];
    uint32_t crc;
};
struct __attribute__((packed)) StoredConfigV6 {
    StoredHeader header;
    uint8_t payload[532];
    uint32_t crc;
};
struct __attribute__((packed)) StoredConfigV7 {
    StoredHeader header;
    uint8_t payload[997];
    uint32_t crc;
};
struct __attribute__((packed)) StoredConfigV8
{
    StoredHeader header;
    uint8_t payload[RECORD_PAYLOAD_SIZE];
    uint32_t crc;
};

static_assert(LEGACY_PENDING_OFFSET + 33 <= LEGACY_RECORD_PAYLOAD_SIZE,
              "Legacy pending credentials exceed the v2 payload");
static_assert(PENDING_OFFSET + 33 <= RECORD_PAYLOAD_SIZE,
              "Pending credentials exceed the v3 payload");
static_assert(sizeof(StoredConfigV8) <= 0x800,
              "Configuration record exceeds one flash page");

struct SlotData
{
    uint32_t generation;
    RuntimeConfig config;
    PendingLorawanCredentials pending;
};

static void secureZero(void *pointer, size_t length)
{
    volatile uint8_t *bytes = reinterpret_cast<volatile uint8_t *>(pointer);
    while (length-- > 0) *bytes++ = 0;
}

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static void putU32Le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint32_t getU32Le(const uint8_t *input)
{
    return (uint32_t)input[0] |
           ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) |
           ((uint32_t)input[3] << 24);
}

static bool isAllZero(const uint8_t *bytes, size_t length)
{
    uint8_t combined = 0;
    for (size_t i = 0; i < length; ++i) combined |= bytes[i];
    return combined == 0;
}

static bool isHexPairSeparatorMac(const char *mac)
{
    if (mac[17] != '\0') return false;
    for (uint8_t i = 0; i < 17; ++i)
    {
        if ((i % 3) == 2)
        {
            if (mac[i] != ':') return false;
        }
        else if (!isxdigit((unsigned char)mac[i]))
        {
            return false;
        }
    }
    return true;
}

static bool validPin(const char *pin)
{
    if (pin[6] != '\0') return false;
    for (uint8_t i = 0; i < 6; ++i)
    {
        if (!isdigit((unsigned char)pin[i])) return false;
    }
    return true;
}

static bool validWifiPassword(const char *password)
{
    const size_t length = strnlen(password, 64);
    if (length < 12 || length > 63) return false;
    for (size_t i = 0; i < length; ++i)
    {
        const uint8_t value = (uint8_t)password[i];
        if (value < 0x21 || value > 0x7E) return false;
    }
    return true;
}

static void clearPending(PendingLorawanCredentials &pending)
{
    secureZero(&pending, sizeof(pending));
}

static bool pendingValid(const PendingLorawanCredentials &pending)
{
    if (pending.active == 0) return true;
    return pending.active == 1 &&
           !isAllZero(pending.devEui, sizeof(pending.devEui)) &&
           !isAllZero(pending.appKey, sizeof(pending.appKey));
}

static bool encodePending(const PendingLorawanCredentials &pending,
                          uint8_t *payload, size_t payloadLength,
                          size_t pendingOffset)
{
    if (payload == nullptr || pendingOffset + 33 > payloadLength) return false;
    payload[pendingOffset] = pending.active;
    if (pending.active)
    {
        memcpy(&payload[pendingOffset + 1], pending.devEui, 8);
        memcpy(&payload[pendingOffset + 9], pending.joinEui, 8);
        memcpy(&payload[pendingOffset + 17], pending.appKey, 16);
    }
    return true;
}

static bool decodePending(const uint8_t *payload, size_t payloadLength,
                          size_t pendingOffset,
                          PendingLorawanCredentials &pending)
{
    clearPending(pending);
    if (payload == nullptr || pendingOffset + 33 > payloadLength) return false;
    pending.active = payload[pendingOffset];
    if (pending.active == 0) return true;
    if (pending.active != 1) return false;
    memcpy(pending.devEui, &payload[pendingOffset + 1], 8);
    memcpy(pending.joinEui, &payload[pendingOffset + 9], 8);
    memcpy(pending.appKey, &payload[pendingOffset + 17], 16);
    return pendingValid(pending);
}

static bool readSlot(uint32_t offset, SlotData &slot)
{
    secureZero(&slot, sizeof(slot));
    StoredHeader header;
    memset(&header, 0, sizeof(header));
    if (!api.system.flash.get(offset, reinterpret_cast<uint8_t *>(&header),
                              sizeof(header)) ||
        header.magic != CONFIG_MAGIC)
    {
        return false;
    }

    if (header.formatVersion == LEGACY_FORMAT_VERSION_V1 &&
        header.payloadLength == LEGACY_RUNTIME_CONFIG_WIRE_SIZE)
    {
        StoredConfigV1 record;
        memset(&record, 0, sizeof(record));
        if (!api.system.flash.get(offset, reinterpret_cast<uint8_t *>(&record),
                                  sizeof(record)))
            return false;
        const uint32_t expected = crc32(reinterpret_cast<const uint8_t *>(&record),
                                        sizeof(record) - sizeof(record.crc));
        const bool valid = record.crc == expected &&
            runtimeConfigDecode(record.payload, sizeof(record.payload), slot.config);
        if (valid)
        {
            slot.generation = record.header.generation;
            clearPending(slot.pending);
        }
        secureZero(&record, sizeof(record));
        return valid;
    }

    if (header.formatVersion == LEGACY_FORMAT_VERSION_V2 &&
        header.payloadLength == LEGACY_RECORD_PAYLOAD_SIZE)
    {
        StoredConfigV2 record;
        memset(&record, 0, sizeof(record));
        if (!api.system.flash.get(offset, reinterpret_cast<uint8_t *>(&record),
                                  sizeof(record)))
            return false;
        const uint32_t expected = crc32(reinterpret_cast<const uint8_t *>(&record),
                                        sizeof(record) - sizeof(record.crc));
        const bool valid = record.crc == expected &&
            runtimeConfigDecode(record.payload, LEGACY_RUNTIME_CONFIG_WIRE_SIZE,
                                slot.config) &&
            decodePending(record.payload, sizeof(record.payload),
                          LEGACY_PENDING_OFFSET, slot.pending);
        if (valid) slot.generation = record.header.generation;
        secureZero(&record, sizeof(record));
        return valid;
    }

    if (header.formatVersion == 3 && header.payloadLength == 168)
    {
        StoredConfigV3 oldRecord;
        if (!api.system.flash.get(offset, reinterpret_cast<uint8_t *>(&oldRecord), sizeof(oldRecord))) return false;
        const bool valid = oldRecord.crc == crc32(reinterpret_cast<const uint8_t *>(&oldRecord), sizeof(oldRecord)-4) &&
            runtimeConfigDecode(oldRecord.payload, V3_RUNTIME_CONFIG_WIRE_SIZE, slot.config) &&
            decodePending(oldRecord.payload, sizeof(oldRecord.payload), V3_RUNTIME_CONFIG_WIRE_SIZE, slot.pending);
        if (valid) slot.generation = oldRecord.header.generation;
        secureZero(&oldRecord, sizeof(oldRecord));
        return valid;
    }
    if (header.formatVersion == 4 && header.payloadLength == 184)
    {
        StoredConfigV4 oldRecord;
        if (!api.system.flash.get(offset, reinterpret_cast<uint8_t *>(&oldRecord), sizeof(oldRecord))) return false;
        const bool valid = oldRecord.crc == crc32(reinterpret_cast<const uint8_t *>(&oldRecord), sizeof(oldRecord)-4) &&
            runtimeConfigDecode(oldRecord.payload, V4_RUNTIME_CONFIG_WIRE_SIZE, slot.config) &&
            decodePending(oldRecord.payload, sizeof(oldRecord.payload), V4_RUNTIME_CONFIG_WIRE_SIZE, slot.pending);
        if (valid) slot.generation = oldRecord.header.generation;
        secureZero(&oldRecord, sizeof(oldRecord));
        return valid;
    }
    if (header.formatVersion == 5 && header.payloadLength == 188) {
        StoredConfigV5 old;
        if (!api.system.flash.get(offset, (uint8_t *)&old, sizeof(old))) return false;
        const bool valid = old.crc == crc32((const uint8_t *)&old, sizeof(old)-4) &&
            runtimeConfigDecode(old.payload, 155, slot.config) &&
            decodePending(old.payload, sizeof(old.payload), 155, slot.pending);
        if (valid) slot.generation = old.header.generation;
        secureZero(&old,sizeof(old));
        return valid;
    }
    if (header.formatVersion == 6 && header.payloadLength == 532) {
        StoredConfigV6 old;
        if (!api.system.flash.get(offset,(uint8_t *)&old,sizeof(old))) return false;
        const bool valid=old.crc==crc32((const uint8_t *)&old,sizeof(old)-4) &&
            runtimeConfigDecode(old.payload,496,slot.config) &&
            decodePending(old.payload,sizeof(old.payload),496,slot.pending);
        if(valid)slot.generation=old.header.generation;
        secureZero(&old,sizeof(old));return valid;
    }
    if (header.formatVersion == 7 && header.payloadLength == 997) {
        StoredConfigV7 old;
        if (!api.system.flash.get(offset,(uint8_t *)&old,sizeof(old))) return false;
        const bool valid=old.crc==crc32((const uint8_t *)&old,sizeof(old)-4) &&
            runtimeConfigDecode(old.payload,961,slot.config) &&
            decodePending(old.payload,sizeof(old.payload),961,slot.pending);
        if(valid)slot.generation=old.header.generation;
        secureZero(&old,sizeof(old));return valid;
    }
    if (header.formatVersion != CONFIG_FORMAT_VERSION ||
        header.payloadLength != RECORD_PAYLOAD_SIZE)
    {
        return false;
    }

    StoredConfigV8 record;
    memset(&record, 0, sizeof(record));
    if (!api.system.flash.get(offset, reinterpret_cast<uint8_t *>(&record),
                              sizeof(record)))
        return false;
    const uint32_t expected = crc32(reinterpret_cast<const uint8_t *>(&record),
                                    sizeof(record) - sizeof(record.crc));
    const bool valid = record.crc == expected &&
        runtimeConfigDecode(record.payload, RUNTIME_CONFIG_WIRE_SIZE,
                            slot.config) &&
        decodePending(record.payload, sizeof(record.payload),
                      PENDING_OFFSET, slot.pending);
    if (valid) slot.generation = record.header.generation;
    secureZero(&record, sizeof(record));
    return valid;
}

static bool generationNewer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

static bool flashEquals(uint32_t offset,const uint8_t *expected,size_t length) {
    uint8_t chunk[64];bool equal=true;
    for(size_t i=0;equal && i<length;i+=sizeof(chunk)) {
        const size_t n=length-i<sizeof(chunk)?length-i:sizeof(chunk);
        equal=api.system.flash.get(offset+i,chunk,n) && memcmp(chunk,expected+i,n)==0;
    }
    secureZero(chunk,sizeof(chunk));return equal;
}
static bool writeRecord(const RuntimeConfig &config,
                        const PendingLorawanCredentials &pending)
{
    if(!runtimeConfigValid(config)||!pendingValid(pending))return false;
    // Reuse one decoded scratch record. Ten functions must not create six
    // simultaneous full-record copies on the limited STM32 stack.
    SlotData scratch;
    const bool validA=readSlot(CONFIG_SLOT_A_OFFSET,scratch);
    const uint32_t genA=scratch.generation;
    const bool validB=readSlot(CONFIG_SLOT_B_OFFSET,scratch);
    const uint32_t genB=scratch.generation;
    secureZero(&scratch,sizeof(scratch));
    const bool latestA=validA && (!validB || generationNewer(genA,genB));
    const uint32_t latestOffset=latestA?CONFIG_SLOT_A_OFFSET:CONFIG_SLOT_B_OFFSET;
    const uint32_t targetOffset=!(validA||validB)?CONFIG_SLOT_A_OFFSET:
        latestA?CONFIG_SLOT_B_OFFSET:CONFIG_SLOT_A_OFFSET;
    const uint32_t latestGeneration=latestA?genA:genB;
    StoredConfigV8 record={};
    record.header.magic=CONFIG_MAGIC;
    record.header.formatVersion=CONFIG_FORMAT_VERSION;
    record.header.payloadLength=RECORD_PAYLOAD_SIZE;
    record.header.generation=(validA||validB)?latestGeneration+1:1;
    runtimeConfigEncode(config,record.payload);
    if(!encodePending(pending,record.payload,sizeof(record.payload),PENDING_OFFSET)) {
        secureZero(&record,sizeof(record));return false;
    }
    StoredHeader latestHeader={};
    if((validA||validB) && api.system.flash.get(latestOffset,(uint8_t *)&latestHeader,sizeof(latestHeader)) &&
       latestHeader.formatVersion==CONFIG_FORMAT_VERSION && latestHeader.payloadLength==RECORD_PAYLOAD_SIZE &&
       flashEquals(latestOffset+sizeof(StoredHeader),record.payload,sizeof(record.payload))) {
        activeGeneration=latestGeneration;activePending=pending;
        secureZero(&record,sizeof(record));return true;
    }
    record.crc=crc32((const uint8_t *)&record,sizeof(record)-4);
    bool ok=api.system.flash.set(targetOffset,(uint8_t *)&record,sizeof(record)) &&
        flashEquals(targetOffset,(const uint8_t *)&record,sizeof(record)) &&
        readSlot(targetOffset,scratch) && scratch.generation==record.header.generation;
    if(ok){activeGeneration=record.header.generation;activePending=pending;}
    secureZero(&scratch,sizeof(scratch));secureZero(&record,sizeof(record));return ok;
}
} // namespace

void runtimeConfigDefaults(RuntimeConfig &config)
{
    memset(&config, 0, sizeof(config));
    strncpy(config.victronMac, VICTRON_MAC, sizeof(config.victronMac) - 1);
    config.victronAddressType = VICTRON_ADDRESS_TYPE;
    config.victronDeviceInstance = VICTRON_DEVICE_INSTANCE;
    config.victronUseSmpPin = 1; // Connected Victron GATT requires PIN pairing.
    strncpy(config.victronPin, VICTRON_PIN, sizeof(config.victronPin) - 1);
    config.configWindowSeconds = CONFIG_WINDOW_SECONDS;
    config.statusIntervalMinutes = STATUS_INTERVAL_MINUTES;
    config.bleMaxAttempts = BLE_MAX_ATTEMPTS;
    config.lorawanCredentialsFromPortal = 0;
    strncpy(config.wifiApPassword, WIFI_AP_PASSWORD,
            sizeof(config.wifiApPassword) - 1);
    strncpy(config.wifiApSsid, WIFI_AP_SSID,
            sizeof(config.wifiApSsid) - 1);
    config.language = 0;
    config.deviceProfile = 1;
    config.ioBoard = 1;
    config.wifiTriggers = 3;
    config.wifiAfterInputSeconds = 3600;
    config.wifiAfterLoraSeconds = 3600;
    config.downlinkAllowed = 31; // Preserve v4.6 routes on migration.
    config.functionCount=2;config.risingFunction=1;config.fallingFunction=0;config.downlinkFunctions=3;
    for(unsigned i=0;i<MAX_BLE_FUNCTIONS;++i) snprintf(config.bleFunctions[i].name,25,"Function %u",i+1);
    strcpy(config.bleFunctions[0].name,"LOAD ON");
    strcpy(config.bleFunctions[1].name,"LOAD OFF");
    config.bleFunctions[0].kind = 1;
    config.bleFunctions[1].kind = 2;
    config.loadOutputEnabled = 1;
    config.loraRegion = LORAWAN_REGION;
    config.loraClass = 0;
    config.loraFport = LORAWAN_FPORT;
    config.adrEnabled = 1;
    config.rx2Frequency = 869525000UL;
    config.inputEnabled = 1;
    config.relayEnabled = 0; // Enabling the output is an explicit choice.
    config.risingActions = ACTION_UPLINK | ACTION_LOAD_ON;
    config.fallingActions = ACTION_UPLINK;
    config.relayPulseMs = 1000;
    config.networkHealthMinutes=240;
    for(unsigned i=0;i<MAX_NETWORKS;++i) {
        config.networkOrder[i]=i;
        NetworkProfile &p=config.networks[i];
        snprintf(p.name,sizeof(p.name),"Network %u",i+1);
        p.preemptMinutes=1440;p.rx2Frequency=869525000;
    }
}

bool runtimeConfigValid(const RuntimeConfig &config)
{
    if(config.networksInitialized>1 || (config.networkHealthMinutes &&
       (config.networkHealthMinutes<15 || config.networkHealthMinutes>1440)))return false;
    uint8_t seen=0;
    for(unsigned i=0;i<MAX_NETWORKS;++i) {
        const uint8_t slot=config.networkOrder[i];
        if(slot>=MAX_NETWORKS || (seen&(1u<<slot)))return false;seen|=1u<<slot;
        const NetworkProfile &p=config.networks[i];const size_t n=strnlen(p.name,25);
        if(!n||n>24||p.enabled>1||p.kind>1||p.rx2Custom>1||p.preemptMinutes<15||p.preemptMinutes>10080)return false;
        for(size_t j=0;j<n;++j)if((uint8_t)p.name[j]<32||(uint8_t)p.name[j]>126)return false;
        if(p.rx2Custom&&!validRx2(config.loraRegion,p.rx2Frequency,p.rx2DataRate))return false;
        if(p.enabled&&!networkHasKey(p))return false;
        // Two active profiles must not be indistinguishable to a join server.
        for(unsigned j=0;j<i;++j)if(p.enabled&&config.networks[j].enabled&&
          !memcmp(p.joinEui,config.networks[j].joinEui,8))return false;
    }
    if(config.ioBoard>2 || (config.inputEnabled && config.ioBoard!=1) ||
       (config.relayEnabled && config.ioBoard==0) || config.wifiTriggers>7 ||
       config.downlinkAllowed>31 || config.wifiAfterInputSeconds<60 ||
       config.wifiAfterInputSeconds>14400 || config.wifiAfterLoraSeconds<60 ||
       config.wifiAfterLoraSeconds>14400) return false;
    if(config.functionCount<1 || config.functionCount>MAX_BLE_FUNCTIONS ||
       config.risingFunction>config.functionCount || config.fallingFunction>config.functionCount ||
       config.downlinkFunctions >= (1u<<config.functionCount)) return false;
    for(unsigned i=0;i<MAX_BLE_FUNCTIONS;++i) {
        const BleFunction &f=config.bleFunctions[i];
        const size_t nameLen=strnlen(f.name,sizeof(f.name));
        if(!nameLen || nameLen>24 || f.kind>12) return false;
        for(size_t j=0;j<nameLen;++j) if((uint8_t)f.name[j]<32 || (uint8_t)f.name[j]>126) return false;
        if((config.deviceProfile==1 && f.kind && smartMpptMode(f.kind)==255) ||
           (config.deviceProfile==2 && f.kind && !genericGattKind(f.kind)) ||
           (config.deviceProfile==3 && f.kind && smartBatteryProtectMode(f.kind)==255)) return false;
        if(i>=config.functionCount && f.kind) return false;
        if(f.valueLength>20 || (genericGattKind(f.kind) && (!f.valueLength ||
           isAllZero(f.service,16) || isAllZero(f.characteristic,16)))) return false;
    }
    if (config.inputEnabled > 1 || config.relayEnabled > 1 ||
        !inputActionsValid(config.risingActions) || !inputActionsValid(config.fallingActions) ||
        config.relayPulseMs < 100 || config.relayPulseMs > 60000) return false;
    if (config.language > 1 || config.deviceProfile < 1 || config.deviceProfile > 3 || config.loadOutputEnabled > 1) return false;
    if (!regionProfile(config.loraRegion) || (config.loraClass != 0 && config.loraClass != 2)) return false;
    if (config.loraFport < 1 || config.loraFport > 223 || config.loraSubband > 8) return false;
    if (config.loraSubband && config.loraRegion != 5 && config.loraRegion != 6) return false;
    if (config.rx2Custom > 1 || config.adrEnabled > 1) return false;
    if (config.rx2Custom && !validRx2(config.loraRegion, config.rx2Frequency, config.rx2DataRate)) return false;
    if (!isHexPairSeparatorMac(config.victronMac)) return false;
    if (config.victronAddressType < -1 || config.victronAddressType > 1) return false;
    if (config.victronDeviceInstance > 23) return false;
    if (config.victronUseSmpPin > 1 || (config.deviceProfile != 2 && config.victronUseSmpPin != 1) || !validPin(config.victronPin)) return false;
    if (config.configWindowSeconds < 300UL || config.configWindowSeconds > 14400UL) return false;
    if (config.statusIntervalMinutes < 1UL || config.statusIntervalMinutes > 1440UL) return false;
    if (config.bleMaxAttempts < 1 || config.bleMaxAttempts > 3) return false;
    if (config.lorawanCredentialsFromPortal > 1) return false;
    if (!validWifiPassword(config.wifiApPassword)) return false;
    const size_t ssidLength = strnlen(config.wifiApSsid,
                                      sizeof(config.wifiApSsid));
    if (ssidLength < 1 || ssidLength > 32) return false;
    for (size_t i = 0; i < ssidLength; ++i)
    {
        const uint8_t value = (uint8_t)config.wifiApSsid[i];
        if (value < 0x20 || value > 0x7E) return false;
    }
    return true;
}

void runtimeConfigEncode(const RuntimeConfig &config,
                         uint8_t output[RUNTIME_CONFIG_WIRE_SIZE])
{
    memset(output, 0, RUNTIME_CONFIG_WIRE_SIZE);
    memcpy(&output[0], config.victronMac, 17);
    output[17] = (uint8_t)config.victronAddressType;
    output[18] = config.victronDeviceInstance;
    output[19] = config.victronUseSmpPin;
    memcpy(&output[20], config.victronPin, 6);
    putU32Le(&output[26], config.configWindowSeconds);
    putU32Le(&output[30], config.statusIntervalMinutes);
    output[34] = config.bleMaxAttempts;
    output[35] = config.lorawanCredentialsFromPortal;
    const size_t passwordLength = strnlen(config.wifiApPassword, 63);
    output[36] = (uint8_t)passwordLength;
    memcpy(&output[37], config.wifiApPassword, passwordLength);
    const size_t ssidLength = strnlen(config.wifiApSsid, 32);
    output[100] = (uint8_t)ssidLength;
    memcpy(&output[101], config.wifiApSsid, ssidLength);
    output[133] = config.language;
    output[134] = config.deviceProfile;
    output[135] = config.loadOutputEnabled;
    output[136] = config.loraRegion;
    output[137] = config.loraClass;
    output[138] = config.loraFport;
    output[139] = config.loraSubband;
    output[140] = config.rx2Custom;
    output[141] = config.rx2DataRate;
    putU32Le(&output[142], config.rx2Frequency);
    output[146] = config.adrEnabled;
    output[147] = config.inputEnabled;
    output[148] = config.relayEnabled;
    output[149] = config.risingActions;
    output[150] = config.fallingActions;
    putU32Le(&output[151], config.relayPulseMs);
    output[155]=config.ioBoard; output[156]=config.wifiTriggers;
    putU32Le(output+157,config.wifiAfterInputSeconds);
    putU32Le(output+161,config.wifiAfterLoraSeconds);
    output[165]=config.downlinkAllowed;
    for(unsigned i=0;i<MAX_BLE_FUNCTIONS;++i) {
        const BleFunction &f=config.bleFunctions[i];uint8_t *p=output+166+i*79;
        memcpy(p,f.name,24);memcpy(p+25,f.service,16);memcpy(p+41,f.characteristic,16);
        memcpy(p+57,f.value,f.valueLength);p[77]=f.valueLength;p[78]=f.kind;
    }
    output[956]=config.functionCount;output[957]=config.risingFunction;output[958]=config.fallingFunction;
    output[959]=(uint8_t)config.downlinkFunctions;output[960]=(uint8_t)(config.downlinkFunctions>>8);
    for(unsigned i=0;i<MAX_NETWORKS;++i) {
        const NetworkProfile &n=config.networks[i];uint8_t *p=output+961+i*NETWORK_WIRE_SIZE;
        memcpy(p,n.name,24);p[25]=n.enabled;p[26]=n.kind;
        memcpy(p+27,n.joinEui,8);memcpy(p+35,n.appKey,16);
        p[51]=n.rx2Custom;p[52]=n.rx2DataRate;putU32Le(p+53,n.rx2Frequency);putU32Le(p+57,n.preemptMinutes);
    }
    memcpy(output+1205,config.networkOrder,4);output[1209]=config.networksInitialized;
    output[1210]=(uint8_t)config.networkHealthMinutes;output[1211]=(uint8_t)(config.networkHealthMinutes>>8);
}

bool runtimeConfigDecode(const uint8_t *input, size_t length,
                         RuntimeConfig &config)
{
    if (input == nullptr ||
        (length != LEGACY_RUNTIME_CONFIG_WIRE_SIZE &&
         length != V3_RUNTIME_CONFIG_WIRE_SIZE &&
         length != V4_RUNTIME_CONFIG_WIRE_SIZE && length != 155 &&
         length != 496 && length != 961 && length != RUNTIME_CONFIG_WIRE_SIZE)) return false;
    runtimeConfigDefaults(config);
    memcpy(config.victronMac, &input[0], 17);
    config.victronMac[17] = '\0';
    config.victronAddressType = (int8_t)input[17];
    config.victronDeviceInstance = input[18];
    if (input[19] > 1) return false;
    config.victronUseSmpPin = length < 496 ? 1 : input[19];
    memcpy(config.victronPin, &input[20], 6);
    config.victronPin[6] = '\0';
    config.configWindowSeconds = getU32Le(&input[26]);
    config.statusIntervalMinutes = getU32Le(&input[30]);
    config.bleMaxAttempts = input[34];
    config.lorawanCredentialsFromPortal = input[35];
    const uint8_t passwordLength = input[36];
    if (passwordLength < 12 || passwordLength > 63) return false;
    memcpy(config.wifiApPassword, &input[37], passwordLength);
    config.wifiApPassword[passwordLength] = '\0';
    if (length == LEGACY_RUNTIME_CONFIG_WIRE_SIZE)
    {
        strncpy(config.wifiApSsid, WIFI_AP_SSID,
                sizeof(config.wifiApSsid) - 1);
    }
    else
    {
        const uint8_t ssidLength = input[100];
        if (ssidLength < 1 || ssidLength > 32) return false;
        memcpy(config.wifiApSsid, &input[101], ssidLength);
        config.wifiApSsid[ssidLength] = '\0';
    }
    if (length >= V4_RUNTIME_CONFIG_WIRE_SIZE)
    {
        config.language = input[133];
        config.deviceProfile = input[134];
        config.loadOutputEnabled = input[135];
        config.loraRegion = input[136];
        config.loraClass = input[137];
        config.loraFport = input[138];
        config.loraSubband = input[139];
        config.rx2Custom = input[140];
        config.rx2DataRate = input[141];
        config.rx2Frequency = getU32Le(&input[142]);
        config.adrEnabled = input[146];
    }
    if (length >= 155)
    {
        config.inputEnabled = input[147];
        config.relayEnabled = input[148];
        config.risingActions = input[149];
        config.fallingActions = input[150];
        config.relayPulseMs = getU32Le(&input[151]);
    }
    if(length >= 496) {
        config.ioBoard=input[155];config.wifiTriggers=input[156];
        config.wifiAfterInputSeconds=getU32Le(input+157);
        config.wifiAfterLoraSeconds=getU32Le(input+161);
        config.downlinkAllowed=input[165];
        if(length==496) for(unsigned i=0;i<2;++i) {
            const uint8_t *p=input+166+i*165;BleFunction &f=config.bleFunctions[i];
            if(p[24]||p[61]||p[98]||p[163]) return false;
            memcpy(f.name,p,25);f.kind=p[164];
            if(!functionSetText(f,(const char *)p+25,(const char *)p+62,(const char *)p+99)) return false;
        }
    }
    if(length>=961) {
        config.functionCount=input[956];config.risingFunction=input[957];config.fallingFunction=input[958];
        config.downlinkFunctions=input[959]|((uint16_t)input[960]<<8);
        for(unsigned i=0;i<MAX_BLE_FUNCTIONS;++i) {
            const uint8_t *p=input+166+i*79;BleFunction &f=config.bleFunctions[i];
            memcpy(f.name,p,25);memcpy(f.service,p+25,16);memcpy(f.characteristic,p+41,16);
            memcpy(f.value,p+57,20);f.valueLength=p[77];f.kind=p[78];if(f.name[24])return false;
        }
    } else {
        config.risingFunction=(config.risingActions&4)?1:(config.risingActions&8)?2:0;
        config.fallingFunction=(config.fallingActions&4)?1:(config.fallingActions&8)?2:0;
        config.downlinkFunctions=config.downlinkAllowed&3;
    }
    if(length==RUNTIME_CONFIG_WIRE_SIZE) {
        for(unsigned i=0;i<MAX_NETWORKS;++i) {
            NetworkProfile &n=config.networks[i];const uint8_t *p=input+961+i*NETWORK_WIRE_SIZE;
            if(p[24])return false;memcpy(n.name,p,25);n.enabled=p[25];n.kind=p[26];
            memcpy(n.joinEui,p+27,8);memcpy(n.appKey,p+35,16);
            n.rx2Custom=p[51];n.rx2DataRate=p[52];n.rx2Frequency=getU32Le(p+53);n.preemptMinutes=getU32Le(p+57);
        }
        memcpy(config.networkOrder,input+1205,4);config.networksInitialized=input[1209];
        config.networkHealthMinutes=input[1210]|((uint16_t)input[1211]<<8);
    }
    return runtimeConfigValid(config);
}

bool runtimeConfigLoad(RuntimeConfig &config)
{
    SlotData slotA;
    SlotData slotB;
    const bool validA = readSlot(CONFIG_SLOT_A_OFFSET, slotA);
    const bool validB = readSlot(CONFIG_SLOT_B_OFFSET, slotB);

    if (!validA && !validB)
    {
        activeGeneration = 0;
        clearPending(activePending);
        runtimeConfigDefaults(config);
        secureZero(&slotA, sizeof(slotA));
        secureZero(&slotB, sizeof(slotB));
        return false;
    }

    const SlotData &selected =
        (validA && (!validB || generationNewer(slotA.generation, slotB.generation)))
            ? slotA : slotB;
    config = selected.config;
    activeGeneration = selected.generation;
    activePending = selected.pending;
    secureZero(&slotA, sizeof(slotA));
    secureZero(&slotB, sizeof(slotB));
    return true;
}

bool runtimeConfigSave(const RuntimeConfig &config)
{
    return writeRecord(config, activePending);
}

uint32_t runtimeConfigRevision()
{
    return activeGeneration;
}

bool runtimeConfigStageLorawanUpdate(const RuntimeConfig &config,
                                     const uint8_t devEui[8],
                                     const uint8_t joinEui[8],
                                     const uint8_t appKey[16])
{
    PendingLorawanCredentials pending;
    clearPending(pending);
    pending.active = 1;
    memcpy(pending.devEui, devEui, sizeof(pending.devEui));
    memcpy(pending.joinEui, joinEui, sizeof(pending.joinEui));
    memcpy(pending.appKey, appKey, sizeof(pending.appKey));
    const bool saved = writeRecord(config, pending);
    clearPending(pending);
    return saved;
}

bool runtimeConfigPendingLorawanUpdate(PendingLorawanCredentials &pending)
{
    pending = activePending;
    return activePending.active == 1;
}

bool runtimeConfigCommitLorawanUpdate(const RuntimeConfig &config)
{
    if (!activePending.active) return true;
    PendingLorawanCredentials cleared;
    clearPending(cleared);
    return writeRecord(config, cleared);
}
