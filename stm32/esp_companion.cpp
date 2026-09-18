#include "esp_companion.h"

#include "settings.h"
#include "victron_result_codes.h"
#include "legacy_at_portal.h"
#include "firmware_update.h"
#include "update_bundle.h"

#if !LEGACY_BLE_AT

#include <board.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <service_nvm.h>
#include <service_mode_cli.h>
#include <udrv_delay.h>
#include <stm32wlxx_hal.h>
// RUI 4.2.4 RAM configuration. A bounded USB exchange must neither reinitialize
// shared UART hardware nor write a mode change to flash for every poll.
extern "C" PRE_rui_cfg_t g_rui_cfg_t;

namespace
{
static const uint16_t MAX_FRAME = 768;
static const uint16_t MAX_DECODED_PAYLOAD = 384;
static const uint16_t MAX_B64_PAYLOAD = 512;
static const uint32_t ESP_BOOT_WAIT_MS = 1500UL;
static const uint32_t HELLO_RETRY_MS = 2000UL;
static const uint8_t HELLO_MAX_ATTEMPTS = 5;
static const uint32_t ACTIVE_PORTAL_REFRESH_MS = 12UL * 60UL * 60UL * 1000UL;
static const uint32_t REQUEST_RETRY_MS = 5000UL;
static const uint8_t REQUEST_MAX_ATTEMPTS = 3;
static const uint8_t MAX_VICTRON_LINK_FAILURES = 3;
static const uint32_t VICTRON_PRE_DELIVERY_TIMEOUT_MS = 60000UL;
static const uint32_t VICTRON_DELIVERY_RETRY_MS = 5000UL;
static const uint32_t VICTRON_DELIVERY_TIMEOUT_MS = 30000UL;
static const uint32_t VICTRON_REPLAY_RETRY_MS = 10000UL;
static const uint32_t VICTRON_JOB_TIMEOUT_MS = 210000UL;
static const uint8_t MAX_QUEUED_VICTRON_JOBS = 4;
static const uint32_t PORTAL_RETRY_MAX_MS = 60000UL;

static char rxLine[MAX_FRAME + 1];
static uint16_t rxLineLength = 0;
static bool rxLineOverflow = false;
static uint8_t rxPayload[MAX_DECODED_PAYLOAD + 1];

static char txPayload[MAX_DECODED_PAYLOAD + 1];
static char txBase64[MAX_B64_PAYLOAD + 1];
static char txFrame[MAX_FRAME + 1];

#ifdef ESP_RECOVERY_SSID
static RuntimeConfig recoveryConfig;
#endif
static const RuntimeConfig *activeConfigPointer;
#define activeConfig (*activeConfigPointer)
static uint32_t activeRevision = 0;
static uint8_t activeDevEui[8];
static uint8_t activeJoinEui[8];

static bool espPowered = false;
static bool linkReady = false;
static bool readySeen = false;
static bool linkFault = false;
static uint32_t nextPowerAttemptAt = 0;
static uint32_t nextHelloAt = 0;
static uint8_t helloAttempts = 0;
static uint16_t helloId = 0;
static uint16_t nextRequestId = 1;

static bool desiredPortal = false;
static bool desiredContact = false;
static uint32_t desiredSecondsRemaining = 0;
static bool portalRunning = false;
static bool portalDirty = true;
static bool contactDirty = true;
static bool snapshotDirty = true;
static uint32_t lastPortalRefreshAt = 0;
static bool portalRequestPending = false;
static bool portalRequestWasStart = false;
static uint16_t portalRequestId = 0;
static uint8_t portalRequestAttempts = 0;
static uint32_t portalRequestSentAt = 0;
static uint8_t portalFailureStreak = 0;
static uint32_t nextPortalRetryAt = 0;

static bool victronPending = false;
static uint8_t desiredLoadValue = 0;
static uint32_t otaHoldUntil=0;
static uint32_t updateSize,updateCrc,updateMode,updatePreparedAt;
static volatile bool usbRequested;
static bool usbAwaiting,usbReady;
static volatile bool usbRawActive;
static uint32_t usbRequestedAt;
static uint32_t usbWakeUntil;
static uint8_t usbProtocol=1;
static int usbCommand(SERIAL_PORT port,char *cmd,stParam *param){
 (void)port;(void)cmd;
 if(param->argc!=1 || (strcmp(param->argv[0],"1")&&strcmp(param->argv[0],"2")) || usbRequested || usbAwaiting || victronPending)return AT_PARAM_ERROR;
 usbProtocol=param->argv[0][0]-'0';
 usbRequestedAt=millis();usbWakeUntil=usbRequestedAt+600000UL;
 usbRequested=true;return AT_OK;
}
static uint32_t scanHoldUntil=0;
static uint8_t victronQueuedCount = 0;
static uint32_t victronActivatedAt = 0;
static bool victronRequestPending = false;
static uint16_t victronRequestId = 0;
static uint32_t victronRequestSentAt = 0;
static uint32_t victronDeliveryStartedAt = 0;
static bool victronDeliveryStarted = false;
static uint32_t victronJobStartedAt = 0;
static bool victronAccepted = false;
static bool victronEverAccepted = false;
static uint8_t victronLinkFailures = 0;
static bool victronResultReady = false;
static uint8_t victronResult = 0;
static uint8_t victronAttempts = 0;
static uint8_t victronLoadValue = 0xFF;

static bool configRequestPending = false;
static CompanionConfigRequest pendingConfigRequest;
static bool lastConfigResultValid = false;
static uint16_t lastConfigResultId = 0;
static bool lastConfigResultOk = false;
static uint32_t lastConfigResultRevision = 0;
static char lastConfigResultCode[25];

static void secureZero(void *pointer, size_t length)
{
    volatile uint8_t *bytes = reinterpret_cast<volatile uint8_t *>(pointer);
    while (length-- > 0) *bytes++ = 0;
}

static void resetConfigSessionState() {}

static bool timeReached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void resetPortalRetry(uint32_t now)
{
    portalFailureStreak = 0;
    nextPortalRetryAt = now;
}

static void schedulePortalRetry(uint32_t now)
{
    if (portalFailureStreak < 7) ++portalFailureStreak;
    uint32_t delayMs = 1000UL << (portalFailureStreak - 1);
    if (delayMs > PORTAL_RETRY_MAX_MS) delayMs = PORTAL_RETRY_MAX_MS;
    nextPortalRetryAt = now + delayMs;
    portalDirty = true;
}

static uint16_t crc16CcittFalse(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static int hexValue(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool parseHex16(const char *text, uint16_t &value)
{
    if (text == nullptr || strlen(text) != 4) return false;
    value = 0;
    for (uint8_t i = 0; i < 4; ++i)
    {
        const int nibble = hexValue(text[i]);
        if (nibble < 0) return false;
        value = (uint16_t)((value << 4) | nibble);
    }
    return true;
}

static const char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64Encode(const uint8_t *input, size_t length,
                           char *output, size_t capacity)
{
    const size_t required = ((length + 2) / 3) * 4;
    if (capacity <= required) return 0;
    size_t in = 0;
    size_t out = 0;
    while (in < length)
    {
        const uint32_t a = input[in++];
        const bool haveB = in < length;
        const uint32_t b = haveB ? input[in++] : 0;
        const bool haveC = in < length;
        const uint32_t c = haveC ? input[in++] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        output[out++] = BASE64_ALPHABET[(triple >> 18) & 0x3F];
        output[out++] = BASE64_ALPHABET[(triple >> 12) & 0x3F];
        output[out++] = haveB ? BASE64_ALPHABET[(triple >> 6) & 0x3F] : '=';
        output[out++] = haveC ? BASE64_ALPHABET[triple & 0x3F] : '=';
    }
    output[out] = '\0';
    return out;
}

static int base64Index(char value)
{
    const char *found = strchr(BASE64_ALPHABET, value);
    return found == nullptr ? -1 : (int)(found - BASE64_ALPHABET);
}

static bool base64Decode(const char *input, uint8_t *output,
                         size_t capacity, size_t &outputLength)
{
    outputLength = 0;
    const size_t length = strlen(input);
    if ((length % 4) != 0) return false;
    for (size_t i = 0; i < length; i += 4)
    {
        const bool lastGroup = i + 4 == length;
        const int a = base64Index(input[i]);
        const int b = base64Index(input[i + 1]);
        const int c = input[i + 2] == '=' ? -2 : base64Index(input[i + 2]);
        const int d = input[i + 3] == '=' ? -2 : base64Index(input[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1) return false;
        if ((c == -2 && d != -2) || (!lastGroup && (c == -2 || d == -2))) return false;

        const uint32_t triple = ((uint32_t)a << 18) |
                                ((uint32_t)b << 12) |
                                ((uint32_t)(c < 0 ? 0 : c) << 6) |
                                (uint32_t)(d < 0 ? 0 : d);
        if (outputLength >= capacity) return false;
        output[outputLength++] = (uint8_t)(triple >> 16);
        if (c != -2)
        {
            if (outputLength >= capacity) return false;
            output[outputLength++] = (uint8_t)(triple >> 8);
        }
        if (d != -2)
        {
            if (outputLength >= capacity) return false;
            output[outputLength++] = (uint8_t)triple;
        }
    }
    return true;
}

static bool validType(const char *type)
{
    const size_t length = strlen(type);
    if (length < 1 || length > 20) return false;
    for (size_t i = 0; i < length; ++i)
    {
        if (!(type[i] == '_' || (type[i] >= 'A' && type[i] <= 'Z') ||
              (type[i] >= '0' && type[i] <= '9')))
        {
            return false;
        }
    }
    return true;
}

static bool urlSafe(uint8_t value)
{
    return isalnum(value) || value == '-' || value == '_' ||
           value == '.' || value == '~';
}

static bool appendRaw(char *buffer, size_t capacity, size_t &length,
                      const char *text)
{
    const size_t add = strlen(text);
    if (length + add >= capacity) return false;
    memcpy(&buffer[length], text, add);
    length += add;
    buffer[length] = '\0';
    return true;
}

static bool appendEncoded(char *buffer, size_t capacity, size_t &length,
                          const char *value)
{
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    for (size_t i = 0; value[i] != '\0'; ++i)
    {
        const uint8_t byte = (uint8_t)value[i];
        if (urlSafe(byte))
        {
            if (length + 1 >= capacity) return false;
            buffer[length++] = (char)byte;
        }
        else
        {
            if (length + 3 >= capacity) return false;
            buffer[length++] = '%';
            buffer[length++] = HEX_DIGITS[byte >> 4];
            buffer[length++] = HEX_DIGITS[byte & 0x0F];
        }
    }
    buffer[length] = '\0';
    return true;
}

static bool appendField(char *buffer, size_t capacity, size_t &length,
                        const char *key, const char *value)
{
    if (length != 0 && !appendRaw(buffer, capacity, length, "&")) return false;
    return appendRaw(buffer, capacity, length, key) &&
           appendRaw(buffer, capacity, length, "=") &&
           appendEncoded(buffer, capacity, length, value);
}

static bool appendUnsigned(char *buffer, size_t capacity, size_t &length,
                           const char *key, uint32_t value)
{
    char number[12];
    snprintf(number, sizeof(number), "%lu", (unsigned long)value);
    return appendField(buffer, capacity, length, key, number);
}

static bool sendFrame(const char *type, uint16_t id, const char *payload)
{
    if (!validType(type) || payload == nullptr) return false;
    const size_t payloadLength = strlen(payload);
    if (payloadLength > MAX_DECODED_PAYLOAD) return false;
    const size_t encodedLength = base64Encode(
        reinterpret_cast<const uint8_t *>(payload), payloadLength,
        txBase64, sizeof(txBase64));
    if (payloadLength != 0 && encodedLength == 0) return false;

    const int bodyLength = snprintf(txFrame, sizeof(txFrame),
                                    "@1|%s|%04X|%s", type, id, txBase64);
    if (bodyLength < 0 || (size_t)bodyLength + 7 >= sizeof(txFrame))
    {
        secureZero(txBase64, sizeof(txBase64));
        return false;
    }
    const uint16_t crc = crc16CcittFalse(
        reinterpret_cast<const uint8_t *>(&txFrame[1]), bodyLength - 1);
    const int suffix = snprintf(&txFrame[bodyLength], sizeof(txFrame) - bodyLength,
                                "|%04X\r\n", crc);
    const bool valid = suffix == 7;
    if (valid)
    {
        Serial1.write(reinterpret_cast<const uint8_t *>(txFrame),
                      (size_t)bodyLength + (size_t)suffix);
        Serial1.flush();
    }
    secureZero(txBase64, sizeof(txBase64));
    secureZero(txFrame, sizeof(txFrame));
    return valid;
}

static uint16_t allocateRequestId()
{
    if (nextRequestId == 0 || nextRequestId >= 0x8000) nextRequestId = 1;
    return nextRequestId++;
}

static bool urlDecodeInPlace(char *text)
{
    char *read = text;
    char *write = text;
    while (*read != '\0')
    {
        uint8_t value = (uint8_t)*read++;
        if (value == '+')
        {
            value = ' ';
        }
        else if (value == '%')
        {
            if (read[0] == '\0' || read[1] == '\0') return false;
            const int high = hexValue(read[0]);
            const int low = hexValue(read[1]);
            if (high < 0 || low < 0) return false;
            value = (uint8_t)((high << 4) | low);
            read += 2;
        }
        if (value == 0 || value < 0x20 || value > 0x7E) return false;
        *write++ = (char)value;
    }
    *write = '\0';
    return true;
}

static bool formValue(const char *form, const char *wanted,
                      char *output, size_t outputCapacity, bool &found)
{
    found = false;
    const size_t wantedLength = strlen(wanted);
    const char *position = form;
    while (*position != '\0')
    {
        const char *end = strchr(position, '&');
        if (end == nullptr) end = position + strlen(position);
        const char *equals = (const char *)memchr(position, '=', end - position);
        if (equals == nullptr) return false;
        if ((size_t)(equals - position) == wantedLength &&
            memcmp(position, wanted, wantedLength) == 0)
        {
            if (found) return false;
            const size_t valueLength = end - equals - 1;
            if (valueLength >= outputCapacity) return false;
            memcpy(output, equals + 1, valueLength);
            output[valueLength] = '\0';
            if (!urlDecodeInPlace(output)) return false;
            found = true;
        }
        position = *end == '&' ? end + 1 : end;
    }
    return true;
}

static bool parseUnsigned(const char *text, uint32_t minimum,
                          uint32_t maximum, uint32_t &value)
{
    if (text == nullptr || *text == '\0') return false;
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed < minimum || parsed > maximum)
        return false;
    value = (uint32_t)parsed;
    return true;
}

static bool parseSignedSmall(const char *text, int minimum,
                             int maximum, int &value)
{
    if (text == nullptr || *text == '\0') return false;
    char *end = nullptr;
    errno = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed < minimum || parsed > maximum)
        return false;
    value = (int)parsed;
    return true;
}

static bool parseHexBytes(const char *text, uint8_t *output, size_t bytes)
{
    if (strlen(text) != bytes * 2) return false;
    for (size_t i = 0; i < bytes; ++i)
    {
        const int high = hexValue(text[i * 2]);
        const int low = hexValue(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool normalizeMac(const char *input, char output[18])
{
    if (input[0] == '\0')
    {
        strcpy(output, "00:00:00:00:00:00");
        return true;
    }
    char digits[13];
    size_t count = 0;
    for (size_t i = 0; input[i] != '\0'; ++i)
    {
        if (isxdigit((unsigned char)input[i]))
        {
            if (count >= 12) return false;
            digits[count++] = (char)toupper((unsigned char)input[i]);
        }
        else if (input[i] != ':' && input[i] != '-')
        {
            return false;
        }
    }
    if (count == 0)
    {
        secureZero(digits, sizeof(digits));
        return false;
    }
    if (count != 12) return false;
    size_t out = 0;
    for (size_t i = 0; i < 12; ++i)
    {
        output[out++] = digits[i];
        if ((i % 2) == 1 && i != 11) output[out++] = ':';
    }
    output[out] = '\0';
    secureZero(digits, sizeof(digits));
    return true;
}

static bool allZeroMac(const char *mac)
{
    return strcmp(mac, "00:00:00:00:00:00") == 0;
}

static void bytesToHex(const uint8_t *input, size_t length,
                       char *output, size_t capacity)
{
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    if (capacity < length * 2 + 1) return;
    for (size_t i = 0; i < length; ++i)
    {
        output[i * 2] = HEX_DIGITS[input[i] >> 4];
        output[i * 2 + 1] = HEX_DIGITS[input[i] & 0x0F];
    }
    output[length * 2] = '\0';
}

static bool formBool(const char *form, const char *key, bool &value)
{
    char text[8];
    bool found = false;
    if (!formValue(form, key, text, sizeof(text), found) || !found) return false;
    if (strcmp(text, "1") == 0)
    {
        value = true;
        return true;
    }
    if (strcmp(text, "0") == 0)
    {
        value = false;
        return true;
    }
    return false;
}

static bool validWifiSsid(const char *ssid)
{
    if (ssid == nullptr) return false;
    const size_t length = strlen(ssid);
    if (length < 1 || length > 32) return false;
    for (size_t i = 0; i < length; ++i)
    {
        const uint8_t value = (uint8_t)ssid[i];
        if (value < 0x21 || value > 0x7E) return false;
    }
    return true;
}

static void sendConfigResultFrame(uint16_t id, bool ok, uint32_t revision,
                                  const char *code)
{
    size_t length = 0;
    txPayload[0] = '\0';
    appendField(txPayload, sizeof(txPayload), length, "ok", ok ? "1" : "0");
    if (ok)
    {
        appendUnsigned(txPayload, sizeof(txPayload), length, "revision", revision);
        appendField(txPayload, sizeof(txPayload), length, "message", "saved");
    }
    else
    {
        appendField(txPayload, sizeof(txPayload), length, "code", code);
        appendField(txPayload, sizeof(txPayload), length, "message", "rejected");
    }
    sendFrame("CONFIG_RESULT", id, txPayload);
    secureZero(txPayload, sizeof(txPayload));
}

static void rejectConfig(uint16_t id, const char *code)
{
    sendConfigResultFrame(id, false, activeRevision, code);
    lastConfigResultValid = true;
    lastConfigResultId = id;
    lastConfigResultOk = false;
    lastConfigResultRevision = activeRevision;
    strncpy(lastConfigResultCode, code, sizeof(lastConfigResultCode) - 1);
    lastConfigResultCode[sizeof(lastConfigResultCode) - 1] = '\0';
}

static int configKeyIndex(const char *key)
{
    static const char *KEYS[] = {
        "revision", "victron_mac", "addr_type", "device_instance",
        "pairing", "pin_set", "status_minutes", "wifi_minutes",
        "ble_attempts", "deveui", "joineui", "pin", "appkey",
        "wifi_password", "wifi_ssid"
    };
    for (uint8_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); ++i)
    {
        if (strcmp(key, KEYS[i]) == 0) return i;
    }
    return -1;
}

static bool parseConfigSet(char *form, uint16_t id,
                           CompanionConfigRequest &request,
                           const char *&errorCode)
{
    memset(&request, 0, sizeof(request));
    request.requestId = id;
    request.config = activeConfig;
    uint16_t seen = 0;
    bool revisionPresent = false;
    uint32_t requestedRevision = 0;

    char *position = form;
    while (*position != '\0')
    {
        char *end = strchr(position, '&');
        if (end != nullptr) *end = '\0';
        char *equals = strchr(position, '=');
        if (equals == nullptr)
        {
            errorCode = "bad_form";
            return false;
        }
        *equals++ = '\0';
        if (!urlDecodeInPlace(position) ||
            (*equals != '\0' && !urlDecodeInPlace(equals)))
        {
            errorCode = "bad_encoding";
            return false;
        }
        const int key = configKeyIndex(position);
        if (key < 0 || (seen & (1U << key)) != 0)
        {
            errorCode = key < 0 ? "unknown_field" : "duplicate_field";
            return false;
        }
        seen |= (uint16_t)(1U << key);

        uint32_t number = 0;
        int signedNumber = 0;
        switch (key)
        {
            case 0:
                if (!parseUnsigned(equals, 0, 0xFFFFFFFFUL, requestedRevision))
                {
                    errorCode = "bad_revision"; return false;
                }
                revisionPresent = true;
                break;
            case 1:
                if (!normalizeMac(equals, request.config.victronMac))
                {
                    errorCode = "bad_victron_mac"; return false;
                }
                break;
            case 2:
                if (!parseSignedSmall(equals, -1, 1, signedNumber))
                {
                    errorCode = "bad_addr_type"; return false;
                }
                request.config.victronAddressType = (int8_t)signedNumber;
                break;
            case 3:
                if (!parseUnsigned(equals, 0, 23, number))
                {
                    errorCode = "bad_instance"; return false;
                }
                request.config.victronDeviceInstance = (uint8_t)number;
                break;
            case 4:
                if (!parseUnsigned(equals, 0, 1, number))
                {
                    errorCode = "bad_pairing"; return false;
                }
                request.config.victronUseSmpPin = (uint8_t)number;
                break;
            case 5:
                if (!parseUnsigned(equals, 0, 1, number))
                {
                    errorCode = "bad_pin_state"; return false;
                }
                break; // informational; the actual PIN is write-only
            case 6:
                if (!parseUnsigned(equals, 1, 1440, number))
                {
                    errorCode = "bad_status_minutes"; return false;
                }
                request.config.statusIntervalMinutes = number;
                break;
            case 7:
                if (!parseUnsigned(equals, 5, 240, number))
                {
                    errorCode = "bad_wifi_minutes"; return false;
                }
                request.config.configWindowSeconds = number * 60UL;
                break;
            case 8:
                if (!parseUnsigned(equals, 1, 3, number))
                {
                    errorCode = "bad_ble_attempts"; return false;
                }
                request.config.bleMaxAttempts = (uint8_t)number;
                break;
            case 9:
                if (!parseHexBytes(equals, request.devEui, sizeof(request.devEui)))
                {
                    errorCode = "bad_deveui"; return false;
                }
                request.credentialMask |= COMPANION_SET_DEVEUI;
                break;
            case 10:
                if (!parseHexBytes(equals, request.joinEui, sizeof(request.joinEui)))
                {
                    errorCode = "bad_joineui"; return false;
                }
                request.credentialMask |= COMPANION_SET_JOINEUI;
                break;
            case 11:
                if (*equals != '\0')
                {
                    if (strlen(equals) != 6) { errorCode = "bad_pin"; return false; }
                    for (uint8_t i = 0; i < 6; ++i)
                    {
                        if (!isdigit((unsigned char)equals[i]))
                        { errorCode = "bad_pin"; return false; }
                    }
                    memcpy(request.config.victronPin, equals, 6);
                    request.config.victronPin[6] = '\0';
                }
                break;
            case 12:
                if (*equals != '\0')
                {
                    if (!parseHexBytes(equals, request.appKey, sizeof(request.appKey)))
                    { errorCode = "bad_appkey"; return false; }
                    request.credentialMask |= COMPANION_SET_APPKEY;
                }
                break;
            case 13:
                if (*equals != '\0')
                {
                    const size_t passwordLength = strlen(equals);
                    if (passwordLength < 12 || passwordLength > 63 ||
                        strchr(equals, ' ') != nullptr)
                    { errorCode = "bad_wifi_password"; return false; }
                    strncpy(request.config.wifiApPassword, equals,
                            sizeof(request.config.wifiApPassword) - 1);
                    request.config.wifiApPassword[
                        sizeof(request.config.wifiApPassword) - 1] = '\0';
                }
                break;
            case 14:
                if (!validWifiSsid(equals))
                {
                    errorCode = "bad_wifi_ssid"; return false;
                }
                strncpy(request.config.wifiApSsid, equals,
                        sizeof(request.config.wifiApSsid) - 1);
                request.config.wifiApSsid[
                    sizeof(request.config.wifiApSsid) - 1] = '\0';
                break;
        }
        if (end == nullptr) break;
        position = end + 1;
    }

    if (!revisionPresent || requestedRevision != activeRevision)
    {
        errorCode = "stale_revision";
        return false;
    }
    if (!runtimeConfigValid(request.config))
    {
        errorCode = "invalid_settings";
        return false;
    }
    if ((request.credentialMask & COMPANION_SET_DEVEUI) &&
        memcmp(request.devEui, activeDevEui, sizeof(request.devEui)) == 0)
        request.credentialMask &= (uint8_t)~COMPANION_SET_DEVEUI;
    if ((request.credentialMask & COMPANION_SET_JOINEUI) &&
        memcmp(request.joinEui, activeJoinEui, sizeof(request.joinEui)) == 0)
        request.credentialMask &= (uint8_t)~COMPANION_SET_JOINEUI;
    return true;
}

static void handleConfigSet(char *form, uint16_t id)
{
    if (id < 0x8000)
    {
        rejectConfig(id, "bad_id");
        return;
    }
    if (lastConfigResultValid && id == lastConfigResultId)
    {
        sendConfigResultFrame(id, lastConfigResultOk,
                              lastConfigResultRevision, lastConfigResultCode);
        return;
    }
    if (configRequestPending)
    {
        if (id != pendingConfigRequest.requestId) rejectConfig(id, "busy");
        return;
    }

    const char *errorCode = "bad_request";
    CompanionConfigRequest parsed;
    if (!parseConfigSet(form, id, parsed, errorCode))
    {
        companionWipeConfigRequest(parsed);
        rejectConfig(id, errorCode);
        return;
    }
    pendingConfigRequest = parsed;
    configRequestPending = true;
    companionWipeConfigRequest(parsed);
}

static void finishVictron(uint8_t result, uint8_t attempts, uint8_t loadValue)
{
    victronPending = false;
    victronRequestPending = false;
    victronAccepted = false;
    victronResult = result;
    victronAttempts = attempts;
    victronLoadValue = loadValue;
    victronResultReady = true;
}

static void handleVictronResult(const char *form, uint16_t id)
{
    if (!victronPending || id != victronRequestId) return;
    bool ok = false;
    bool verified = false;
    bool changed = false;
    bool targetSeen = false;
    if (!formBool(form, "ok", ok) ||
        !formBool(form, "verified", verified) ||
        !formBool(form, "changed", changed) ||
        !formBool(form, "target_seen", targetSeen))
    {
        finishVictron(BLE_COMPANION_TIMEOUT, 0, 0xFF);
        return;
    }

    char attemptsText[4] = {0};
    char resultText[4] = {0};
    char loadValueText[4] = {0};
    char verifiedValueText[4] = {0};
    bool attemptsFound = false;
    bool resultFound = false;
    bool loadValueFound = false;
    bool verifiedValueFound = false;
    uint32_t parsedAttempts = 0;
    uint32_t parsedResult = 0;
    uint32_t parsedLoadValue = 0;
    uint32_t parsedVerifiedValue = 0;
    const bool fieldsValid =
        formValue(form, "attempts", attemptsText, sizeof(attemptsText), attemptsFound) &&
        attemptsFound &&
        parseUnsigned(attemptsText, 0, activeConfig.bleMaxAttempts, parsedAttempts) &&
        formValue(form, "result_code", resultText, sizeof(resultText), resultFound) &&
        resultFound && parseUnsigned(resultText, BLE_OK, BLE_BUSY, parsedResult) &&
        formValue(form, "load_value", loadValueText,
                  sizeof(loadValueText), loadValueFound) &&
        loadValueFound && parseUnsigned(loadValueText, 0, 255, parsedLoadValue) &&
        formValue(form, "verified_value", verifiedValueText,
                  sizeof(verifiedValueText), verifiedValueFound) &&
        verifiedValueFound &&
        parseUnsigned(verifiedValueText, 0, 255, parsedVerifiedValue);
    const uint8_t kind=activeConfig.bleFunctions[desiredLoadValue].kind;
    const uint8_t expectedValue=activeConfig.deviceProfile==3?smartBatteryProtectMode(kind):smartMpptMode(kind);
    const bool success = fieldsValid && ok && parsedResult == BLE_OK && parsedAttempts >= 1 &&
        ((activeConfig.deviceProfile==3 && expectedValue!=255 && verified && parsedVerifiedValue==expectedValue && parsedLoadValue==(expectedValue==3?1u:0u)) ||
         (activeConfig.deviceProfile==1 && expectedValue!=255 && verified && (parsedLoadValue&15)==expectedValue && (parsedVerifiedValue&15)==expectedValue) ||
         (kind==3 && !verified && parsedLoadValue==255) ||
         (kind==4 && verified && parsedLoadValue==255));
    const bool failure = fieldsValid && !ok && !verified &&
        parsedResult >= BLE_BAD_SETTINGS && parsedResult <= BLE_BUSY;
    (void)changed;    // Parsed because these are mandatory protocol fields.
    (void)targetSeen; // A successful direct connect is stronger than scan sighting.
    secureZero(attemptsText, sizeof(attemptsText));
    secureZero(resultText, sizeof(resultText));
    secureZero(loadValueText, sizeof(loadValueText));
    secureZero(verifiedValueText, sizeof(verifiedValueText));
    if (!success && !failure)
    {
        finishVictron(BLE_COMPANION_TIMEOUT, 0, 0xFF);
        return;
    }

    finishVictron((uint8_t)parsedResult, (uint8_t)parsedAttempts,
                  (uint8_t)parsedLoadValue);
}

static bool payloadProtoOne(const char *payload)
{
    char proto[8];
    bool found = false;
    return formValue(payload, "proto", proto, sizeof(proto), found) &&
           found && strcmp(proto, "1") == 0;
}

static void handleFrame(const char *type, uint16_t id, char *payload)
{
    if(!strcmp(type,"USB_READY")&&usbAwaiting){
        usbAwaiting=false;usbReady=!strcmp(payload,"1")||!strcmp(payload,"2");
        if(!usbReady){otaHoldUntil=0;Serial.println("LBR_USB_UNAVAILABLE");}return;
    }
    if (!strcmp(type,"UPDATE_PREPARE")) {
        unsigned long size=0,crc=0,mode=0;char extra;
        bool valid=sscanf(payload,"%lu,%lu,%lu%c",&size,&crc,&mode,&extra)==3 &&
          size>=256 && size<=LBR_STM_MAX && size%8==0 && mode<=1 &&
          linkReady && !victronPending && !victronQueuedCount && firmwareUpdateReady();
        if(valid){updateSize=size;updateCrc=crc;updateMode=mode;updatePreparedAt=millis();otaHoldUntil=millis()+600000UL;}
        else updatePreparedAt=0;
        sendFrame("UPDATE_READY",0,valid?"1":"0");return;
    }
    if (!strcmp(type,"UPDATE_EXEC")) {
        unsigned long size=0,crc=0,mode=0;char extra;
        if(updatePreparedAt && millis()-updatePreparedAt<10000 &&
           sscanf(payload,"%lu,%lu,%lu%c",&size,&crc,&mode,&extra)==3 &&
           size==updateSize && crc==updateCrc && mode==updateMode && !victronPending)
            firmwareUpdateLaunch(size,crc,mode);
        updatePreparedAt=0;return;
    }
#ifdef ESP_RECOVERY_SSID
    if(!strcmp(type,"READY") || !strcmp(type,"HELLO_ACK") || !strcmp(type,"RESULT") || !strcmp(type,"PORTAL_STATE"))
      Serial.printf("ESP %s %u %s\\r\\n",type,id,payload);
#endif
    if (strcmp(type, "READY") == 0 && id == 0)
    {
        if (payloadProtoOne(payload))
        {
            const uint32_t now = millis();
            const bool wasLinkReady = linkReady;
            otaHoldUntil=0;

            // READY always starts a new ESP session. This is also safe for the
            // initial boot and prevents stale portal/request state if the ESP
            // resets during (rather than after) a handshake.
            linkReady = false;
            helloAttempts = 0;
            helloId = 0;
            portalRunning = false;
            portalRequestPending = false;
            portalDirty = true;
            contactDirty = true;
            snapshotDirty = true;
            lastPortalRefreshAt = 0;
            nextPortalRetryAt = now;
            victronRequestPending = false;
            victronAccepted = false;

            // READY is an explicit new ESP session. Clear the request-ID replay
            // cache even when this READY was received without a power cycle.
            resetConfigSessionState();

            if (wasLinkReady)
            {
                // READY after a completed handshake means the ESP restarted.
                // Re-handshake and give the current job a bounded fresh try,
                // without extending its original accepted-job deadline.
                linkFault = true;
                if (victronPending)
                {
                    if (victronLinkFailures < MAX_VICTRON_LINK_FAILURES)
                        ++victronLinkFailures;
                    if (victronLinkFailures >= MAX_VICTRON_LINK_FAILURES)
                        finishVictron(BLE_COMPANION_TIMEOUT, 0, 0xFF);
                }
            }
            readySeen = true;
            nextHelloAt = now;
        }
        return;
    }
    if (strcmp(type, "HELLO_ACK") == 0 && id == helloId)
    {
        if (payloadProtoOne(payload))
        {
            linkReady = true;
            linkFault = false;
            portalDirty = true;
            contactDirty = true;
            snapshotDirty = true;
        }
        return;
    }
    // All remaining frame types belong to an established session. This also
    // rejects bytes left behind by the previous ESP instance after READY.
    if (!linkReady) return;
    if (strcmp(type, "PORTAL_STATE") == 0 && id == 0)
    {
        bool running = false;
        if (formBool(payload, "running", running))
        {
            portalRunning = running;
            if (running)
            {
                portalDirty = false;
                resetPortalRetry(millis());
            }
            else if (desiredPortal && !portalRequestPending && !portalDirty)
            {
                schedulePortalRetry(millis());
            }
        }
        return;
    }
    if (strcmp(type, "RESULT") == 0 && portalRequestPending &&
        id == portalRequestId)
    {
        bool ok = false;
        const bool validResult = formBool(payload, "ok", ok);
        portalRequestPending = false;
        if (portalRequestWasStart)
        {
            if (validResult && ok)
            {
                portalRunning = true;
                portalDirty = false;
                resetPortalRetry(millis());
            }
            else
            {
                portalRunning = false;
                if (desiredPortal) schedulePortalRetry(millis());
            }
        }
        else if (validResult && ok)
        {
            portalRunning = false;
        }
        return;
    }
    if (!strcmp(type,"OTA_STATE")) {
        otaHoldUntil=!strcmp(payload,"1")?millis()+600000UL:0;return;
    }
    if (!strcmp(type,"BLE_RESULT")) {scanHoldUntil=0;return;}
    if (strncmp(type, "HTTP_", 5) == 0) { portalNativeFrame(type, id, payload); return; }
    if (strcmp(type, "VICTRON_ACCEPTED") == 0 && victronPending &&
        id == victronRequestId)
    {
        if (payloadProtoOne(payload) && !victronAccepted)
        {
            victronAccepted = true;
            if (!victronEverAccepted)
            {
                victronEverAccepted = true;
                victronJobStartedAt = millis();
            }
        }
        return;
    }
    if (strcmp(type, "VICTRON_RESULT") == 0)
    {
        handleVictronResult(payload, id);
    }
}

static void processRxLine()
{
    if (rxLineOverflow || rxLineLength < 10 || rxLine[0] != '@') return;
    rxLine[rxLineLength] = '\0';
    char *crcSeparator = strrchr(rxLine, '|');
    if (crcSeparator == nullptr) return;
    uint16_t receivedCrc = 0;
    if (!parseHex16(crcSeparator + 1, receivedCrc)) return;
    const uint16_t calculatedCrc = crc16CcittFalse(
        reinterpret_cast<const uint8_t *>(&rxLine[1]),
        (size_t)(crcSeparator - &rxLine[1]));
    if (receivedCrc != calculatedCrc) return;
    *crcSeparator = '\0';

    char *version = &rxLine[1];
    char *separator1 = strchr(version, '|');
    if (separator1 == nullptr) return;
    *separator1++ = '\0';
    char *type = separator1;
    char *separator2 = strchr(type, '|');
    if (separator2 == nullptr) return;
    *separator2++ = '\0';
    char *idText = separator2;
    char *separator3 = strchr(idText, '|');
    if (separator3 == nullptr) return;
    *separator3++ = '\0';
    char *base64 = separator3;
    if (strchr(base64, '|') != nullptr || strcmp(version, "1") != 0 ||
        !validType(type)) return;

    uint16_t id = 0;
    if (!parseHex16(idText, id)) return;
    size_t payloadLength = 0;
    if (!base64Decode(base64, rxPayload, MAX_DECODED_PAYLOAD, payloadLength) ||
        memchr(rxPayload, '\0', payloadLength) != nullptr)
    {
        secureZero(rxPayload, sizeof(rxPayload));
        return;
    }
    rxPayload[payloadLength] = '\0';
    handleFrame(type, id, reinterpret_cast<char *>(rxPayload));
    secureZero(rxPayload, sizeof(rxPayload));
}

static void readEspFrames()
{
    while (Serial1.available() > 0)
    {
        const int input = Serial1.read();
        if (input < 0) break;
        const char value = (char)input;
        if (value == '\n')
        {
            processRxLine();
            secureZero(rxLine, sizeof(rxLine));
            rxLineLength = 0;
            rxLineOverflow = false;
        }
        else if (value != '\r')
        {
            if (rxLineLength < MAX_FRAME)
            {
                rxLine[rxLineLength++] = value;
            }
            else
            {
                rxLineOverflow = true;
            }
        }
    }
}

static void drainEspUart()
{
    while (Serial1.available() > 0) (void)Serial1.read();
}

static void powerEspOn(uint32_t now)
{
    setCurrentATMode(LORA_AT_MODE);
    drainEspUart();
    secureZero(rxLine, sizeof(rxLine));
    rxLineLength = 0;
    rxLineOverflow = false;
    // HELLO fallback can succeed even if READY was lost. Treat every physical
    // power-on as a new request-ID session independently of receiving READY.
    resetConfigSessionState();
    setEspPowerMode(POWER_ON);
    espPowered = true;
    linkReady = false;
    linkFault = false;
    readySeen = false;
    helloAttempts = 0;
    helloId = 0;
    nextHelloAt = now + ESP_BOOT_WAIT_MS;
    portalRunning = false;
    portalRequestPending = false;
    portalDirty = true;
    contactDirty = true;
    snapshotDirty = true;
}

static void powerEspOff(uint32_t now)
{
    setEspPowerMode(POWER_OFF);
    espPowered = false;
    linkReady = false;
    readySeen = false;
    portalRunning = false;
    portalRequestPending = false;
    victronRequestPending = false;
    nextPowerAttemptAt = now + 2000UL;
    secureZero(rxLine, sizeof(rxLine));
    secureZero(rxPayload, sizeof(rxPayload));
}

static void handleCompanionLinkFailure(uint32_t now)
{
    if (desiredPortal) schedulePortalRetry(now);
    if (victronPending)
    {
        if (victronLinkFailures < MAX_VICTRON_LINK_FAILURES)
            ++victronLinkFailures;
        if (victronLinkFailures >= MAX_VICTRON_LINK_FAILURES)
        {
            finishVictron(BLE_COMPANION_TIMEOUT, 0, 0xFF);
        }
    }
    linkFault = true;
    powerEspOff(now);
    if (desiredPortal && !victronPending)
        nextPowerAttemptAt = nextPortalRetryAt;
}

static void timeoutCurrentVictronJob(uint32_t now)
{
    // A missing ACK can still mean the ESP accepted the command. Fence that
    // possibly-running worker before a queued edge is assigned a new ID.
    finishVictron(BLE_COMPANION_TIMEOUT, 0, 0xFF);
    linkFault = true;
    powerEspOff(now);
}

static void sendHello(uint32_t now)
{
    if (helloId == 0) helloId = allocateRequestId();
    sendFrame("HELLO", helloId, "role=stm32&proto=1");
    ++helloAttempts;
    nextHelloAt = now + HELLO_RETRY_MS;
}

static void sendContact()
{
    size_t length = 0;
    txPayload[0] = '\0';
    appendField(txPayload, sizeof(txPayload), length, "active",
                desiredContact ? "1" : "0");
    sendFrame("CONTACT", allocateRequestId(), txPayload);
    secureZero(txPayload, sizeof(txPayload));
    contactDirty = false;
}

static void sendSnapshot()
{
    size_t length = 0;
    char number[12];
    char devEui[17];
    char joinEui[17];
    bytesToHex(activeDevEui, sizeof(activeDevEui), devEui, sizeof(devEui));
    bytesToHex(activeJoinEui, sizeof(activeJoinEui), joinEui, sizeof(joinEui));
    txPayload[0] = '\0';
    appendUnsigned(txPayload, sizeof(txPayload), length, "revision", activeRevision);
    appendField(txPayload, sizeof(txPayload), length, "victron_mac",
                allZeroMac(activeConfig.victronMac) ? "" : activeConfig.victronMac);
    snprintf(number, sizeof(number), "%d", (int)activeConfig.victronAddressType);
    appendField(txPayload, sizeof(txPayload), length, "addr_type", number);
    appendUnsigned(txPayload, sizeof(txPayload), length, "device_instance",
                   activeConfig.victronDeviceInstance);
    appendField(txPayload, sizeof(txPayload), length, "pairing",
                activeConfig.victronUseSmpPin ? "1" : "0");
    appendField(txPayload, sizeof(txPayload), length, "pin_set",
                activeConfig.victronUseSmpPin ? "1" : "0");
    appendUnsigned(txPayload, sizeof(txPayload), length, "status_minutes",
                   activeConfig.statusIntervalMinutes);
    appendUnsigned(txPayload, sizeof(txPayload), length, "wifi_minutes",
                   activeConfig.configWindowSeconds / 60UL);
    appendUnsigned(txPayload, sizeof(txPayload), length, "ble_attempts",
                   activeConfig.bleMaxAttempts);
    appendField(txPayload, sizeof(txPayload), length, "wifi_ssid",
                activeConfig.wifiApSsid);
    appendField(txPayload, sizeof(txPayload), length, "deveui", devEui);
    appendField(txPayload, sizeof(txPayload), length, "joineui", joinEui);
    sendFrame("CONFIG_SNAPSHOT", allocateRequestId(), txPayload);
    secureZero(txPayload, sizeof(txPayload));
    secureZero(devEui, sizeof(devEui));
    secureZero(joinEui, sizeof(joinEui));
    snapshotDirty = false;
}

static void buildPortalStartPayload()
{
    size_t length = 0;
    uint32_t seconds = desiredSecondsRemaining;
    // CONTACT may remain active indefinitely. Give the ESP the largest
    // fail-safe lease and refresh it halfway; falling still uses the exact
    // unchanged STM deadline and never creates a new hour.
    if (desiredContact) seconds = 86400UL;
    if (seconds < 60) seconds = 60;
    if (seconds > 86400UL) seconds = 86400UL;
    txPayload[0] = '\0';
    appendUnsigned(txPayload, sizeof(txPayload), length, "seconds", seconds);
    appendField(txPayload, sizeof(txPayload), length, "password",
                activeConfig.wifiApPassword);
    appendField(txPayload, sizeof(txPayload), length, "ssid",
                activeConfig.wifiApSsid);
}

static void sendPortalRequest(uint32_t now, bool start, bool retry)
{
    if (!retry)
    {
        portalRequestId = allocateRequestId();
        portalRequestAttempts = 0;
    }
    if (start)
    {
        buildPortalStartPayload();
        sendFrame("PORTAL_START", portalRequestId, txPayload);
        secureZero(txPayload, sizeof(txPayload));
    }
    else
    {
        sendFrame("PORTAL_STOP", portalRequestId, "");
    }
    portalRequestWasStart = start;
    portalRequestPending = true;
    ++portalRequestAttempts;
    portalRequestSentAt = now;
    if (start)
    {
        portalDirty = false;
        lastPortalRefreshAt = now;
    }
}

static void sendVictronRequest(uint32_t now)
{
    if (!victronDeliveryStarted)
    {
        victronRequestId = allocateRequestId();
        victronAccepted = false;
        victronDeliveryStartedAt = now;
        victronDeliveryStarted = true;
    }
    size_t length = 0;
    char number[12];
    txPayload[0] = '\0';
    const BleFunction &f=activeConfig.bleFunctions[desiredLoadValue];
    appendUnsigned(txPayload, sizeof(txPayload), length, "value", genericGattKind(f.kind)?0:activeConfig.deviceProfile==3?smartBatteryProtectMode(f.kind):smartMpptMode(f.kind));
    appendUnsigned(txPayload,sizeof(txPayload),length,"driver",activeConfig.deviceProfile);
    appendUnsigned(txPayload,sizeof(txPayload),length,"generic",genericGattKind(f.kind)?f.kind:0);
    if(genericGattKind(f.kind)) {
        char text[41];
        functionHex(f.service,16,text,true);appendField(txPayload,sizeof(txPayload),length,"service",text);
        functionHex(f.characteristic,16,text,true);appendField(txPayload,sizeof(txPayload),length,"char",text);
        functionHex(f.value,f.valueLength,text);appendField(txPayload,sizeof(txPayload),length,"hex",text);
    }
    appendField(txPayload, sizeof(txPayload), length, "mac",
                activeConfig.victronMac);
    snprintf(number, sizeof(number), "%d", (int)activeConfig.victronAddressType);
    appendField(txPayload, sizeof(txPayload), length, "addr_type", number);
    appendUnsigned(txPayload, sizeof(txPayload), length, "instance",
                   activeConfig.deviceProfile==3?0:activeConfig.victronDeviceInstance);
    appendUnsigned(txPayload, sizeof(txPayload), length, "attempts",
                   activeConfig.bleMaxAttempts);
    appendField(txPayload, sizeof(txPayload), length, "pairing",
                activeConfig.victronUseSmpPin ? "1" : "0");
    if (activeConfig.victronUseSmpPin)
    {
        appendField(txPayload, sizeof(txPayload), length, "pin",
                    activeConfig.victronPin);
    }
    sendFrame("VICTRON_LOAD_SET", victronRequestId, txPayload);
    secureZero(txPayload, sizeof(txPayload));
    victronRequestPending = true;
    victronRequestSentAt = now;
}
} // namespace

void companionBegin(const RuntimeConfig &config, uint32_t revision,
                      const uint8_t devEui[8], const uint8_t joinEui[8])
{
    api.system.atMode.add("USB","1: legacy; 2: checked local USB firmware transport","USB",usbCommand,RAK_ATCMD_PERM_WRITE);
#ifdef ESP_RECOVERY_SSID
    recoveryConfig=config;activeConfigPointer=&recoveryConfig;
#else
    activeConfigPointer=&config;
#endif
#ifdef ESP_RECOVERY_SSID
    strcpy(recoveryConfig.wifiApSsid,"LoRaBLE-Recovery");
#endif
    activeRevision = revision;
    memcpy(activeDevEui, devEui, sizeof(activeDevEui));
    memcpy(activeJoinEui, joinEui, sizeof(activeJoinEui));
    setCurrentATMode(LORA_AT_MODE);
    setEspPowerMode(POWER_OFF);
    const uint32_t now = millis();
    nextPowerAttemptAt = now;
    resetPortalRetry(now);
}

void companionSetDemand(bool portalWanted, bool contactActive,
                        uint32_t secondsRemaining)
{
    const uint32_t now = millis();
    // A local USB updater also works after the configured WiFi window expires.
    // This temporary maintenance demand is RAM-only and never changes settings.
    if(usbWakeUntil && !timeReached(now,usbWakeUntil)){
        portalWanted=true;
        const uint32_t usbSeconds=(usbWakeUntil-now+999UL)/1000UL;
        if(secondsRemaining<usbSeconds)secondsRemaining=usbSeconds;
    }
    if (desiredPortal != portalWanted)
    {
        desiredPortal = portalWanted;
        portalDirty = true;
        resetPortalRetry(now);
    }
    if (desiredContact != contactActive)
    {
        desiredContact = contactActive;
        contactDirty = true;
        portalDirty = true;
        nextPortalRetryAt = now;
    }
    desiredSecondsRemaining = secondsRemaining;
}

void companionService(uint32_t now)
{
    if(espPowered) readEspFrames();
    if(usbReady){
        usbReady=false;otaHoldUntil=millis()+600000UL;
        const SERVICE_MODE_TYPE savedMode=g_rui_cfg_t.mode_type[DEFAULT_SERIAL_CONSOLE];
        usbRawActive=true;
        g_rui_cfg_t.mode_type[DEFAULT_SERIAL_CONSOLE]=SERVICE_MODE_TYPE_CUSTOM;
        Serial.println(usbProtocol==2?"LBR_USB_READY2":"LBR_USB_READY");Serial.flush();
        uint32_t last=millis(),start=last;char tail[10]={0};bool end=false;
        // ESP ends an abandoned exchange after 15 s. Stay in raw mode long
        // enough to receive its terminator instead of feeding it to the AT CLI.
        while(!end && millis()-last<35000UL && millis()-start<660000UL){
            uint8_t bytes[64];size_t count=0;
            while(Serial.available() && count<sizeof(bytes)){
                const int c=Serial.read();if(c<0)break;bytes[count++]=(uint8_t)c;
            }
            if(count){Serial1.write(bytes,count);last=millis();}
            count=0;
            while(Serial1.available() && count<sizeof(bytes)){
                const int value=Serial1.read();if(value<0)break;
                const char c=(char)value;bytes[count++]=(uint8_t)c;last=millis();
                memmove(tail,tail+1,8);tail[8]=c;
                if(!strcmp(tail,"~LBR-END~")){end=true;break;}
            }
            if(count)Serial.write(bytes,count);
            // Arduino delay() calls rui_running(), reentering the application/
            // serial event pump inside this raw exchange. UART DMA interrupts
            // remain enabled; use only the hardware wait and feed the watchdog.
            // The SDK watchdog handle is optional; the reload register is safe
            // with the watchdog either active or inactive. Do not initialize it.
            IWDG->KR=0xAAAAu;udrv_delay_us(100);
        }
        Serial.flush();g_rui_cfg_t.mode_type[DEFAULT_SERIAL_CONSOLE]=savedMode;
        usbRawActive=false;
        service_mode_cli_init(DEFAULT_SERIAL_CONSOLE);otaHoldUntil=millis()+15000UL;
        return;
    }
    if(usbAwaiting && now-usbRequestedAt>6000UL){usbAwaiting=false;otaHoldUntil=0;Serial.println("LBR_USB_UNAVAILABLE");}
    if(usbRequested){
        if(linkReady&&portalRunning&&!victronPending&&!victronQueuedCount){
            usbRequested=false;
            usbAwaiting=true;usbRequestedAt=now;otaHoldUntil=now+720000UL;sendFrame("USB_OPEN",0,usbProtocol==2?"2":"1");
        }else if(now-usbRequestedAt>15000UL){
            usbRequested=false;
            Serial.printf("LBR_USB_UNAVAILABLE link=%u portal=%u busy=%u queued=%u\r\n",linkReady,portalRunning,victronPending,victronQueuedCount);
        }
    }
    if(otaHoldUntil && !timeReached(now,otaHoldUntil)) return;
    const bool powerWanted = desiredPortal || victronPending || victronQueuedCount > 0 ||
        (otaHoldUntil && !timeReached(now,otaHoldUntil)) || (scanHoldUntil && !timeReached(now,scanHoldUntil));
    if (!powerWanted)
    {
        if (espPowered) powerEspOff(now);
        return;
    }

    // All deadlines belong to the logical job, not to one UART/ESP session.
    // Check them even while the companion is rebooting or re-handshaking. The
    // pre-delivery envelope prevents a READY/HELLO loop before the first send
    // from retaining one job forever.
    if (victronPending &&
        ((!victronDeliveryStarted &&
          (uint32_t)(now - victronActivatedAt) >=
              VICTRON_PRE_DELIVERY_TIMEOUT_MS) ||
         (victronEverAccepted &&
          (uint32_t)(now - victronJobStartedAt) >= VICTRON_JOB_TIMEOUT_MS) ||
         (!victronEverAccepted && victronDeliveryStarted &&
          (uint32_t)(now - victronDeliveryStartedAt) >=
              VICTRON_DELIVERY_TIMEOUT_MS)))
    {
        timeoutCurrentVictronJob(now);
        return;
    }
    if (!espPowered)
    {
        if (timeReached(now, nextPowerAttemptAt)) powerEspOn(now);
        return;
    }

    readEspFrames();
    if (!linkReady)
    {
        if (timeReached(now, nextHelloAt))
        {
            if (helloAttempts >= HELLO_MAX_ATTEMPTS)
            {
                handleCompanionLinkFailure(now);
                return;
            }
            // READY accelerates this path, but HELLO is also retried if READY
            // was emitted before the STM UART became ready.
            (void)readySeen;
            sendHello(now);
        }
        return;
    }

    if(otaHoldUntil && !timeReached(now,otaHoldUntil)) return;
    if (snapshotDirty) sendSnapshot();
    if (contactDirty) sendContact();

    if (portalRequestPending &&
        (uint32_t)(now - portalRequestSentAt) >= REQUEST_RETRY_MS)
    {
        if (portalRequestAttempts < REQUEST_MAX_ATTEMPTS)
        {
            sendPortalRequest(now, portalRequestWasStart, true);
        }
        else
        {
            portalRequestPending = false;
            // Never power-cycle the ESP underneath a long native Victron job
            // merely because a portal control response was lost.
            if (victronPending)
            {
                if (desiredPortal) schedulePortalRetry(now);
            }
            else
            {
                handleCompanionLinkFailure(now);
                return;
            }
        }
    }
    else if (!portalRequestPending)
    {
        if (desiredPortal &&
            ((portalDirty && timeReached(now, nextPortalRetryAt)) ||
             (desiredContact &&
              (uint32_t)(now - lastPortalRefreshAt) >= ACTIVE_PORTAL_REFRESH_MS)))
        {
            sendPortalRequest(now, true, false);
        }
        else if (!desiredPortal && portalRunning)
        {
            sendPortalRequest(now, false, false);
        }
    }

    if (victronPending)
    {
        if (!victronRequestPending)
        {
            // Keep the same ID and delivery start across an ESP restart.
            sendVictronRequest(now);
        }
        else if (!victronAccepted &&
                 (uint32_t)(now - victronRequestSentAt) >=
                     VICTRON_DELIVERY_RETRY_MS)
        {
            sendVictronRequest(now);
        }
        else if (victronAccepted &&
                 (uint32_t)(now - victronRequestSentAt) >=
                     VICTRON_REPLAY_RETRY_MS)
        {
            // Same ID: active duplicates are harmless and, after completion,
            // refresh ACCEPTED or ask the ESP to replay its cached final result.
            sendVictronRequest(now);
        }
    }
}

bool companionRequestVictronLoad(uint8_t value)
{
    if (!activeConfig.loadOutputEnabled) return false;
    if (value>=activeConfig.functionCount || victronPending || victronQueuedCount || !activeConfig.bleFunctions[value].kind) return false;
    desiredLoadValue = value;
    if (victronPending)
    {
        if (victronQueuedCount < MAX_QUEUED_VICTRON_JOBS)
        {
            ++victronQueuedCount;
            return true;
        }
        else
        {
            Serial.println("Victron-wachtrij vol; nieuwste flank genegeerd.");
            return false;
        }
    }
    victronPending = true;
    victronActivatedAt = millis();
    victronRequestPending = false;
    victronLinkFailures = 0;
    victronAccepted = false;
    victronDeliveryStarted = false;
    victronEverAccepted = false;
    return true;
}

bool companionUsbActive(){return usbRawActive;}

bool companionTakeVictronResult(uint8_t &result, uint8_t &attempts,
                                uint8_t &loadValue)
{
    if (!victronResultReady) return false;
    result = victronResult;
    attempts = victronAttempts;
    loadValue = victronLoadValue;
    victronResultReady = false;
    if (victronQueuedCount > 0)
    {
        --victronQueuedCount;
        victronPending = true;
        victronActivatedAt = millis();
        victronRequestPending = false;
        victronLinkFailures = 0;
        victronAccepted = false;
        victronDeliveryStarted = false;
        victronEverAccepted = false;
    }
    return true;
}

bool companionTakeConfigRequest(CompanionConfigRequest &request)
{
    if (!configRequestPending) return false;
    request = pendingConfigRequest;
    companionWipeConfigRequest(pendingConfigRequest);
    configRequestPending = false;
    return true;
}

void companionConfigResult(uint16_t requestId, bool ok, uint32_t revision,
                           const char *code)
{
    const char *safeCode = (code == nullptr || *code == '\0') ? "save_failed" : code;
    sendConfigResultFrame(requestId, ok, revision, safeCode);
    lastConfigResultValid = true;
    lastConfigResultId = requestId;
    lastConfigResultOk = ok;
    lastConfigResultRevision = revision;
    strncpy(lastConfigResultCode, safeCode, sizeof(lastConfigResultCode) - 1);
    lastConfigResultCode[sizeof(lastConfigResultCode) - 1] = '\0';
}

void companionUpdateSnapshot(const RuntimeConfig &config, uint32_t revision,
                             const uint8_t devEui[8], const uint8_t joinEui[8])
{
#ifdef ESP_RECOVERY_SSID
    recoveryConfig=config;activeConfigPointer=&recoveryConfig;
#else
    activeConfigPointer=&config;
#endif
#ifdef ESP_RECOVERY_SSID
    strcpy(recoveryConfig.wifiApSsid,"LoRaBLE-Recovery");
#endif
    activeRevision = revision;
    memcpy(activeDevEui, devEui, sizeof(activeDevEui));
    memcpy(activeJoinEui, joinEui, sizeof(activeJoinEui));
    snapshotDirty = true;
    portalDirty = true;
    nextPortalRetryAt = millis();
}

void companionWipeConfigRequest(CompanionConfigRequest &request)
{
    secureZero(&request, sizeof(request));
}

bool companionIsReady()
{
    return linkReady;
}

bool companionIsPowered()
{
    return espPowered;
}

uint8_t companionState()
{
    if (linkFault) return 4;
    if (!espPowered) return 0;
    if (!linkReady) return 1;
    if (victronPending) return 3;
    if (desiredPortal || portalRunning) return 2;
    return 1;
}

void companionSendJson(uint16_t id, bool ok, const char *json)
{
    char chunk[240];
    const size_t length = strlen(json);
    for(size_t offset=0;offset<length;offset+=200) {
        snprintf(chunk,sizeof(chunk),"%u:%.*s",(unsigned)offset,
          (int)((length-offset)>200?200:length-offset),json+offset);
        sendFrame("HTTP_DATA",id,chunk);
    }
    snprintf(chunk,sizeof(chunk),"%u:%u",ok?200:400,(unsigned)length);
    sendFrame("HTTP_END",id,chunk);
}
bool companionScan()
{
    if(!linkReady || victronPending || (otaHoldUntil && !timeReached(millis(),otaHoldUntil))) return false;
    scanHoldUntil=millis()+30000UL;
    char payload[64];
    const bool targetSet = strcmp(activeConfig.victronMac,"00:00:00:00:00:00") != 0;
    snprintf(payload,sizeof(payload),"duration_ms=5000%s%s",
        targetSet ? "&target_mac=" : "", targetSet ? activeConfig.victronMac : "");
    return sendFrame("BLE_SCAN",allocateRequestId(),payload);
}
#endif // !LEGACY_BLE_AT
