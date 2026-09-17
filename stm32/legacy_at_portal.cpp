#include "legacy_at_portal.h"
#include "activity_log.h"



#include "settings.h"
#if LEGACY_BLE_AT
#include "portal_asset.h"
#endif
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace
{
static const size_t HTTP_CAPACITY = 6144;
static const uint32_t AT_TIMEOUT_MS = 5000;

enum IpdState : uint8_t
{
    IPD_SEEK,
    IPD_LINK,
    IPD_LENGTH,
    IPD_META,
    IPD_DATA
};

static bool running = false;
static bool paused = false;
static IpdState ipdState = IPD_SEEK;
static uint8_t seekMatched = 0;
static int8_t ipdLink = -1;
static uint32_t ipdLength = 0;
static uint32_t ipdReceived = 0;
static char httpRequest[HTTP_CAPACITY];
static size_t httpLength = 0;
static int8_t httpLink = -1;
static bool requestReady = false;
static bool updatePending = false;
static int8_t responseLink = -1;
static uint16_t responseRequestId = 0;
static uint16_t nextRequestId = 1;
static CompanionConfigRequest pendingUpdate;
static const RuntimeConfig *snapshotPointer;
#define snapshot (*snapshotPointer)
static PortalLiveStatus live;
static uint8_t pendingAction = 0;
static uint8_t snapshotDevEui[8];
static uint8_t snapshotJoinEui[8];
static void inspectRequestCompletion();
static uint32_t bufferedRequests[5] = {0};
static char noticeLine[80];
static uint8_t noticeLength = 0;

// Browsers may request /config while the previous response is being sent.
// AT acknowledgements and unsolicited +IPD notices share this UART. Preserve
// the latter instead of silently discarding them while waiting for SEND OK.
static void observeNotice(char value)
{
    if (value == '\n')
    {
        noticeLine[noticeLength] = 0;
        unsigned link = 0, length = 0;
        if (sscanf(noticeLine, "+IPD,%u,%u", &link, &length) == 2 && link < 5 && length <= HTTP_CAPACITY)
        {
            bufferedRequests[link] = length;
        }
        noticeLength = 0;
    }
    else if (value != '\r')
    {
        if (noticeLength + 1 < sizeof(noticeLine)) noticeLine[noticeLength++] = value;
        else noticeLength = 0;
    }
}

static void clearHttp()
{
    memset(httpRequest, 0, sizeof(httpRequest));
    httpLength = 0;
    httpLink = -1;
    requestReady = false;
}

static void resetParser()
{
    ipdState = IPD_SEEK;
    seekMatched = 0;
    ipdLink = -1;
    ipdLength = 0;
    ipdReceived = 0;
    clearHttp();
}

static void drainUart()
{
    while (Serial1.available() > 0) observeNotice((char)Serial1.read());
}

static bool waitForToken(const char *token, uint32_t timeoutMs)
{
    const size_t tokenLength = strlen(token);
    size_t matched = 0;
    const uint32_t started = millis();
    while ((uint32_t)(millis() - started) < timeoutMs)
    {
        while (Serial1.available() > 0)
        {
            const int value = Serial1.read();
            if (value < 0) continue;
            const char ch = (char)value;
            observeNotice(ch);
            if (ch == token[matched])
            {
                ++matched;
                if (matched == tokenLength) return true;
            }
            else
            {
                matched = (ch == token[0]) ? 1 : 0;
            }
        }
        delay(2);
    }
    return false;
}

static bool atCommand(const String &command, uint32_t timeoutMs = AT_TIMEOUT_MS)
{
    drainUart();
    Serial1.print(command);
    Serial1.print("\r\n");
    Serial1.flush();
    return waitForToken("\r\nOK\r\n", timeoutMs);
}

static bool readUartByte(char &value, uint32_t timeoutMs)
{
    const uint32_t started = millis();
    while ((uint32_t)(millis() - started) < timeoutMs)
    {
        if (Serial1.available() > 0)
        {
            const int received = Serial1.read();
            if (received >= 0)
            {
                value = (char)received;
                return true;
            }
        }
        delay(1);
    }
    return false;
}

static bool receivePassiveChunk(int8_t link, uint32_t requested,
                                uint32_t &actual)
{
    actual = 0;
    String command("AT+CIPRECVDATA=");
    command += String(link);
    command += ',';
    command += String((unsigned long)requested);
    drainUart();
    // +IPD reports the remaining bytes after each passive read. Consuming
    // the final chunk must not leave the previous remainder queued forever.
    // Any NEW notice received during this command will replace this value.
    bufferedRequests[link] = 0;
    Serial1.print(command);
    Serial1.print("\r\n");
    Serial1.flush();
    if (!waitForToken("+CIPRECVDATA:", 3000))
    {
        Serial.println("CIPRECVDATA: geen antwoordheader.");
        return false;
    }

    char value = 0;
    bool haveLength = false;
    while (readUartByte(value, 2000))
    {
        if (isdigit((unsigned char)value))
        {
            haveLength = true;
            actual = actual * 10UL + (uint32_t)(value - '0');
            if (actual > requested)
            {
                Serial.println("CIPRECVDATA: ongeldige antwoordlengte.");
                return false;
            }
        }
        else if (value == ',' && haveLength)
        {
            break;
        }
        else
        {
            Serial.println("CIPRECVDATA: onverwacht teken in lengte.");
            return false;
        }
    }
    if (!haveLength || actual == 0)
    {
        Serial.println("CIPRECVDATA: lege lengte.");
        return false;
    }

    if (httpLink < 0) httpLink = link;
    if (httpLink != link) clearHttp();
    httpLink = link;
    for (uint32_t i = 0; i < actual; ++i)
    {
        if (!readUartByte(value, 2000))
        {
            Serial.printf("CIPRECVDATA: datatimeout op %lu/%lu.\r\n",
                          (unsigned long)i, (unsigned long)actual);
            return false;
        }
        if (httpLength + 1 >= sizeof(httpRequest))
        {
            Serial.println("CIPRECVDATA: HTTP-buffer vol.");
            return false;
        }
        httpRequest[httpLength++] = value;
    }
    if (!waitForToken("\r\nOK\r\n", 3000))
    {
        Serial.println("CIPRECVDATA: afsluitende OK ontbreekt.");
        return false;
    }
    return true;
}

static bool receivePassivePayload(int8_t link, uint32_t available)
{
    while (available > 0)
    {
        // RUI 4.2.4 exposes a short Serial1 receive ring. Keep the complete
        // +CIPRECVDATA header, payload and trailer comfortably below it.
        const uint32_t requested = available > 64UL ? 64UL : available;
        uint32_t actual = 0;
        if (!receivePassiveChunk(link, requested, actual) || actual == 0)
            return false;
        available -= actual;
        inspectRequestCompletion();
    }
    return true;
}

static void appendAtEscaped(String &target, const char *value)
{
    for (size_t i = 0; value[i] != '\0'; ++i)
    {
        if (value[i] == '\\' || value[i] == '"' || value[i] == ',')
            target += '\\';
        target += value[i];
    }
}

static bool sendRaw(int8_t link, const char *data, size_t length)
{
    if (link < 0 || data == nullptr || length == 0) return false;
    String command("AT+CIPSEND=");
    command += String(link);
    command += ',';
    command += String((unsigned int)length);
    drainUart();
    Serial1.print(command);
    Serial1.print("\r\n");
    Serial1.flush();
    if (!waitForToken(">", 3000)) return false;
    Serial1.write(reinterpret_cast<const uint8_t *>(data), length);
    Serial1.flush();
    return waitForToken("SEND OK", 5000);
}

static bool sendText(int8_t link, const char *text)
{
    return sendRaw(link, text, strlen(text));
}

static String jsonEscape(const char *value)
{
    String result("\"");
    for (size_t i = 0; value[i]; ++i)
    {
        if (value[i] == '\\' || value[i] == '"') result += '\\';
        if ((uint8_t)value[i] >= 0x20) result += value[i];
    }
    result += '"';
    return result;
}

static String hexText(const uint8_t *value, size_t length)
{
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    String result;
    result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i)
    {
        result += HEX_DIGITS[value[i] >> 4];
        result += HEX_DIGITS[value[i] & 0x0F];
    }
    return result;
}

static bool sendPage(int8_t link)
{
#if LEGACY_BLE_AT
    String header("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Encoding: gzip\r\nCache-Control: no-store\r\nConnection: keep-alive\r\nContent-Length: ");
    header += String(sizeof(PORTAL_GZIP));
    header += "\r\n\r\n";
    bool ok = sendText(link, header.c_str());
    for (size_t offset = 0; ok && offset < sizeof(PORTAL_GZIP); offset += 1024)
    {
        const size_t left = sizeof(PORTAL_GZIP) - offset;
        ok = sendRaw(link, reinterpret_cast<const char *>(PORTAL_GZIP + offset), left > 1024 ? 1024 : left);
    }
    // Keep the socket open for /config and /save. With Content-Length a
    // browser can finish receiving before SEND OK reaches the STM32. Closing
    // by numeric link ID at that point can close a newly reused connection.
    // ESP's 30-second idle timeout reclaims clients that stop responding.
    return ok;
#else
    return false;
#endif
}

#if !LEGACY_BLE_AT
static uint16_t nativeRequestId;
#endif
static void sendJson(int8_t link, bool ok, const String &body)
{
#if LEGACY_BLE_AT
    String header(ok ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n");
    header += "Content-Type: application/json\r\nCache-Control: no-store\r\nConnection: keep-alive\r\nContent-Length: ";
    header += String(body.length());
    header += "\r\n\r\n";
    (void)sendText(link, header.c_str());
    (void)sendText(link, body.c_str());
#else
    companionSendJson(nativeRequestId, ok, body.c_str());
#endif
}

static void sendMessagePage(int8_t link, bool ok, const char *code)
{
    String json("{\"ok\":");
    json += ok ? "true" : "false";
    json += ",\"code\":";
    json += jsonEscape(code);
    json += "}";
    sendJson(link, ok, json);
}

// Keep the repeated JSON construction out of line: flash is tight on STM32.
static __attribute__((noinline)) void appendJsonNumber(String &json, const char *name, uint32_t value, bool signedValue = true)
{
    json += ",\"";
    json += name;
    json += "\":";
    json += signedValue ? String((long)value) : String((unsigned long)value);
}

static void sendConfiguration(int8_t link)
{
    String json;
    json.reserve(900);
    json = "{\"wifi_ssid\":" + jsonEscape(snapshot.wifiApSsid);
    json += ",\"victron_mac\":" + jsonEscape(snapshot.victronMac);
    json += ",\"dev_eui\":\"" + hexText(snapshotDevEui, 8) + "\"";
    json += ",\"join_eui\":\"" + hexText(snapshotJoinEui, 8) + "\"";
    #define FIELD(name, value) appendJsonNumber(json, name, (long)(value))
    FIELD("io_board", snapshot.ioBoard);
    FIELD("wifi_triggers", snapshot.wifiTriggers);
    FIELD("wifi_input_min", snapshot.wifiAfterInputSeconds/60);
    FIELD("wifi_lora_min", snapshot.wifiAfterLoraSeconds/60);
    FIELD("downlink_allowed", snapshot.downlinkAllowed);
    FIELD("function_count",snapshot.functionCount);
    FIELD("rising_fn",snapshot.risingFunction);FIELD("falling_fn",snapshot.fallingFunction);
    FIELD("downlink_functions",snapshot.downlinkFunctions);
    for(unsigned i=0;i<MAX_BLE_FUNCTIONS;++i) {
        const BleFunction &f=snapshot.bleFunctions[i];char key[20],text[41];
        #define FUNCTION_TEXT(suffix,value) snprintf(key,sizeof(key),"fn%u_%s",i+1,suffix); json+=",\"";json+=key;json+="\":";json+=jsonEscape(value)
        FUNCTION_TEXT("name",f.name);
        if(genericGattKind(f.kind))functionHex(f.service,16,text,true);else text[0]=0;FUNCTION_TEXT("service",text);
        if(genericGattKind(f.kind))functionHex(f.characteristic,16,text,true);else text[0]=0;FUNCTION_TEXT("char",text);
        functionHex(f.value,f.valueLength,text);FUNCTION_TEXT("value",text);
        #undef FUNCTION_TEXT
        snprintf(key,sizeof(key),"fn%u_kind",i+1);FIELD(key,f.kind);
    }
    FIELD("language", snapshot.language);
    FIELD("profile", snapshot.deviceProfile);
    FIELD("load_enabled", snapshot.loadOutputEnabled);
    FIELD("address_type", snapshot.victronAddressType);
    FIELD("instance", snapshot.victronDeviceInstance);
    FIELD("smp", snapshot.victronUseSmpPin);
    FIELD("window_min", snapshot.configWindowSeconds / 60);
    FIELD("status_min", snapshot.statusIntervalMinutes);
    FIELD("ble_attempts", snapshot.bleMaxAttempts);
    FIELD("region", snapshot.loraRegion);
    FIELD("class", snapshot.loraClass);
    FIELD("fport", snapshot.loraFport);
    FIELD("subband", snapshot.loraSubband);
    FIELD("adr", snapshot.adrEnabled);
    FIELD("input_enabled", snapshot.inputEnabled);
    FIELD("relay_enabled", snapshot.relayEnabled);
    FIELD("rising_actions", snapshot.risingActions);
    FIELD("falling_actions", snapshot.fallingActions);
    FIELD("relay_pulse_ms", snapshot.relayPulseMs);
    FIELD("rx2_custom", snapshot.rx2Custom);
    FIELD("rx2_freq", snapshot.rx2Frequency);
    FIELD("rx2_dr", snapshot.rx2DataRate);
    FIELD("ble_available", live.bleAvailable);
    FIELD("joined", live.joined);
    FIELD("ble_result", live.bleResult);
    FIELD("revision", runtimeConfigRevision());
    #undef FIELD
    json += "}";
    sendJson(link, true, json);
}

static void sendStatus(int8_t link)
{
    String json("{\"firmware\":\"4.9.0\"");
    #define STATE(name, value) appendJsonNumber(json, name, (uint32_t)(value), false)
    STATE("joined", live.joined);
    STATE("ble_available", live.bleAvailable);
    STATE("ble_result", live.bleResult);
    STATE("load_value", live.loadValue);
    STATE("ble_attempts", live.bleAttempts);
    STATE("ble_pending", live.blePending);
    STATE("input_enabled", snapshot.inputEnabled);
    STATE("input_active", live.inputActive);
    STATE("relay_enabled", snapshot.relayEnabled);
    STATE("relay_on", live.relayOn);
    STATE("relay_pulsing", live.relayPulsing);
    STATE("tx_count", live.txCount);
    STATE("rising_count", live.risingCount);
    STATE("falling_count", live.fallingCount);
    STATE("input_overflow", live.inputOverflow);
    STATE("last_event", live.lastEvent);
    STATE("last_actions", live.lastActions);
    STATE("relay_changed_ms", live.relayChangedAt);
    STATE("uptime_s", activitySeconds());
    STATE("revision", runtimeConfigRevision());
    #undef STATE
    json += "}";
    sendJson(link, true, json);
}

static int hexNibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    value = (char)toupper((unsigned char)value);
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool parseHex(const char *text, uint8_t *output, size_t outputLength)
{
    if (strlen(text) != outputLength * 2) return false;
    for (size_t i = 0; i < outputLength; ++i)
    {
        const int high = hexNibble(text[i * 2]);
        const int low = hexNibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool decodeFormValue(const char *begin, size_t length,
                            char *output, size_t outputCapacity)
{
    if (outputCapacity == 0) return false;
    size_t written = 0;
    for (size_t i = 0; i < length; ++i)
    {
        char value = begin[i];
        if (value == '+') value = ' ';
        else if (value == '%')
        {
            if (i + 2 >= length) return false;
            const int high = hexNibble(begin[i + 1]);
            const int low = hexNibble(begin[i + 2]);
            if (high < 0 || low < 0) return false;
            value = (char)((high << 4) | low);
            i += 2;
        }
        if (value == '\0' || written + 1 >= outputCapacity) return false;
        output[written++] = value;
    }
    output[written] = '\0';
    return true;
}

static bool formValue(const char *body, const char *name,
                      char *output, size_t outputCapacity, bool required)
{
    const size_t nameLength = strlen(name);
    const char *position = body;
    bool found = false;
    if (outputCapacity > 0) output[0] = '\0';
    while (position != nullptr && *position != '\0')
    {
        const char *end = strchr(position, '&');
        if (end == nullptr) end = position + strlen(position);
        const char *equals = (const char *)memchr(position, '=', end - position);
        if (equals != nullptr && (size_t)(equals - position) == nameLength &&
            memcmp(position, name, nameLength) == 0)
        {
            if (found || !decodeFormValue(equals + 1, end - equals - 1, output, outputCapacity)) return false;
            found = true;
        }
        position = (*end == '&') ? end + 1 : nullptr;
    }
    return found || !required;
}

static bool parseUnsigned(const char *text, uint32_t minimum,
                          uint32_t maximum, uint32_t &value)
{
    if (text == nullptr || *text == '\0') return false;
    for (const char *p = text; *p; ++p) if (!isdigit((unsigned char)*p)) return false;
    char *end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed < minimum || parsed > maximum)
        return false;
    value = (uint32_t)parsed;
    return true;
}

static bool parseSigned(const char *text, int minimum, int maximum, int &value)
{
    if (text == nullptr || *text == '\0') return false;
    char *end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < minimum || parsed > maximum)
        return false;
    value = (int)parsed;
    return true;
}

static __attribute__((noinline)) bool readFormNumber(const char *body, const char *name,
                    uint32_t minimum, uint32_t maximum, uint32_t &value)
{
    char text[16];
    return formValue(body, name, text, sizeof(text), true) && parseUnsigned(text, minimum, maximum, value);
}

static bool buildUpdate(const char *body, CompanionConfigRequest &request)
{
    memset(&request, 0, sizeof(request));
    request.requestId = nextRequestId++;
    request.config = snapshot;

    char mac[18], addressTypeText[4], instanceText[4], smpText[3];
    char pin[7], windowText[7], statusText[7], attemptsText[3];
    char ssid[33], wifiPassword[64], devEuiText[17], joinEuiText[17], appKeyText[33];
    if (!formValue(body, "victron_mac", mac, sizeof(mac), true) ||
        !formValue(body, "address_type", addressTypeText, sizeof(addressTypeText), true) ||
        !formValue(body, "instance", instanceText, sizeof(instanceText), true) ||
        !formValue(body, "smp", smpText, sizeof(smpText), true) ||
        !formValue(body, "victron_pin", pin, sizeof(pin), false) ||
        !formValue(body, "window_min", windowText, sizeof(windowText), true) ||
        !formValue(body, "status_min", statusText, sizeof(statusText), true) ||
        !formValue(body, "ble_attempts", attemptsText, sizeof(attemptsText), true) ||
        !formValue(body, "wifi_ssid", ssid, sizeof(ssid), true) ||
        !formValue(body, "wifi_password", wifiPassword, sizeof(wifiPassword), false) ||
        !formValue(body, "dev_eui", devEuiText, sizeof(devEuiText), true) ||
        !formValue(body, "join_eui", joinEuiText, sizeof(joinEuiText), true) ||
        !formValue(body, "app_key", appKeyText, sizeof(appKeyText), false))
        return false;

    int signedValue = 0;
    uint32_t unsignedValue = 0;
    if (!parseSigned(addressTypeText, -1, 1, signedValue)) return false;
    request.config.victronAddressType = (int8_t)signedValue;
    if (!parseUnsigned(instanceText, 0, 23, unsignedValue)) return false;
    request.config.victronDeviceInstance = (uint8_t)unsignedValue;
    if (!parseUnsigned(smpText, 0, 1, unsignedValue)) return false;
    request.config.victronUseSmpPin = (uint8_t)unsignedValue;
    if (!parseUnsigned(windowText, 5, 240, unsignedValue)) return false;
    request.config.configWindowSeconds = unsignedValue * 60UL;
    if (!parseUnsigned(statusText, 1, 1440, unsignedValue)) return false;
    request.config.statusIntervalMinutes = unsignedValue;
    if (!parseUnsigned(attemptsText, 1, 3, unsignedValue)) return false;
    request.config.bleMaxAttempts = (uint8_t)unsignedValue;

    strncpy(request.config.victronMac, mac, sizeof(request.config.victronMac) - 1);
    strncpy(request.config.wifiApSsid, ssid, sizeof(request.config.wifiApSsid) - 1);
    if (pin[0] != '\0')
        strncpy(request.config.victronPin, pin, sizeof(request.config.victronPin) - 1);
    if (wifiPassword[0] != '\0')
        strncpy(request.config.wifiApPassword, wifiPassword,
                sizeof(request.config.wifiApPassword) - 1);

    uint8_t parsed[16] = {0};
    if (!parseHex(devEuiText, parsed, 8)) return false;
    if (memcmp(parsed, snapshotDevEui, 8) != 0)
    {
        memcpy(request.devEui, parsed, 8);
        request.credentialMask |= COMPANION_SET_DEVEUI;
    }
    if (!parseHex(joinEuiText, parsed, 8)) return false;
    if (memcmp(parsed, snapshotJoinEui, 8) != 0)
    {
        memcpy(request.joinEui, parsed, 8);
        request.credentialMask |= COMPANION_SET_JOINEUI;
    }
    if (appKeyText[0] != '\0')
    {
        if (!parseHex(appKeyText, request.appKey, 16)) return false;
        request.credentialMask |= COMPANION_SET_APPKEY;
    }
    char number[16];
    #define READ_NUMBER(name, minValue, maxValue, target) \
        if (!readFormNumber(body, name, minValue, maxValue, unsignedValue)) return false; \
        target = unsignedValue
    READ_NUMBER("schema", 8, 8, unsignedValue);
    READ_NUMBER("expected_revision",0,0xFFFFFFFFUL,unsignedValue);
    if(unsignedValue!=runtimeConfigRevision()) return false;
    READ_NUMBER("language", 0, 1, request.config.language);
    READ_NUMBER("profile", 1, 3, request.config.deviceProfile);
    READ_NUMBER("region", 2, 11, request.config.loraRegion);
    READ_NUMBER("class", 0, 2, request.config.loraClass);
    READ_NUMBER("fport", 1, 223, request.config.loraFport);
    READ_NUMBER("subband", 0, 8, request.config.loraSubband);
    READ_NUMBER("adr", 0, 1, request.config.adrEnabled);
    READ_NUMBER("rx2_custom", 0, 1, request.config.rx2Custom);
    READ_NUMBER("rx2_freq", 100000000, 1000000000, request.config.rx2Frequency);
    READ_NUMBER("rx2_dr", 0, 13, request.config.rx2DataRate);
    READ_NUMBER("rising_actions", 0, 127, request.config.risingActions);
    READ_NUMBER("falling_actions", 0, 127, request.config.fallingActions);
    READ_NUMBER("relay_pulse_ms", 100, 60000, request.config.relayPulseMs);
    READ_NUMBER("io_board",0,2,request.config.ioBoard);
    READ_NUMBER("wifi_triggers",0,7,request.config.wifiTriggers);
    READ_NUMBER("wifi_input_min",1,240,unsignedValue);request.config.wifiAfterInputSeconds=unsignedValue*60;
    READ_NUMBER("wifi_lora_min",1,240,unsignedValue);request.config.wifiAfterLoraSeconds=unsignedValue*60;
    READ_NUMBER("downlink_allowed",0,31,request.config.downlinkAllowed);
    READ_NUMBER("function_count",1,MAX_BLE_FUNCTIONS,request.config.functionCount);
    READ_NUMBER("rising_fn",0,MAX_BLE_FUNCTIONS,request.config.risingFunction);
    READ_NUMBER("falling_fn",0,MAX_BLE_FUNCTIONS,request.config.fallingFunction);
    READ_NUMBER("downlink_functions",0,1023,request.config.downlinkFunctions);
    for(unsigned i=0;i<MAX_BLE_FUNCTIONS;++i) {
        BleFunction &f=request.config.bleFunctions[i];char key[20],service[37],characteristic[37],value[41];
        #define READ_FUNCTION(suffix,value) snprintf(key,sizeof(key),"fn%u_%s",i+1,suffix); if(!formValue(body,key,value,sizeof(value),false))return false
        READ_FUNCTION("name",f.name);READ_FUNCTION("service",service);
        READ_FUNCTION("char",characteristic);READ_FUNCTION("value",value);
        if(!functionSetText(f,service,characteristic,value))return false;
        #undef READ_FUNCTION
        snprintf(key,sizeof(key),"fn%u_kind",i+1);READ_NUMBER(key,0,12,f.kind);
    }
    #undef READ_NUMBER
    if (!formValue(body, "load_enabled", number, sizeof(number), false)) return false;
    if (number[0] && strcmp(number, "1") != 0) return false;
    request.config.loadOutputEnabled = number[0] ? 1 : 0;
    if (!formValue(body, "input_enabled", number, sizeof(number), false)) return false;
    if (number[0] && strcmp(number, "1") != 0) return false;
    request.config.inputEnabled = number[0] ? 1 : 0;
    if (!formValue(body, "relay_enabled", number, sizeof(number), false)) return false;
    if (number[0] && strcmp(number, "1") != 0) return false;
    request.config.relayEnabled = number[0] ? 1 : 0;
    return runtimeConfigValid(request.config);
}

static bool headerEquals(const char *request, const char *name, const char *expected)
{
    const char *line = strstr(request, "\r\n");
    const size_t n = strlen(name);
    while (line && line[2] && line[2] != '\r')
    {
        line += 2;
        const char *end = strstr(line, "\r\n");
        if (!end) return false;
        if ((size_t)(end-line) > n && strncasecmp(line, name, n) == 0 && line[n] == ':')
        {
            const char *value = line + n + 1;
            while (*value == ' ' || *value == '\t') ++value;
            return (size_t)(end-value) == strlen(expected) && strncmp(value, expected, end-value) == 0;
        }
        line = end;
    }
    return false;
}

static int contentLength(const char *request)
{
    const char *line = strstr(request, "\r\n");
    int result = -1;
    while (line && line[2] && line[2] != '\r')
    {
        line += 2;
        const char *end = strstr(line, "\r\n");
        if (!end) return -1;
        if (strncasecmp(line, "Content-Length:", 15) == 0)
        {
            if (result >= 0) return -1;
            line += 15;
            while (*line == ' ' || *line == '\t') ++line;
            if (line == end) return -1;
            unsigned value = 0;
            while (line < end)
            {
                if (!isdigit((unsigned char)*line)) return -1;
                value = value * 10 + (*line++ - '0');
                if (value > 2400) return -1;
            }
            result = (int)value;
        }
        line = end;
    }
    return result;
}

static void inspectRequestCompletion()
{
    if (requestReady || httpLength == 0) return;
    httpRequest[httpLength] = '\0';
    const char *headerEnd = strstr(httpRequest, "\r\n\r\n");
    if (headerEnd == nullptr) return;
    if (strncmp(httpRequest, "POST ", 5) == 0)
    {
        const int bodyLength = contentLength(httpRequest);
        if (bodyLength < 0)
        {
            requestReady = true;
            return;
        }
        const size_t headerBytes = (size_t)(headerEnd - httpRequest) + 4;
        if (httpLength < headerBytes + (size_t)bodyLength)
        {
            return;
        }
    }
    requestReady = true;
}

static void handleRequest()
{
    if (!requestReady || updatePending || httpLink < 0) return;
    const int8_t link = httpLink;
    httpRequest[httpLength] = '\0';
    if (strncmp(httpRequest, "GET / ", 6) == 0)
    {
        paused = true;
        (void)sendPage(link);
        paused = false;
        resetParser();
        return;
    }
    if (strncmp(httpRequest, "GET /config ", 12) == 0)
    {
        paused = true;
        sendConfiguration(link);
        paused = false;
        resetParser();
        return;
    }
    if (strncmp(httpRequest,"GET /log ",9)==0) {
        sendJson(link,true,activityJson());resetParser();return;
    }
    if (strncmp(httpRequest, "GET /status ", 12) == 0)
    {
        paused = true;
        sendStatus(link);
        paused = false;
        resetParser();
        return;
    }
    if (strncmp(httpRequest, "POST /action ", 13) == 0)
    {
        const char *body = strstr(httpRequest, "\r\n\r\n");
        if (body) body += 4;
        char command[24] = {0};
        uint8_t action = 0;
        if (!pendingAction && body && contentLength(httpRequest) == (int)strlen(body) &&
            headerEquals(httpRequest, "X-LoRaBLE", "1") &&
            headerEquals(httpRequest, "Content-Type", "application/x-www-form-urlencoded") &&
            formValue(body, "command", command, sizeof(command), true))
        {
            if (!strcmp(command, "test_rising") && snapshot.inputEnabled) action = 1;
            if (!strcmp(command, "test_falling") && snapshot.inputEnabled) action = 2;
            if (!strcmp(command, "relay_pulse") && snapshot.relayEnabled) action = 3;
            if (!strcmp(command, "relay_off") && snapshot.relayEnabled) action = 4;
            if (!strcmp(command, "uplink")) action = 5;
#if !LEGACY_BLE_AT
            if(!strcmp(command,"ble_scan")) action=6;
            for(unsigned i=0;i<snapshot.functionCount;++i) {
                char expected[16];snprintf(expected,sizeof(expected),"ble_fn%u",i+1);
                if(!strcmp(command,expected) && snapshot.loadOutputEnabled && snapshot.bleFunctions[i].kind) action=7+i;
            }
#endif
        }
        pendingAction = action;
        paused = true;
        sendMessagePage(link, action != 0, action ? "queued" : "action_unavailable");
        paused = false;
        resetParser();
        return;
    }
    if (strncmp(httpRequest, "POST /save ", 11) == 0)
    {
        char *body = strstr(httpRequest, "\r\n\r\n");
        if (body != nullptr) body += 4;
        if (body != nullptr && contentLength(httpRequest) == (int)strlen(body) &&
            headerEquals(httpRequest, "X-LoRaBLE", "1") &&
            headerEquals(httpRequest, "Content-Type", "application/x-www-form-urlencoded") &&
            buildUpdate(body, pendingUpdate))
        {
            updatePending = true;
            responseLink = link;
            responseRequestId = pendingUpdate.requestId;
            clearHttp();
            return;
        }
        paused = true;
        sendMessagePage(link, false, "invalid_settings");
        paused = false;
        resetParser();
        return;
    }
    paused = true;
    sendMessagePage(link, false, "not_found");
    paused = false;
    resetParser();
}

static void consumeUartByte(char value)
{
    static const char PREFIX[] = "+IPD,";
    switch (ipdState)
    {
    case IPD_SEEK:
        if (value == PREFIX[seekMatched])
        {
            ++seekMatched;
            if (seekMatched == sizeof(PREFIX) - 1)
            {
                ipdState = IPD_LINK;
                ipdLink = -1;
                seekMatched = 0;
            }
        }
        else seekMatched = (value == PREFIX[0]) ? 1 : 0;
        break;
    case IPD_LINK:
        if (isdigit((unsigned char)value))
        {
            const int digit = value - '0';
            ipdLink = (ipdLink < 0) ? digit : (int8_t)(ipdLink * 10 + digit);
        }
        else if (value == ',' && ipdLink >= 0 && ipdLink < 5)
        {
            ipdState = IPD_LENGTH;
            ipdLength = 0;
        }
        else resetParser();
        break;
    case IPD_LENGTH:
        if (isdigit((unsigned char)value))
        {
            ipdLength = ipdLength * 10UL + (uint32_t)(value - '0');
            if (ipdLength > 4096UL) resetParser();
        }
        else if (value == ':' && ipdLength > 0)
        {
            ipdState = IPD_DATA;
            ipdReceived = 0;
            if (httpLink < 0) httpLink = ipdLink;
            if (httpLink != ipdLink) clearHttp();
            httpLink = ipdLink;
        }
        else if (value == ',' && ipdLength > 0)
        {
            // AT+CIPDINFO=1 adds remote IP and port before the colon. Accept
            // that format too, even though startup requests CIPDINFO=0.
            ipdState = IPD_META;
        }
        else if (value == '\r' && ipdLength > 0)
        {
            // Passive receive mode announces only link + buffered length. Pull
            // it in small pieces so RUI's short Serial1 ring cannot overflow.
            const int8_t link = ipdLink;
            const uint32_t available = ipdLength;
            bufferedRequests[link] = 0;
            ipdState = IPD_SEEK;
            ipdLength = 0;
            ipdReceived = 0;
            if (!receivePassivePayload(link, available))
            {
                Serial.println("Passieve HTTP-data kon niet volledig worden gelezen.");
                clearHttp();
            }
        }
        else resetParser();
        break;
    case IPD_META:
        if (value == ':')
        {
            ipdState = IPD_DATA;
            ipdReceived = 0;
            if (httpLink < 0) httpLink = ipdLink;
            if (httpLink != ipdLink) clearHttp();
            httpLink = ipdLink;
        }
        break;
    case IPD_DATA:
        ++ipdReceived;
        if (httpLength + 1 < sizeof(httpRequest))
            httpRequest[httpLength++] = value;
        else
            clearHttp();
        if (ipdReceived >= ipdLength)
        {
            ipdState = IPD_SEEK;
            ipdLength = 0;
            ipdReceived = 0;
            inspectRequestCompletion();
        }
        break;
    }
}
} // namespace

bool legacyPortalStart(const RuntimeConfig &config,
                       const uint8_t devEui[8], const uint8_t joinEui[8])
{
    legacyPortalUpdateSnapshot(config, devEui, joinEui);
    paused = true;
    resetParser();

    bool modeOk = atCommand("AT+CWMODE=2,0", 4000);
    if (!modeOk) modeOk = atCommand("AT+CWMODE=2", 4000);

    String apCommand("AT+CWSAP=\"");
    appendAtEscaped(apCommand, config.wifiApSsid);
    apCommand += F("\",\"");
    appendAtEscaped(apCommand, config.wifiApPassword);
    apCommand += F("\",6,3,2,0");

    const bool apOk = modeOk && atCommand(apCommand, 5000);
    (void)atCommand("AT+CIPSERVER=0", 2500);
    const bool muxOk = apOk && atCommand("AT+CIPMUX=1", 3000);
    const bool receiveOk = muxOk && atCommand("AT+CIPRECVMODE=1", 3000);
    (void)atCommand("AT+CIPDINFO=0", 3000);
    const bool serverOk = receiveOk && atCommand("AT+CIPSERVER=1,80", 5000);
    if (serverOk) (void)atCommand("AT+CIPSTO=30", 3000);

    running = serverOk;
    paused = false;
    drainUart();
    resetParser();
    return running;
}

void legacyPortalStop()
{
    paused = true;
    if (running) (void)atCommand("AT+CIPSERVER=0", 3000);
    running = false;
    updatePending = false;
    responseLink = -1;
    responseRequestId = 0;
    memset(&pendingUpdate, 0, sizeof(pendingUpdate));
    resetParser();
}

void legacyPortalPause()
{
    paused = true;
    resetParser();
    drainUart();
}

void legacyPortalResume()
{
    drainUart();
    resetParser();
    paused = false;
}

void legacyPortalService()
{
    if (!running || paused || updatePending) return;
    for (uint8_t link = 0; link < 5 && !requestReady; ++link)
    {
        if (!bufferedRequests[link]) continue;
        const uint32_t available = bufferedRequests[link];
        bufferedRequests[link] = 0;
        if (!receivePassivePayload(link, available)) clearHttp();
    }
    while (Serial1.available() > 0 && !requestReady)
    {
        const int value = Serial1.read();
        if (value >= 0) consumeUartByte((char)value);
    }
    handleRequest();
}

bool legacyPortalIsRunning()
{
    return running;
}

bool legacyPortalTakeConfigRequest(CompanionConfigRequest &request)
{
    if (!updatePending) return false;
    request = pendingUpdate;
    updatePending = false;
    memset(&pendingUpdate, 0, sizeof(pendingUpdate));
    return true;
}

void legacyPortalConfigResult(uint16_t requestId, bool ok,
                              uint32_t revision, const char *code)
{
    (void)revision;
    if (responseLink < 0 || requestId != responseRequestId) return;
    paused = true;
    if (ok)
        sendMessagePage(responseLink, true, "saved");
    else if (code != nullptr && strcmp(code, "lorawan_update_pending") == 0)
        sendMessagePage(responseLink, true, "saved_pending");
    else
        sendMessagePage(responseLink, false, "save_failed");
    responseLink = -1;
    responseRequestId = 0;
    resetParser();
    paused = false;
}

void legacyPortalUpdateSnapshot(const RuntimeConfig &config,
                                const uint8_t devEui[8], const uint8_t joinEui[8])
{
    snapshotPointer = &config;
    memcpy(snapshotDevEui, devEui, sizeof(snapshotDevEui));
    memcpy(snapshotJoinEui, joinEui, sizeof(snapshotJoinEui));
}

void legacyPortalSetStatus(const PortalLiveStatus &status)
{
    live = status;
}

bool legacyPortalTakeAction(uint8_t &action)
{
    if (!pendingAction) return false;
    action = pendingAction;
    pendingAction = 0;
    return true;
}

#if !LEGACY_BLE_AT
void portalNativeFrame(const char *type, uint16_t id, const char *payload)
{
    static char path[24];
    static size_t bodyUsed;
    static uint32_t started;
    if (!strcmp(type, "HTTP_BEGIN")) {
        if (!id || updatePending || responseLink >= 0) return;
        nativeRequestId = id;
        bodyUsed = 0;
        started = millis();
        snprintf(path, sizeof(path), "%s", payload);
        clearHttp();
        return;
    }
    if (!id || id != nativeRequestId || millis() - started > 10000) return;
    if (!strcmp(type, "HTTP_DATA")) {
        char *end;
        const unsigned long offset = strtoul(payload, &end, 10);
        const size_t n = strlen(end[0] == ':' ? end + 1 : "");
        if (end == payload || *end != ':' || offset != bodyUsed ||
            bodyUsed + n > 2400) {nativeRequestId = 0; return;}
        memcpy(httpRequest + bodyUsed, end + 1, n);
        bodyUsed += n;
        httpRequest[bodyUsed] = 0;
    } else if (!strcmp(type, "HTTP_END")) {
        char *end;
        const unsigned long expected = strtoul(payload, &end, 10);
        if (end == payload || *end || expected != bodyUsed) {nativeRequestId=0;return;}
        char header[200];
        const int n = snprintf(header, sizeof(header),
            "%s HTTP/1.1\r\nContent-Length: %u\r\nX-LoRaBLE: 1\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n\r\n",
            path, (unsigned)bodyUsed);
        if (n < 0 || n >= (int)sizeof(header)) {nativeRequestId=0;return;}
        memmove(httpRequest+n, httpRequest, bodyUsed+1);
        memcpy(httpRequest, header, n);
        httpLength = n + bodyUsed;
        httpLink = 0;
        requestReady = true;
        handleRequest();
    }
}
#endif
