/*
 * RAK11160/RAK11162 + RAK13001 -> LoRaWAN, input/relay and SmartSolar LOAD
 * Victron LoRaBLE Remote v4.8, 2026-09-16
 *
 * Triggers:
 *   - new active edge on the isolated RAK13001 input (WB_IO3, active LOW)
 *   - LoRaWAN Class-A downlink on FPort 10: 0x01, "ON" or "BEEP"
 *
 * Safety:
 *   - MPPT: only VREG 0xEDAB is writable; Generic uses configured GATT UUID
 *   - MPPT mode 0..7 accepted; upper EDAB bits preserved
 *   - the register is read before and after the write
 *   - one action per contact activation; a continuously active input cannot repeat
 *
 * LEGACY_BLE_AT=1 uses ESP-AT. Stock WiFi firmware may lack BLE support.
 * LEGACY_BLE_AT=0 is for the matching native ESP8684 companion.
 * Fill in settings.h before uploading this sketch.
 */

#include "settings.h"
#include "config_store.h"
#include "region_profile.h"
#include "network_manager.h"
#include "action_rules.h"
#include "victron_result_codes.h"
#include "esp_companion.h"
#include "esp_migration.h"
#include "activity_log.h"
#include "legacy_at_portal.h"
#include <board.h>
#include "stm32_timer.h"
#include <ctype.h>
#include <string.h>

#define FW_MAJOR 4
#define FW_MINOR 10

#if LEGACY_BLE_AT != 0 && LEGACY_BLE_AT != 1
#error "LEGACY_BLE_AT must be 0 or 1"
#endif

static const uint16_t VIC_LOAD_CONTROL_VREG = 0xEDAB;
static const uint8_t VIC_LOAD_ALWAYS_ON = 0x04;
#if LEGACY_BLE_AT
static const uint8_t BLE_CONN_INDEX = 0;
static const size_t ESP_RX_CAPACITY = 2048;
#endif

#if LEGACY_BLE_AT
static uint8_t espRx[ESP_RX_CAPACITY];
static size_t espRxLen = 0;
#endif

static volatile bool pendingAlwaysOn = false;
static uint8_t pendingLoadValue = 0;
static bool pendingBleReport = false;
static bool bleBusy = false;
static volatile bool relayOn = false;
static volatile bool relayPulseActive = false;
static bool relayTimerReady = false;
static volatile bool relayReportOnExpiry = false;
static volatile uint32_t relayChangedAt = 0;
static volatile bool relayExpiredEvent = false;
static UTIL_TIMER_Object_t relayTimer;
static volatile uint32_t txCount = 0;
static volatile uint32_t risingCount = 0, fallingCount = 0;
static volatile uint32_t inputOverflow = 0;
static volatile uint8_t edgeQueue[16], edgeWrite = 0, edgeRead = 0;
static UTIL_TIMER_Object_t inputTimer;
static bool inputTimerReady = false;
static uint8_t lastEvent = 0, lastActions = 0;
static volatile uint8_t remoteActionQueue[4],remoteFunctionQueue[4];
static volatile uint8_t remoteWrite = 0, remoteRead = 0;
static volatile bool statusTxInFlight = false;
static volatile bool healthTxInFlight = false;
static volatile bool lastKnownJoined = false;
static volatile uint32_t statusRevision = 0;
static volatile uint32_t statusRevisionInFlight = 0;
static volatile uint32_t statusRevisionAcked = 0;
static volatile uint32_t busyEventRevision = 0;
static volatile uint32_t busyEventAcked = 0;
static volatile uint32_t busyEventInFlight = 0;
static volatile bool busyStatusInFlight = false;
static bool lorawanConfigured = false;
static volatile int lastRawContact = HIGH;
static volatile int stableContact = HIGH;
static volatile uint32_t rawContactChangedAt = 0;
static volatile uint32_t lastStatusAt = 0;
static uint32_t lastStatusAttemptAt = 0;
static uint32_t lastJoinAttemptAt = 0;
static bool haveDownlinkCounter = false;
static uint32_t lastDownlinkCounter = 0;

static uint8_t lastBleResult = BLE_NOT_RUN;
static uint8_t lastBleAttempts = 0;
static uint8_t lastLoadValue = 0xFF;
static RuntimeConfig runtimeConfig;
static uint32_t configWindowDeadline = 0;
static bool lastWifiWanted = false;
static uint32_t pendingRebootAt = 0;
#if LEGACY_BLE_AT
static bool legacyEspPowered = false;
static bool legacyBleAtAvailable = false;
static uint32_t legacyPortalRetryAt = 0;
#endif

static uint8_t nodeDevEui[8] = LORAWAN_DEVEUI;
static uint8_t nodeAppEui[8] = LORAWAN_APPEUI;
static uint8_t nodeAppKey[16] = LORAWAN_APPKEY;

static const uint32_t STATUS_RETRY_MS = 10000UL;

static bool deadlinePending(uint32_t now, uint32_t deadline)
{
    return (int32_t)(deadline - now) > 0;
}

static void startConfigWindow(uint32_t now)
{
    configWindowDeadline = now + runtimeConfig.configWindowSeconds * 1000UL;
}

static void extendConfigWindow(uint32_t now,uint32_t seconds)
{
    const uint32_t proposed=now+seconds*1000UL;
    if(!deadlinePending(now,configWindowDeadline) || (int32_t)(proposed-configWindowDeadline)>0)
        configWindowDeadline=proposed;
}

static bool configWindowPending(uint32_t now)
{
    return deadlinePending(now, configWindowDeadline);
}

static bool wifiWanted(uint32_t now)
{
    return (runtimeConfig.inputEnabled && (runtimeConfig.wifiTriggers & 2) && stableContact == CONTACT_ACTIVE_LEVEL) || configWindowPending(now);
}

static uint32_t configSecondsRemaining(uint32_t now)
{
    if (!configWindowPending(now)) return 0;
    return (configWindowDeadline - now + 999UL) / 1000UL;
}

static void requestStatusUplink()
{
    ++statusRevision;
}

static void relayPulseExpired(void *context)
{
    (void)context;
    if (!relayPulseActive) return;
    digitalWrite(RELAY_OUTPUT_PIN, LOW);
    relayOn = false;
    relayPulseActive = false;
    relayChangedAt = millis();
    relayExpiredEvent = true;
}

static bool setRelay(uint8_t action, bool report)
{
    if (!runtimeConfig.relayEnabled) return false;
    if (action != ACTION_RELAY_ON && action != ACTION_RELAY_OFF && action != ACTION_RELAY_PULSE) return false;
    if (relayTimerReady) UTIL_TIMER_Stop(&relayTimer);
    relayPulseActive = false;
    relayReportOnExpiry = report;
    if (action == ACTION_RELAY_PULSE)
    {
        relayPulseActive = true;
        if (!relayTimerReady || UTIL_TIMER_SetPeriod(&relayTimer, runtimeConfig.relayPulseMs) != UTIL_TIMER_OK ||
            UTIL_TIMER_Start(&relayTimer) != UTIL_TIMER_OK)
        {
            relayPulseActive = false;
            digitalWrite(RELAY_OUTPUT_PIN, LOW);
            relayOn = false;
            return false;
        }
    }
    relayOn = action != ACTION_RELAY_OFF;
    digitalWrite(RELAY_OUTPUT_PIN, relayOn ? HIGH : LOW);
    relayChangedAt = millis();
    activityAdd(relayOn?11:12);
    if (report) requestStatusUplink();
    return true;
}

static void executeActions(uint8_t actions, uint8_t event, uint8_t functionIndex=255)
{
    activityAdd(event+1,actions);
    lastEvent = event;
    lastActions = actions;
    const bool report = (actions & ACTION_UPLINK) != 0;
    if (actions & (ACTION_RELAY_ON | ACTION_RELAY_OFF | ACTION_RELAY_PULSE))
        setRelay(actions & (ACTION_RELAY_ON | ACTION_RELAY_OFF | ACTION_RELAY_PULSE), report);
    if ((actions & (ACTION_LOAD_ON | ACTION_LOAD_OFF)) && runtimeConfig.loadOutputEnabled)
    {
        if (pendingAlwaysOn || bleBusy)
        {
            if (report) ++busyEventRevision;
        }
        else
        {
            const uint8_t fn=functionIndex==255?((actions & ACTION_LOAD_ON)?0:1):functionIndex;
            if(fn>=runtimeConfig.functionCount || !runtimeConfig.bleFunctions[fn].kind) {if(report)requestStatusUplink();return;}
            pendingLoadValue = fn; // Function index; companion resolves its saved recipe.
            pendingBleReport = report;
            pendingAlwaysOn = true;
            lastBleResult = BLE_NOT_RUN;
            lastBleAttempts = 0;
            lastLoadValue = 0xFF;
        }
    }
    if (report) requestStatusUplink();
}

static void executeInputEdge(bool rising, bool simulated)
{
    if (!runtimeConfig.inputEnabled) return;
    uint8_t actions = enabledInputActions(rising ? runtimeConfig.risingActions : runtimeConfig.fallingActions,
                        true, runtimeConfig.loadOutputEnabled, runtimeConfig.relayEnabled);
    const uint8_t fn=rising?runtimeConfig.risingFunction:runtimeConfig.fallingFunction;
    actions &= ~(ACTION_LOAD_ON|ACTION_LOAD_OFF);
    if(fn && runtimeConfig.loadOutputEnabled) actions|=fn==2?ACTION_LOAD_OFF:ACTION_LOAD_ON;
    if(runtimeConfig.wifiTriggers & 1) extendConfigWindow(millis(),runtimeConfig.wifiAfterInputSeconds);
    executeActions(actions, simulated ? (rising ? 4 : 5) : (rising ? 1 : 2),fn?fn-1:255);
}

static bool statusUplinkNeeded()
{
    return statusRevision != statusRevisionAcked ||
           busyEventRevision != busyEventAcked;
}

#if LEGACY_BLE_AT
static void resetEspRx()
{
    espRxLen = 0;
    memset(espRx, 0, sizeof(espRx));
}

static void drainEspUart()
{
    while (Serial1.available() > 0)
    {
        (void)Serial1.read();
    }
}

static bool rxContains(const char *needle)
{
    const size_t needleLen = strlen(needle);
    if (needleLen == 0 || espRxLen < needleLen)
    {
        return false;
    }

    for (size_t i = 0; i <= espRxLen - needleLen; ++i)
    {
        if (memcmp(&espRx[i], needle, needleLen) == 0)
        {
            return true;
        }
    }
    return false;
}

static void readEspAvailable()
{
    while (Serial1.available() > 0)
    {
        const int value = Serial1.read();
        if (value >= 0 && espRxLen < ESP_RX_CAPACITY)
        {
            espRx[espRxLen++] = (uint8_t)value;
        }
    }
}

static bool waitEspToken(const char *wanted, uint32_t timeoutMs, bool failOnError = true)
{
    const uint32_t started = millis();
    while ((uint32_t)(millis() - started) < timeoutMs)
    {
        readEspAvailable();
        if (rxContains(wanted))
        {
            return true;
        }
        if (failOnError && rxContains("\r\nERROR\r\n"))
        {
            return false;
        }
        delay(2);
    }
    readEspAvailable();
    return rxContains(wanted);
}

static void collectEspFor(uint32_t durationMs)
{
    const uint32_t started = millis();
    while ((uint32_t)(millis() - started) < durationMs)
    {
        readEspAvailable();
        delay(2);
    }
    readEspAvailable();
}

static String espRxText()
{
    String result;
    result.reserve(espRxLen + 1);
    for (size_t i = 0; i < espRxLen; ++i)
    {
        if (espRx[i] != 0)
        {
            result += (char)espRx[i];
        }
    }
    return result;
}

static bool sendAt(const String &command, uint32_t timeoutMs = 5000)
{
    drainEspUart();
    resetEspRx();
    Serial1.print(command);
    Serial1.print("\r\n");
    Serial1.flush();
    return waitEspToken("\r\nOK\r\n", timeoutMs);
}

static bool restartLegacyEspAt()
{
    drainEspUart();
    resetEspRx();
    Serial1.print("AT+RST\r\n");
    Serial1.flush();
    if (!waitEspToken("ready", 8000, false)) return false;
    return sendAt("AT", 2500);
}

static bool ensureLegacyEspPowered()
{
    setCurrentATMode(LORA_AT_MODE);
    if (legacyEspPowered && sendAt("AT", 2000))
    {
        return true;
    }

    if (legacyEspPowered)
    {
        legacyPortalStop();
        setEspPowerMode(POWER_OFF);
        legacyEspPowered = false;
        delay(250);
    }

    drainEspUart();
    resetEspRx();
    setEspPowerMode(POWER_ON);
    legacyEspPowered = true;
    (void)waitEspToken("ready", 5000, false);
    for (uint8_t attempt = 0; attempt < 3; ++attempt)
    {
        if (sendAt("AT", 2000)) return true;
        delay(250);
    }

    legacyPortalStop();
    setEspPowerMode(POWER_OFF);
    legacyEspPowered = false;
    return false;
}

static void powerOffLegacyEsp()
{
    legacyPortalStop();
    if (legacyEspPowered)
    {
        (void)sendAt("AT+CWMODE=0,0", 2500);
        setEspPowerMode(POWER_OFF);
    }
    legacyEspPowered = false;
}

static void releaseLegacyEspAfterBle()
{
    if (wifiWanted(millis()))
    {
        (void)sendAt("AT+CWINIT=1", 5000);
        if (!legacyPortalIsRunning() &&
            !legacyPortalStart(runtimeConfig, nodeDevEui, nodeAppEui))
        {
            legacyPortalRetryAt = millis() + 10000UL;
            Serial.println("Instellingen-WiFi kon na BLE nog niet worden hervat.");
        }
    }
    else
    {
        powerOffLegacyEsp();
    }
}

static bool enterLegacyBleRadioMode()
{
    // This ESP32-C2 image cannot keep WiFi and BLE initialized together.
    // Drop the server/AP briefly; the same secured portal is restored later.
    legacyPortalStop();
    bool nullMode = sendAt("AT+CWMODE=0,0", 4000);
    if (!nullMode) nullMode = sendAt("AT+CWMODE=0", 4000);
    if (!nullMode)
    {
        Serial.println("ESP-omschakeling: WiFi null-modus geweigerd.");
        return false;
    }
    // Clear the ESP32-C2 heap after the web server was active. The STM32 and
    // LoRaWAN session keep running while only the ESP AT coprocessor restarts.
    if (!restartLegacyEspAt())
    {
        Serial.println("ESP-omschakeling: ESP-AT herstart mislukt.");
        return false;
    }
    nullMode = sendAt("AT+CWMODE=0,0", 4000);
    if (!nullMode) nullMode = sendAt("AT+CWMODE=0", 4000);
    if (!nullMode) return false;
    const bool driverOff = sendAt("AT+CWINIT=0", 5000);
    if (!driverOff)
    {
        Serial.print("ESP-omschakeling: WiFi-driver bleef actief: ");
        Serial.println(espRxText());
    }
    return driverOff;
}

static bool writeGatt(uint8_t serviceIndex, uint8_t characteristicIndex,
                      int descriptorIndex, const uint8_t *data, size_t length,
                      uint32_t timeoutMs = 5000)
{
    drainEspUart();
    resetEspRx();

    if (descriptorIndex >= 0)
    {
        Serial1.printf("AT+BLEGATTCWR=%u,%u,%u,%d,%u\r\n",
                       BLE_CONN_INDEX, serviceIndex, characteristicIndex,
                       descriptorIndex, (unsigned int)length);
    }
    else
    {
        Serial1.printf("AT+BLEGATTCWR=%u,%u,%u,,%u\r\n",
                       BLE_CONN_INDEX, serviceIndex, characteristicIndex,
                       (unsigned int)length);
    }
    Serial1.flush();

    if (!waitEspToken(">", timeoutMs))
    {
        return false;
    }
    Serial1.write(data, length);
    Serial1.flush();
    return waitEspToken("\r\nOK\r\n", timeoutMs);
}

static int csvInteger(const String &line, int field)
{
    int start = 0;
    for (int current = 0; current < field; ++current)
    {
        start = line.indexOf(',', start);
        if (start < 0)
        {
            return -1;
        }
        ++start;
    }

    int end = line.indexOf(',', start);
    if (end < 0)
    {
        end = line.length();
    }
    String token = line.substring(start, end);
    token.trim();
    return token.toInt();
}

static String lineContaining(const String &haystack, const String &needle, int from = 0)
{
    const int found = haystack.indexOf(needle, from);
    if (found < 0)
    {
        return String();
    }

    int start = haystack.lastIndexOf('\n', found);
    start = (start < 0) ? 0 : start + 1;
    int end = haystack.indexOf('\n', found);
    if (end < 0)
    {
        end = haystack.length();
    }
    String line = haystack.substring(start, end);
    line.trim();
    return line;
}

static int scanVictronAddressType()
{
    String command = "AT+BLESCAN=1,4,1,\"";
    command += runtimeConfig.victronMac;
    command += "\",1";

    // During a timed scan ESP-AT may print OK either before or after the
    // scan results. BLESCANDONE is therefore the reliable completion marker.
    drainEspUart();
    resetEspRx();
    Serial1.print(command);
    Serial1.print("\r\n");
    Serial1.flush();
    if (!waitEspToken("+BLESCANDONE", 7000))
    {
        return -1;
    }

    String response = espRxText();
    response.toLowerCase();
    String mac = runtimeConfig.victronMac;
    mac.toLowerCase();
    String line = lineContaining(response, mac);
    if (line.length() == 0)
    {
        return -1;
    }
    const int comma = line.lastIndexOf(',');
    if (comma < 0)
    {
        return -1;
    }
    return line.substring(comma + 1).toInt();
}

static bool discoverVictronGatt(uint8_t &serviceIndex,
                                uint8_t &controlChar, uint8_t &lastDataChar,
                                uint8_t &dataChar, int &controlCccd,
                                int &lastDataCccd, int &dataCccd)
{
    if (!sendAt("AT+BLEGATTCPRIMSRV=0", 12000))
    {
        return false;
    }

    String services = espRxText();
    services.toLowerCase();
    String serviceLine = lineContaining(services, "306b0001-b081-4037-83dc-e59fcc3cdfd0");
    if (serviceLine.length() == 0)
    {
        serviceLine = lineContaining(services, "306b0001-b081-4037-83dc-e59fcc3cdfd1");
    }
    const int parsedService = csvInteger(serviceLine, 1);
    if (parsedService <= 0 || parsedService > 255)
    {
        return false;
    }
    serviceIndex = (uint8_t)parsedService;

    String command = "AT+BLEGATTCCHAR=0,";
    command += String(serviceIndex);
    if (!sendAt(command, 12000))
    {
        return false;
    }

    String chars = espRxText();
    chars.toLowerCase();
    const String controlLine = lineContaining(chars, "306b0002-b081-4037-83dc-e59fcc3cdfd");
    const String lastDataLine = lineContaining(chars, "306b0003-b081-4037-83dc-e59fcc3cdfd");
    const String dataLine = lineContaining(chars, "306b0004-b081-4037-83dc-e59fcc3cdfd");

    const int parsedControl = csvInteger(controlLine, 3);
    const int parsedLastData = csvInteger(lastDataLine, 3);
    const int parsedData = csvInteger(dataLine, 3);
    if (parsedControl <= 0 || parsedLastData <= 0 || parsedData <= 0)
    {
        return false;
    }
    controlChar = (uint8_t)parsedControl;
    lastDataChar = (uint8_t)parsedLastData;
    dataChar = (uint8_t)parsedData;

    controlCccd = -1;
    lastDataCccd = -1;
    dataCccd = -1;

    int position = 0;
    while (position < (int)chars.length())
    {
        int end = chars.indexOf('\n', position);
        if (end < 0)
        {
            end = chars.length();
        }
        String line = chars.substring(position, end);
        line.trim();
        if (line.indexOf("+blegattcchar:\"desc\"") >= 0 && line.indexOf("2902") >= 0)
        {
            const int characteristic = csvInteger(line, 3);
            const int descriptor = csvInteger(line, 4);
            if (characteristic == controlChar) controlCccd = descriptor;
            if (characteristic == lastDataChar) lastDataCccd = descriptor;
            if (characteristic == dataChar) dataCccd = descriptor;
        }
        position = end + 1;
    }

    return controlCccd > 0 && lastDataCccd > 0 && dataCccd > 0;
}

static bool findLoadReport(uint8_t &value)
{
    const uint8_t instance = runtimeConfig.victronDeviceInstance;
    const uint8_t prefix[] = {
        0x08, instance, 0x19,
        (uint8_t)(VIC_LOAD_CONTROL_VREG >> 8),
        (uint8_t)(VIC_LOAD_CONTROL_VREG & 0xFF),
        0x41
    };

    if (espRxLen < sizeof(prefix) + 1)
    {
        return false;
    }
    for (size_t i = 0; i <= espRxLen - sizeof(prefix) - 1; ++i)
    {
        if (memcmp(&espRx[i], prefix, sizeof(prefix)) == 0)
        {
            value = espRx[i + sizeof(prefix)];
            return true;
        }
    }
    return false;
}

static bool requestLoadValue(uint8_t serviceIndex, uint8_t lastDataChar, uint8_t &value)
{
    const uint8_t getFrame[] = {
        0x05, runtimeConfig.victronDeviceInstance, 0x81, 0x19,
        (uint8_t)(VIC_LOAD_CONTROL_VREG >> 8),
        (uint8_t)(VIC_LOAD_CONTROL_VREG & 0xFF)
    };
    if (!writeGatt(serviceIndex, lastDataChar, -1, getFrame, sizeof(getFrame)))
    {
        return false;
    }
    collectEspFor(3000);
    return findLoadReport(value);
}

static uint8_t runVictronTransaction(uint8_t desiredValue)
{
    if (desiredValue != 0 && desiredValue != 4) return BLE_BAD_SETTINGS;
    if (String(runtimeConfig.victronMac) == "00:00:00:00:00:00" ||
        strlen(runtimeConfig.victronMac) != 17 ||
        runtimeConfig.victronDeviceInstance > 23)
    {
        return BLE_BAD_SETTINGS;
    }
    if (!legacyBleAtAvailable) return BLE_AT_FIRMWARE_MISSING;

    legacyPortalPause();
    drainEspUart();
    resetEspRx();
    if (!ensureLegacyEspPowered())
    {
        releaseLegacyEspAfterBle();
        return BLE_AT_FIRMWARE_MISSING;
    }

    if (!sendAt("AT+BLEINIT?", 3000))
    {
        releaseLegacyEspAfterBle();
        return BLE_AT_FIRMWARE_MISSING;
    }

    (void)sendAt("AT+BLEINIT=0", 3000);
    // Briefly hand the ESP32-C2 radio from WiFi to BLE. The portal is brought
    // back automatically by releaseLegacyEspAfterBle().
    if (!enterLegacyBleRadioMode())
    {
        releaseLegacyEspAfterBle();
        return BLE_AT_FIRMWARE_MISSING;
    }
    if (!sendAt("AT+BLEINIT=1", 8000))
    {
        releaseLegacyEspAfterBle();
        return BLE_AT_FIRMWARE_MISSING;
    }

    int addressType = scanVictronAddressType();
    if (addressType < 0)
    {
        addressType = runtimeConfig.victronAddressType;
    }
    if (addressType != 0 && addressType != 1)
    {
        (void)sendAt("AT+BLEINIT=0", 3000);
        releaseLegacyEspAfterBle();
        return BLE_TARGET_NOT_FOUND;
    }

    String connectCommand = "AT+BLECONN=0,\"";
    connectCommand += runtimeConfig.victronMac;
    connectCommand += "\",";
    connectCommand += String(addressType);
    connectCommand += ",20";
    if (!sendAt(connectCommand, 25000))
    {
        (void)sendAt("AT+BLEINIT=0", 3000);
        releaseLegacyEspAfterBle();
        return BLE_CONNECT_FAILED;
    }

    if (runtimeConfig.victronUseSmpPin)
    {
        (void)sendAt("AT+BLESECPARAM=13,2,16,3,3,0", 3000);
        String keyCommand = "AT+BLESETKEY=";
        keyCommand += runtimeConfig.victronPin;
        if (!sendAt(keyCommand, 3000) || !sendAt("AT+BLEENC=0,3", 12000))
        {
            (void)sendAt("AT+BLEDISCONN=0", 3000);
            (void)sendAt("AT+BLEINIT=0", 3000);
            releaseLegacyEspAfterBle();
            return BLE_CONNECT_FAILED;
        }
    }

    uint8_t serviceIndex = 0;
    uint8_t controlChar = 0;
    uint8_t lastDataChar = 0;
    uint8_t dataChar = 0;
    int controlCccd = -1;
    int lastDataCccd = -1;
    int dataCccd = -1;

    if (!discoverVictronGatt(serviceIndex, controlChar, lastDataChar, dataChar,
                             controlCccd, lastDataCccd, dataCccd))
    {
        (void)sendAt("AT+BLEDISCONN=0", 3000);
        (void)sendAt("AT+BLEINIT=0", 3000);
        releaseLegacyEspAfterBle();
        return BLE_GATT_NOT_FOUND;
    }

    const uint8_t notifyEnable[] = { 0x01, 0x00 };
    if (!writeGatt(serviceIndex, controlChar, controlCccd, notifyEnable, sizeof(notifyEnable)) ||
        !writeGatt(serviceIndex, lastDataChar, lastDataCccd, notifyEnable, sizeof(notifyEnable)) ||
        !writeGatt(serviceIndex, dataChar, dataCccd, notifyEnable, sizeof(notifyEnable)))
    {
        (void)sendAt("AT+BLEDISCONN=0", 3000);
        (void)sendAt("AT+BLEINIT=0", 3000);
        releaseLegacyEspAfterBle();
        return BLE_GATT_NOT_FOUND;
    }

    const uint8_t controlInit1[] = { 0xFA, 0x80, 0xFF };
    const uint8_t controlInit2[] = { 0xF9, 0x80 };
    const uint8_t session1[] = { 0x01 };
    const uint8_t session2[] = { 0x03, 0x00 };
    const uint8_t session3[] = {
        0x06, 0x00, 0x82, 0x18, 0x93, 0x42, 0x10, 0x27,
        0x05, 0x00, 0x82, 0x19, 0xEC, 0x66, 0x19, 0xEC, 0x65,
        0x03, 0x01, 0x03, 0x03
    };

    bool sessionOk = writeGatt(serviceIndex, controlChar, -1, controlInit1, sizeof(controlInit1));
    sessionOk = sessionOk && writeGatt(serviceIndex, controlChar, -1, controlInit2, sizeof(controlInit2));
    sessionOk = sessionOk && writeGatt(serviceIndex, lastDataChar, -1, session1, sizeof(session1));
    delay(200);
    sessionOk = sessionOk && writeGatt(serviceIndex, lastDataChar, -1, session2, sizeof(session2));
    delay(200);
    sessionOk = sessionOk && writeGatt(serviceIndex, lastDataChar, -1, session3, sizeof(session3));
    delay(700);

    uint8_t currentValue = 0xFF;
    if (!sessionOk || !requestLoadValue(serviceIndex, lastDataChar, currentValue))
    {
        (void)sendAt("AT+BLEDISCONN=0", 3000);
        (void)sendAt("AT+BLEINIT=0", 3000);
        releaseLegacyEspAfterBle();
        return BLE_INITIAL_READ_FAILED;
    }
    lastLoadValue = currentValue;

    if (currentValue != desiredValue)
    {
        // CBOR: setValues(instance, [VREG 0xEDAB, byte-string 0x04]).
        // This is the only settings write present in the firmware.
        const uint8_t setFrame[] = {
            0x06, runtimeConfig.victronDeviceInstance, 0x82, 0x19,
            (uint8_t)(VIC_LOAD_CONTROL_VREG >> 8),
            (uint8_t)(VIC_LOAD_CONTROL_VREG & 0xFF),
            0x41, desiredValue
        };
        if (!writeGatt(serviceIndex, lastDataChar, -1, setFrame, sizeof(setFrame)))
        {
            (void)sendAt("AT+BLEDISCONN=0", 3000);
            (void)sendAt("AT+BLEINIT=0", 3000);
            releaseLegacyEspAfterBle();
            return BLE_WRITE_FAILED;
        }
        delay(500);
    }

    uint8_t verifiedValue = 0xFF;
    const bool verified = requestLoadValue(serviceIndex, lastDataChar, verifiedValue) &&
                          verifiedValue == desiredValue;
    lastLoadValue = verifiedValue;

    (void)sendAt("AT+BLEDISCONN=0", 4000);
    (void)sendAt("AT+BLEINIT=0", 4000);
    releaseLegacyEspAfterBle();
    return verified ? BLE_OK : BLE_VERIFY_FAILED;
}
#endif

static bool isAllZero(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        if (data[i] != 0) return false;
    }
    return true;
}

static void wipeSensitive(void *pointer, size_t length)
{
    volatile uint8_t *bytes = reinterpret_cast<volatile uint8_t *>(pointer);
    while (length-- > 0) *bytes++ = 0;
}

static bool ensureLorawanCredentials(const uint8_t devEui[8],
                                     const uint8_t joinEui[8],
                                     const uint8_t appKey[16])
{
    uint8_t currentDevEui[8] = {0};
    uint8_t currentJoinEui[8] = {0};
    uint8_t currentAppKey[16] = {0};

    const bool haveDevEui = api.lorawan.deui.get(currentDevEui, sizeof(currentDevEui));
    const bool haveJoinEui = api.lorawan.appeui.get(currentJoinEui, sizeof(currentJoinEui));
    const bool haveAppKey = api.lorawan.appkey.get(currentAppKey, sizeof(currentAppKey));
    bool ok = true;
    if ((!haveAppKey || memcmp(currentAppKey, appKey, sizeof(currentAppKey)) != 0) &&
        !api.lorawan.appkey.set(const_cast<uint8_t *>(appKey), 16))
        ok = false;
    if (ok &&
        (!haveJoinEui || memcmp(currentJoinEui, joinEui, sizeof(currentJoinEui)) != 0) &&
        !api.lorawan.appeui.set(const_cast<uint8_t *>(joinEui), 8))
        ok = false;
    if (ok &&
        (!haveDevEui || memcmp(currentDevEui, devEui, sizeof(currentDevEui)) != 0) &&
        !api.lorawan.deui.set(const_cast<uint8_t *>(devEui), 8))
        ok = false;

    if (ok)
    {
        ok = api.lorawan.deui.get(currentDevEui, sizeof(currentDevEui)) &&
             api.lorawan.appeui.get(currentJoinEui, sizeof(currentJoinEui)) &&
             api.lorawan.appkey.get(currentAppKey, sizeof(currentAppKey)) &&
             memcmp(currentDevEui, devEui, sizeof(currentDevEui)) == 0 &&
             memcmp(currentJoinEui, joinEui, sizeof(currentJoinEui)) == 0 &&
             memcmp(currentAppKey, appKey, sizeof(currentAppKey)) == 0;
    }

    wipeSensitive(currentDevEui, sizeof(currentDevEui));
    wipeSensitive(currentJoinEui, sizeof(currentJoinEui));
    wipeSensitive(currentAppKey, sizeof(currentAppKey));
    return ok;
}

static bool recoverPendingLorawanUpdate()
{
    PendingLorawanCredentials pending;
    if (!runtimeConfigPendingLorawanUpdate(pending))
    {
        wipeSensitive(&pending, sizeof(pending));
        return true;
    }

    Serial.println("Onderbroken LoRaWAN-update wordt veilig hervat.");
    memcpy(nodeDevEui, pending.devEui, sizeof(nodeDevEui));
    memcpy(nodeAppEui, pending.joinEui, sizeof(nodeAppEui));
    memcpy(nodeAppKey, pending.appKey, sizeof(nodeAppKey));
    const bool applied = ensureLorawanCredentials(
        pending.devEui, pending.joinEui, pending.appKey);
    if (!applied || !runtimeConfigCommitLorawanUpdate(runtimeConfig))
    {
        wipeSensitive(&pending, sizeof(pending));
        Serial.println("LoRaWAN-herstel blijft bewaard voor de volgende boot.");
        return false;
    }
    wipeSensitive(&pending, sizeof(pending));
    Serial.println("LoRaWAN-update hersteld.");
    return true;
}

static bool equalsAsciiIgnoreCase(const uint8_t *data, uint8_t length, const char *word)
{
    const size_t wordLen = strlen(word);
    if (length != wordLen) return false;
    for (size_t i = 0; i < wordLen; ++i)
    {
        if (toupper((unsigned char)data[i]) != toupper((unsigned char)word[i])) return false;
    }
    return true;
}

static void receiveCallback(SERVICE_LORA_RECEIVE_T *data)
{
    if(data && networkJoined())networkDownlinkReceived();
    if(!networkJoined())return;
    if (data == nullptr || data->Port != runtimeConfig.loraFport || data->BufferSize == 0)
    {
        return;
    }
    if (haveDownlinkCounter && data->DownLinkCounter == lastDownlinkCounter)
    {
        return;
    }
    haveDownlinkCounter = true;
    lastDownlinkCounter = data->DownLinkCounter;

    const bool commandOn = (data->BufferSize == 1 && data->Buffer[0] == 0x01) ||
                           equalsAsciiIgnoreCase(data->Buffer, data->BufferSize, "ON") ||
                           equalsAsciiIgnoreCase(data->Buffer, data->BufferSize, "BEEP");
    uint8_t actions = commandOn ? (ACTION_LOAD_ON | ACTION_UPLINK) : 0;
    const uint8_t code = data->BufferSize == 1 ? data->Buffer[0] : 0;
    const uint8_t functionId=(code>=1 && code<=MAX_BLE_FUNCTIONS)?code:commandOn?1:
        equalsAsciiIgnoreCase(data->Buffer,data->BufferSize,"OFF")?2:0;
    if(functionId)actions=(functionId==2?ACTION_LOAD_OFF:ACTION_LOAD_ON)|ACTION_UPLINK;
    if (code == 0x02 || equalsAsciiIgnoreCase(data->Buffer, data->BufferSize, "OFF")) actions = ACTION_LOAD_OFF | ACTION_UPLINK;
    if (code == 0x10) actions = ACTION_RELAY_OFF | ACTION_UPLINK;
    if (code == 0x11) actions = ACTION_RELAY_ON | ACTION_UPLINK;
    if (code == 0x12) actions = ACTION_RELAY_PULSE | ACTION_UPLINK;
    if (code == 0x20) actions = ACTION_UPLINK;
    const uint8_t allowed=runtimeConfig.downlinkAllowed;
    if(functionId && (functionId>runtimeConfig.functionCount ||
       !(runtimeConfig.downlinkFunctions & (1u<<(functionId-1))) ||
       !runtimeConfig.loadOutputEnabled || !runtimeConfig.bleFunctions[functionId-1].kind)) return;
    if(!(allowed & 4)) actions &= ~ACTION_RELAY_OFF;
    if(!(allowed & 8)) actions &= ~ACTION_RELAY_ON;
    if(!(allowed & 16)) actions &= ~ACTION_RELAY_PULSE;
    // Disabled routes must not become a status command merely due to bit 0.

    if(!runtimeConfig.relayEnabled) actions &= ~(ACTION_RELAY_ON|ACTION_RELAY_OFF|ACTION_RELAY_PULSE);
    if(code!=0x20 && !(actions & ~ACTION_UPLINK)) return;
    if (!actions) return;
    const uint8_t next = (remoteWrite + 1) % 4;
    if (next == remoteRead) { ++busyEventRevision; requestStatusUplink(); return; }
    remoteActionQueue[remoteWrite] = actions;
    remoteFunctionQueue[remoteWrite]=functionId?functionId-1:255;
    remoteWrite = next;
}

static void joinCallback(int32_t status)
{
    networkJoinResult(status);
    if(!companionUsbActive())Serial.printf("LoRaWAN join-status: %ld\r\n", (long)status);
    if (status == RAK_LORAMAC_STATUS_OK)
    {
        // Downlink FCnt restarts for a fresh OTAA session. Do not let the
        // duplicate guard from the previous session discard its first command.
        haveDownlinkCounter = false;
        lastDownlinkCounter = 0;
        lastKnownJoined = true;
        requestStatusUplink();
    }
    else
    {
        lastKnownJoined = false;
    }
}

static void sendCallback(int32_t status)
{
    networkTxComplete(healthTxInFlight,status==RAK_LORAMAC_STATUS_OK&&api.lorawan.cfs.get());
    healthTxInFlight=false;
    if(!companionUsbActive())Serial.printf("LoRaWAN uplink-status: %ld\r\n", (long)status);
    statusTxInFlight = false;
    if (status == RAK_LORAMAC_STATUS_OK)
    {
        lastStatusAt = millis();
        ++txCount;
        const bool sentBusyStatus = busyStatusInFlight;
        if (sentBusyStatus)
        {
            busyEventAcked = busyEventInFlight;
        }
        busyStatusInFlight = false;
        statusRevisionAcked = statusRevisionInFlight;
        if (sentBusyStatus)
        {
            // A queue-overflow status is latched until local TX completion.
            // Follow it once with the latest state, which may have changed while
            // BUSY was waiting or in flight.
            requestStatusUplink();
        }
    }
    else
    {
        busyStatusInFlight = false;
        // Revisions remain unacknowledged; a later loop retries the uplink.
    }
}

static bool configureLorawan()
{
    if (!recoverPendingLorawanUpdate()) return false;

    // Use the device's current RUI identity by default, including a fresh
    // board's provisioned DevEUI. Do not require a per-board compiled value.
    uint8_t boardDevEui[8] = {0};
    if (api.lorawan.deui.get(boardDevEui, sizeof(boardDevEui)) &&
        !isAllZero(boardDevEui, sizeof(boardDevEui)))
        memcpy(nodeDevEui, boardDevEui, sizeof(nodeDevEui));

    // Public updates have no compiled AppKey. Preserve a previously provisioned
    // RUI tuple even when older firmware originally seeded it from settings.h.
    if (runtimeConfig.lorawanCredentialsFromPortal ||
        isAllZero(nodeAppKey, sizeof(nodeAppKey)))
    {
        if (!api.lorawan.deui.get(nodeDevEui, sizeof(nodeDevEui)) ||
            !api.lorawan.appeui.get(nodeAppEui, sizeof(nodeAppEui)) ||
            !api.lorawan.appkey.get(nodeAppKey, sizeof(nodeAppKey)))
        {
            Serial.println("LoRaWAN-gegevens konden niet uit RUI NVM worden gelezen.");
            return false;
        }
    }

    if(!runtimeConfig.networksInitialized) {
        NetworkProfile &p=runtimeConfig.networks[0];
        strcpy(p.name,"Private network");memcpy(p.joinEui,nodeAppEui,8);memcpy(p.appKey,nodeAppKey,16);
        p.enabled=!isAllZero(nodeAppKey,16);p.rx2Custom=runtimeConfig.rx2Custom;
        p.rx2DataRate=runtimeConfig.rx2DataRate;p.rx2Frequency=runtimeConfig.rx2Frequency;
        runtimeConfig.networksInitialized=1;
        if(!runtimeConfigSave(runtimeConfig)){Serial.println("Netwerkprofiel-migratie niet opgeslagen.");return false;}
    }
    if(isAllZero(nodeAppKey,16))for(unsigned i=0;i<MAX_NETWORKS;++i) {
        const NetworkProfile &p=runtimeConfig.networks[runtimeConfig.networkOrder[i]];
        if(p.enabled&&networkHasKey(p)){memcpy(nodeAppEui,p.joinEui,8);memcpy(nodeAppKey,p.appKey,16);break;}
    }
    if (isAllZero(nodeDevEui, sizeof(nodeDevEui)) ||
        isAllZero(nodeAppKey, sizeof(nodeAppKey)))
    {
        Serial.println("LoRaWAN niet gestart: vul de ontbrekende OTAA-waarden in via WiFi of settings.local.h.");
        return false;
    }

    if (api.lorawan.nwm.get() != 1)
    {
        Serial.println("LoRaWAN-modus wordt geactiveerd; het bord herstart.");
        api.lorawan.nwm.set();
        api.system.reboot();
    }

    bool settingsOk = ensureLorawanCredentials(nodeDevEui, nodeAppEui, nodeAppKey);

    // Read first and call RUI setters only when a value really differs. The
    // lower flash driver also compares before erasing, but this avoids needless
    // service work during every normal boot.
    const RegionProfile *region = regionProfile(runtimeConfig.loraRegion);
    if (!region) return false;
    if (settingsOk && api.lorawan.band.get() != runtimeConfig.loraRegion)
        settingsOk = api.lorawan.band.set(runtimeConfig.loraRegion);
    if (settingsOk && api.lorawan.deviceClass.get() != runtimeConfig.loraClass)
        settingsOk = api.lorawan.deviceClass.set(runtimeConfig.loraClass);
    if (settingsOk && (runtimeConfig.loraRegion == 5 || runtimeConfig.loraRegion == 6))
    {
        uint16_t desired = runtimeConfig.loraSubband ? (1U << (runtimeConfig.loraSubband-1)) : 0x00FF;
        uint16_t current = 0;
        settingsOk = api.lorawan.mask.get(&current);
        if (settingsOk && current != desired) settingsOk = api.lorawan.mask.set(&desired);
    }
    if (settingsOk && api.lorawan.njm.get() != (bool)RAK_LORA_OTAA)
        settingsOk = api.lorawan.njm.set(RAK_LORA_OTAA);
    if (settingsOk && !api.lorawan.pnm.get())
        settingsOk = api.lorawan.pnm.set(true);
    if (settingsOk && api.lorawan.jn2dl.get() != 6000)
        settingsOk = api.lorawan.jn2dl.set(6000);
    if (settingsOk && api.lorawan.jn1dl.get() != 5000)
        settingsOk = api.lorawan.jn1dl.set(5000);
    // RUI 4.2.4 may derive RX1 while changing RX2. Set RX2 first, then read
    // RX1 again; never write a temporary invalid/1-ms delay.
    if (settingsOk && api.lorawan.rx2dl.get() != 2000)
        settingsOk = api.lorawan.rx2dl.set(2000);
    if (settingsOk && api.lorawan.rx1dl.get() != 1000)
        settingsOk = api.lorawan.rx1dl.set(1000);
    const uint8_t rx2Dr = runtimeConfig.rx2Custom ? runtimeConfig.rx2DataRate : region->rx2DataRate;
    const uint32_t rx2Fq = runtimeConfig.rx2Custom ? runtimeConfig.rx2Frequency : region->rx2Frequency;
    if (settingsOk && api.lorawan.rx2dr.get() != rx2Dr)
        settingsOk = api.lorawan.rx2dr.set(rx2Dr);
    if (settingsOk && api.lorawan.rx2fq.get() != rx2Fq)
        settingsOk = api.lorawan.rx2fq.set(rx2Fq);
    if (settingsOk && !api.lorawan.dcs.get())
        settingsOk = api.lorawan.dcs.set(true);
    if (settingsOk && api.lorawan.dr.get() != region->uplinkDataRate)
        settingsOk = api.lorawan.dr.set(region->uplinkDataRate);
    if (settingsOk && api.lorawan.adr.get() != (runtimeConfig.adrEnabled != 0))
        settingsOk = api.lorawan.adr.set(runtimeConfig.adrEnabled != 0);
    if (settingsOk && api.lorawan.cfm.get())
        settingsOk = api.lorawan.cfm.set(0);

    if (!settingsOk)
    {
        Serial.println("LoRaWAN-configuratie mislukt.");
        return false;
    }

    api.lorawan.registerRecvCallback(receiveCallback);
    api.lorawan.registerJoinCallback(joinCallback);
    api.lorawan.registerSendCallback(sendCallback);
    return true;
}

static void sampleContact(void *context)
{
    (void)context;
    const int raw = digitalRead(CONTACT_INPUT_PIN);
    const uint32_t now = millis();
    if (raw != lastRawContact)
    {
        lastRawContact = raw;
        rawContactChangedAt = now;
    }

    if (raw != stableContact && (uint32_t)(now - rawContactChangedAt) >= CONTACT_DEBOUNCE_MS)
    {
        stableContact = raw;
        if (!runtimeConfig.inputEnabled) return;
        const bool rising = stableContact == CONTACT_ACTIVE_LEVEL;
        if (rising) ++risingCount; else ++fallingCount;
        const uint8_t next = (edgeWrite + 1) % 16;
        if (next == edgeRead) { ++inputOverflow; return; }
        edgeQueue[edgeWrite] = rising ? 1 : 0;
        edgeWrite = next;
    }
}

static void updateContact()
{
    if (!inputTimerReady) sampleContact(nullptr);
    if (edgeRead == edgeWrite) return;
    const bool rising = edgeQueue[edgeRead] != 0;
    edgeRead = (edgeRead + 1) % 16;
    executeInputEdge(rising, false);
}

static bool applyConfigRequest(CompanionConfigRequest &request,
                               bool &credentialsChanged,
                               const char *&errorCode)
{
    credentialsChanged = request.credentialMask != 0;
    if(bleBusy || pendingAlwaysOn) {errorCode="bluetooth_busy";return false;}
    if (!runtimeConfigValid(request.config) ||
        (request.credentialMask & ~(COMPANION_SET_DEVEUI |
                                    COMPANION_SET_JOINEUI |
                                    COMPANION_SET_APPKEY)) != 0)
    {
        errorCode = "invalid_settings";
        return false;
    }
    if (((request.credentialMask & COMPANION_SET_DEVEUI) &&
         isAllZero(request.devEui, sizeof(request.devEui))) ||
        ((request.credentialMask & COMPANION_SET_APPKEY) &&
         isAllZero(request.appKey, sizeof(request.appKey))))
    {
        errorCode = "zero_lorawan_value";
        return false;
    }

    RuntimeConfig &candidate = request.config;
    uint8_t targetDevEui[8] = {0};
    uint8_t targetJoinEui[8] = {0};
    uint8_t targetAppKey[16] = {0};
    PendingLorawanCredentials stagedBase;
    const bool haveStagedBase = runtimeConfigPendingLorawanUpdate(stagedBase);
    const bool haveCurrentTuple = !credentialsChanged || haveStagedBase ||
        (api.lorawan.deui.get(targetDevEui, sizeof(targetDevEui)) &&
         api.lorawan.appeui.get(targetJoinEui, sizeof(targetJoinEui)) &&
         api.lorawan.appkey.get(targetAppKey, sizeof(targetAppKey)));
    if (credentialsChanged && haveStagedBase)
    {
        memcpy(targetDevEui, stagedBase.devEui, sizeof(targetDevEui));
        memcpy(targetJoinEui, stagedBase.joinEui, sizeof(targetJoinEui));
        memcpy(targetAppKey, stagedBase.appKey, sizeof(targetAppKey));
    }
    wipeSensitive(&stagedBase, sizeof(stagedBase));
    if (!haveCurrentTuple)
    {
        wipeSensitive(&candidate, sizeof(candidate));
        wipeSensitive(targetDevEui, sizeof(targetDevEui));
        wipeSensitive(targetJoinEui, sizeof(targetJoinEui));
        wipeSensitive(targetAppKey, sizeof(targetAppKey));
        errorCode = "lorawan_read_failed";
        return false;
    }

    if (credentialsChanged)
    {
        if (request.credentialMask & COMPANION_SET_DEVEUI)
            memcpy(targetDevEui, request.devEui, sizeof(targetDevEui));
        if (request.credentialMask & COMPANION_SET_JOINEUI)
            memcpy(targetJoinEui, request.joinEui, sizeof(targetJoinEui));
        if (request.credentialMask & COMPANION_SET_APPKEY)
            memcpy(targetAppKey, request.appKey, sizeof(targetAppKey));
        candidate.lorawanCredentialsFromPortal = 1;

        if (isAllZero(targetDevEui, sizeof(targetDevEui)) ||
            isAllZero(targetAppKey, sizeof(targetAppKey)))
        {
            wipeSensitive(&candidate, sizeof(candidate));
            wipeSensitive(targetDevEui, sizeof(targetDevEui));
            wipeSensitive(targetJoinEui, sizeof(targetJoinEui));
            wipeSensitive(targetAppKey, sizeof(targetAppKey));
            errorCode = "incomplete_lorawan_tuple";
            return false;
        }

        // First persist the complete intended tuple and candidate config in a
        // new A/B generation. Only then touch RUI NVM. A power loss at any
        // later instruction is recovered idempotently during the next boot.
        if (!runtimeConfigStageLorawanUpdate(candidate, targetDevEui,
                                             targetJoinEui, targetAppKey))
        {
            wipeSensitive(&candidate, sizeof(candidate));
            wipeSensitive(targetDevEui, sizeof(targetDevEui));
            wipeSensitive(targetJoinEui, sizeof(targetJoinEui));
            wipeSensitive(targetAppKey, sizeof(targetAppKey));
            errorCode = "flash_write_failed";
            return false;
        }
        runtimeConfig = candidate;
        // The staged tuple is now the durable intended identity, even if an
        // immediate RUI write fails and boot recovery must finish it.
        memcpy(nodeDevEui, targetDevEui, sizeof(nodeDevEui));
        memcpy(nodeAppEui, targetJoinEui, sizeof(nodeAppEui));
        memcpy(nodeAppKey, targetAppKey, sizeof(nodeAppKey));

        if (!ensureLorawanCredentials(targetDevEui, targetJoinEui, targetAppKey) ||
            !runtimeConfigCommitLorawanUpdate(candidate))
        {
            wipeSensitive(&candidate, sizeof(candidate));
            wipeSensitive(targetDevEui, sizeof(targetDevEui));
            wipeSensitive(targetJoinEui, sizeof(targetJoinEui));
            wipeSensitive(targetAppKey, sizeof(targetAppKey));
            errorCode = "lorawan_update_pending";
            return false;
        }

    }
    else if (!runtimeConfigSave(candidate))
    {
        wipeSensitive(&candidate, sizeof(candidate));
        errorCode = "flash_write_failed";
        return false;
    }
    runtimeConfig = candidate;

    wipeSensitive(&candidate, sizeof(candidate));
    wipeSensitive(targetDevEui, sizeof(targetDevEui));
    wipeSensitive(targetJoinEui, sizeof(targetJoinEui));
    wipeSensitive(targetAppKey, sizeof(targetAppKey));
    errorCode = "ok";
    return true;
}

#if LEGACY_BLE_AT
static void serviceLegacyWifi(uint32_t now)
{
    if (!wifiWanted(now))
    {
        if (legacyEspPowered) powerOffLegacyEsp();
        return;
    }

    if (!legacyPortalIsRunning() &&
        (legacyPortalRetryAt == 0 || !deadlinePending(now, legacyPortalRetryAt)))
    {
        if (ensureLegacyEspPowered() &&
            legacyPortalStart(runtimeConfig, nodeDevEui, nodeAppEui))
        {
            legacyPortalRetryAt = 0;
            legacyPortalPause();
            legacyBleAtAvailable = sendAt("AT+BLEINIT?", 3000);
            legacyPortalResume();
            Serial.print("Beveiligd instellingen-WiFi actief: ");
            Serial.println(runtimeConfig.wifiApSsid);
            Serial.println("Open http://192.168.4.1/");
            if (!legacyBleAtAvailable)
            {
                Serial.println("Let op: ESP-WiFi werkt, maar deze ESP-image bevat geen BLE-AT.");
            }
        }
        else
        {
            powerOffLegacyEsp();
            legacyPortalRetryAt = now + 10000UL;
            Serial.println("ESP-AT WiFi-portaal start niet; nieuwe poging over 10 s.");
            return;
        }
    }

    servicePortalApi();
}
#endif

static void servicePortalApi()
{
    PortalLiveStatus live = {};
#if LEGACY_BLE_AT
    live.bleAvailable = legacyBleAtAvailable;
#else
    live.bleAvailable = companionIsReady();
#endif
    live.joined = networkJoined();
    live.bleResult = lastBleResult;
    live.loadValue = lastLoadValue;
    live.bleAttempts = lastBleAttempts;
    live.blePending = pendingAlwaysOn || bleBusy;
    static uint32_t loggedTx=0; static int loggedJoin=-1, loggedBle=-1;
    if(loggedTx!=txCount){loggedTx=txCount;activityAdd(10,txCount);}
    if(loggedJoin!=(int)live.joined){loggedJoin=live.joined;activityAdd(9,live.joined);}
    if(loggedBle!=lastBleResult){loggedBle=lastBleResult;activityAdd(8,lastBleResult);}
    live.inputActive = stableContact == CONTACT_ACTIVE_LEVEL;
    live.relayOn = relayOn;
    live.relayPulsing = relayPulseActive;
    live.txCount = txCount;
    live.risingCount = risingCount;
    live.fallingCount = fallingCount;
    live.lastEvent = lastEvent;
    live.lastActions = lastActions;
    live.relayChangedAt = relayChangedAt;
    live.inputOverflow = inputOverflow;
    legacyPortalSetStatus(live);
#if LEGACY_BLE_AT
    legacyPortalService();
#endif
    uint8_t action = 0;
    if (pendingRebootAt == 0 && legacyPortalTakeAction(action))
    {
        if (action == 1 || action == 2) executeInputEdge(action == 1, true);
        else if (action == 3) executeActions(ACTION_RELAY_PULSE | ACTION_UPLINK, 6);
        else if (action == 4) executeActions(ACTION_RELAY_OFF | ACTION_UPLINK, 6);
        else if (action == 5) executeActions(ACTION_UPLINK, 6);
        else if (action == PORTAL_ACTION_REBOOT) pendingRebootAt = millis() + 2500UL;
#if !LEGACY_BLE_AT
        else if(action==6) companionScan();
        else if(action>=7 && action<7+runtimeConfig.functionCount) executeActions((action==8?ACTION_LOAD_OFF:ACTION_LOAD_ON)|ACTION_UPLINK,6,action-7);
#endif
    }

    CompanionConfigRequest request;
    if (legacyPortalTakeConfigRequest(request))
    {
        const uint32_t revisionBefore = runtimeConfigRevision();
        bool credentialsChanged = false;
        const char *errorCode = "save_failed";
        const bool saved = applyConfigRequest(request, credentialsChanged, errorCode);
        const bool durableStateChanged = runtimeConfigRevision() != revisionBefore;
        legacyPortalConfigResult(request.requestId, saved,
                                 runtimeConfigRevision(), saved && !durableStateChanged ? "unchanged" : errorCode);
        if (saved || durableStateChanged)
        {
            legacyPortalUpdateSnapshot(runtimeConfig, nodeDevEui, nodeAppEui);
            if(durableStateChanged) {
                activityAdd(13,runtimeConfigRevision());
                requestStatusUplink();
                pendingRebootAt = millis() + 2500UL;
                Serial.println("Portalinstellingen opgeslagen; herstart volgt.");
            }
        }
        wipeSensitive(&request, sizeof(request));
    }
}

#if !LEGACY_BLE_AT
static void serviceCompanion(uint32_t now)
{
    const bool portalWanted = wifiWanted(now);
    companionSetDemand(portalWanted,
                       runtimeConfig.inputEnabled && (runtimeConfig.wifiTriggers&2) && stableContact == CONTACT_ACTIVE_LEVEL,
                       configSecondsRemaining(now));

    if (pendingAlwaysOn)
    {
        pendingAlwaysOn = false;
        if (!companionRequestVictronLoad(pendingLoadValue))
        {
            lastBleResult = BLE_BUSY;
            lastBleAttempts = 0;
            lastLoadValue = 0xFF;
            ++busyEventRevision;
            requestStatusUplink();
        } else bleBusy = true;
    }

    servicePortalApi();
    companionService(now);

    uint8_t result = BLE_NOT_RUN;
    uint8_t attempts = 0;
    uint8_t loadValue = 0xFF;
    if (companionTakeVictronResult(result, attempts, loadValue))
    {
        bleBusy = false;
        lastBleResult = result;
        lastBleAttempts = attempts;
        lastLoadValue = loadValue;
        Serial.printf("Victron-resultaat via ESP-v4: %u\r\n", result);
        requestStatusUplink();
    }


}
#endif

static void sendStatusUplink()
{
    if (!lorawanConfigured || !networkJoined() || statusTxInFlight || pendingRebootAt)
    {
        return;
    }

    const uint32_t now = millis();
    if (lastStatusAttemptAt != 0 &&
        (uint32_t)(now - lastStatusAttemptAt) < STATUS_RETRY_MS)
    {
        return;
    }

    // Capture generations before composing the payload. Any event that races
    // with composition then remains newer than this frame and is sent later.
    const uint32_t revisionForPayload = statusRevision;
    const uint32_t busyRevisionForPayload = busyEventRevision;
    const bool reportBusyEvent = busyRevisionForPayload != busyEventAcked;

    // RUI reports the battery/VBAT rail in volts. Keep raw vehicle/input
    // voltage separate: the opto-isolated RAK13001 input only exposes a
    // digital state and cannot measure the applied 12/24-V magnitude.
    const float boardVbat = api.system.bat.get();
    const uint16_t boardVbatMv =
        (boardVbat > 0.0f && boardVbat < 10.0f)
            ? (uint16_t)(boardVbat * 1000.0f + 0.5f)
            : 0xFFFF;

    uint8_t payload[20];
    payload[0] = FW_MAJOR;
    payload[1] = FW_MINOR;
    payload[2] = (stableContact == CONTACT_ACTIVE_LEVEL) ? 1 : 0;
    payload[3] = reportBusyEvent ? BLE_BUSY : lastBleResult;
    payload[4] = reportBusyEvent ? 0 : lastBleAttempts;
    payload[5] = reportBusyEvent ? 0xFF : lastLoadValue;
    payload[6] = pendingAlwaysOn ? 1 : 0;
    payload[7] = 4; // Adds device profile + stable function ID; prior positions retained.
#if LEGACY_BLE_AT
    payload[8] = legacyPortalIsRunning() ? 2 : 0;
#else
    payload[8] = companionState();
#endif
    payload[9] = wifiWanted(now) ? 1 : 0;
    payload[10] = (uint8_t)runtimeConfigRevision();
    payload[11] = (uint8_t)(runtimeConfigRevision() >> 8);
    payload[12] = (uint8_t)boardVbatMv;
    payload[13] = (uint8_t)(boardVbatMv >> 8);
    payload[14] = (runtimeConfig.inputEnabled ? 1 : 0) | (runtimeConfig.relayEnabled ? 2 : 0) |
                  (relayOn ? 4 : 0) | (relayPulseActive ? 8 : 0);
    payload[15] = lastEvent;
    payload[16] = lastActions;
    const uint8_t requestedKind = runtimeConfig.bleFunctions[pendingLoadValue%MAX_BLE_FUNCTIONS].kind;
    payload[17] = runtimeConfig.deviceProfile==3?smartBatteryProtectMode(requestedKind):smartMpptMode(requestedKind);
    payload[18] = runtimeConfig.deviceProfile;
    payload[19] = (lastActions & (ACTION_LOAD_ON|ACTION_LOAD_OFF))?pendingLoadValue+1:0;

    statusRevisionInFlight = revisionForPayload;
    busyStatusInFlight = reportBusyEvent;
    busyEventInFlight = busyRevisionForPayload;
    statusTxInFlight = true;
    lastStatusAttemptAt = now;
    healthTxInFlight=networkHealthDue();
    if (!api.lorawan.send(sizeof(payload), payload, runtimeConfig.loraFport, healthTxInFlight, 0))
    {
        healthTxInFlight=false;
        statusTxInFlight = false;
        busyStatusInFlight = false;
    }
}

void setup()
{
    // Explicitly restore both ports and their intended modes if an earlier AT
    // configuration changed them. RUI's flash driver skips identical writes.
    Serial.begin(115200, RAK_AT_MODE);
    // The application owns this UART; the board's default ESP bridge must not
    // concurrently consume replies intended for the portal/companion parser.
    Serial1.begin(115200, RAK_CUSTOM_MODE);
    delay(1500);
    Serial.println("Victron LoRaBLE Remote - firmware v4.11.1");
    activityAdd(1);

    setEspPowerMode(POWER_OFF);
#if LEGACY_BLE_AT
    legacyEspPowered = false;
#endif
    setCurrentATMode(LORA_AT_MODE);
    // RUI 4.2.4 on the RAK11160 can miss OTAA/Class-A receive windows when
    // low-power mode or sleep.all() is entered while the radio transaction is
    // still pending. Keep the STM32 awake so joins, downlinks and uplinks are
    // handled reliably.
    if (api.system.lpm.get() != 0 && !api.system.lpm.set(0))
        Serial.println("Energiebesparingsmodus kon niet worden uitgeschakeld.");
    const bool storedConfig = runtimeConfigLoad(runtimeConfig);
#ifdef LORABLE_PUBLIC_BUILD
    if (!storedConfig) {
        // A new installation never arms unknown hardware or Bluetooth targets.
        // Existing saved installations and their migrations remain unchanged.
        runtimeConfig.ioBoard = 0;
        runtimeConfig.inputEnabled = runtimeConfig.relayEnabled = 0;
        runtimeConfig.loadOutputEnabled = 0;
        runtimeConfig.risingActions = runtimeConfig.fallingActions = 0;
        runtimeConfig.risingFunction = runtimeConfig.fallingFunction = 0;
        runtimeConfig.downlinkAllowed = runtimeConfig.downlinkFunctions = 0;
        runtimeConfig.wifiTriggers = 0;
        runtimeConfig.statusIntervalMinutes = 240;
    }
#endif
    Serial.printf("Configuratie: %s, revisie %lu\r\n",
                  storedConfig ? "opgeslagen" : "standaard",
                  (unsigned long)runtimeConfigRevision());
    startConfigWindow(millis());
    lorawanConfigured = configureLorawan();

    // Configure WB_IO3 only after band.set(). RUI temporarily uses PB12/WB_IO3
    // for its H/L radio-band detection and deinitializes it afterwards.
    digitalWrite(RELAY_OUTPUT_PIN, LOW);
    pinMode(RELAY_OUTPUT_PIN, OUTPUT);
    digitalWrite(RELAY_OUTPUT_PIN, LOW);
    // Direct RTC timer: the pulse ends even while an AT/HTTP call blocks the
    // application loop. RUI user timers are queued and can be delayed there.
    relayTimerReady = UTIL_TIMER_Create(&relayTimer, 0xFFFFFFFFU, UTIL_TIMER_ONESHOT,
                                        relayPulseExpired, nullptr) == UTIL_TIMER_OK;
    pinMode(WB_IO2, OUTPUT);
    digitalWrite(WB_IO2, HIGH);
    delay(50);
    pinMode(CONTACT_INPUT_PIN, INPUT_PULLUP);

    // Boot is not an edge: establish the current level without switching a load.
    stableContact = digitalRead(CONTACT_INPUT_PIN);
    lastRawContact = stableContact;
    rawContactChangedAt = millis();
    inputTimerReady = UTIL_TIMER_Create(&inputTimer, 10, UTIL_TIMER_PERIODIC, sampleContact, nullptr) == UTIL_TIMER_OK &&
                      UTIL_TIMER_Start(&inputTimer) == UTIL_TIMER_OK;
    lastWifiWanted = wifiWanted(millis());

#if LEGACY_BLE_AT
    Serial.println("ESP-AT instellingenportaal; BLE-ondersteuning wordt gecontroleerd.");
#ifdef ESP_CAPABILITY_DIAGNOSTIC
    if (ensureLegacyEspPowered())
    {
        // Probe the short OTA handshake before any long UART response.
        const bool otaHandshake = sendAt("AT+USEROTA=1", 3000);
        Serial.printf("ESP USEROTA command accepted=%u; no URL supplied\r\n", otaHandshake);
        setEspPowerMode(POWER_OFF);
        legacyEspPowered = false;
        delay(300);
        ensureLegacyEspPowered();
        // UART_CUR is volatile. Lower the baud rate for long diagnostic
        // replies without modifying either processor's saved UART settings.
        Serial1.print("AT+UART_CUR=9600,8,1,0,0\r\n");
        Serial1.flush();
        delay(200);
        udrv_serial_deinit(SERIAL_UART1);
        udrv_serial_init(SERIAL_UART1, 9600, SERIAL_WORD_LEN_8, SERIAL_STOP_BIT_1,
                         SERIAL_PARITY_DISABLE, SERIAL_TWO_WIRE_NORMAL_MODE);
        const bool slowUart = sendAt("AT", 2000);
        Serial.printf("ESP diagnostic UART at 9600=%u\r\n", slowUart);
        const bool versionComplete = sendAt("AT+GMR", 4000);
        Serial.printf("ESP version complete=%u\r\n", versionComplete);
        Serial.println(espRxText());
        const bool partitionsComplete = sendAt("AT+SYSFLASH?", 4000);
        Serial.printf("ESP partition query complete=%u\r\n", partitionsComplete);
        Serial.println(espRxText());
        setEspPowerMode(POWER_OFF);
        legacyEspPowered = false;
        delay(300);
        Serial1.begin(115200, RAK_CUSTOM_MODE);
    }
#endif
    // Bring the portal up before starting OTAA so its blocking AT setup cannot
    // overlap the first Class-A join receive windows.
    serviceLegacyWifi(millis());
#else
#ifdef ESP_MIGRATION_TO_NATIVE
    migrationBegin(runtimeConfig);
#else
    legacyPortalUpdateSnapshot(runtimeConfig, nodeDevEui, nodeAppEui);
    companionBegin(runtimeConfig, runtimeConfigRevision(), nodeDevEui, nodeAppEui);
#endif
#endif

    requestStatusUplink();
    if (lorawanConfigured)
    {
        networkBegin(runtimeConfig,nodeDevEui);
    }
}

void loop()
{
#ifdef ESP_MIGRATION_TO_NATIVE
    static bool nativeStarted=false;
    if(migrationService())return;
    if(!nativeStarted){
        nativeStarted=true;
        startConfigWindow(millis());
        legacyPortalUpdateSnapshot(runtimeConfig,nodeDevEui,nodeAppEui);
        companionBegin(runtimeConfig,runtimeConfigRevision(),nodeDevEui,nodeAppEui);
    }
#endif
    if (relayExpiredEvent)
    {
        relayExpiredEvent = false;
        if (relayReportOnExpiry) requestStatusUplink();
    }
    updateContact();
    if (remoteRead != remoteWrite)
    {
        const uint8_t actions = remoteActionQueue[remoteRead],fn=remoteFunctionQueue[remoteRead];
        remoteRead = (remoteRead + 1) % 4;
        if(runtimeConfig.wifiTriggers & 4) extendConfigWindow(millis(),runtimeConfig.wifiAfterLoraSeconds);
        executeActions(actions, 3,fn);
    }

    activityTick();
    if(lorawanConfigured&&!pendingRebootAt)networkTick(statusTxInFlight);
    const uint32_t now = millis();
    const bool currentWifiWanted = wifiWanted(now);
    if (currentWifiWanted != lastWifiWanted)
    {
        lastWifiWanted = currentWifiWanted;
        Serial.println(currentWifiWanted ? "Configuratievenster actief."
                                         : "Configuratievenster verlopen.");
        requestStatusUplink();
    }
#if LEGACY_BLE_AT
    serviceLegacyWifi(now);
#endif
    const bool joined = lorawanConfigured && networkJoined();
    if (joined && !lastKnownJoined)
    {
        lastKnownJoined = true;
        requestStatusUplink();
    }
    else if (!joined)
    {
        lastKnownJoined = false;
    }

    const uint32_t statusIntervalMs =
        networkStatusIntervalMinutes() * 60UL * 1000UL;
    if (joined && !statusUplinkNeeded() && !statusTxInFlight &&
        ((uint32_t)(now - lastStatusAt) >= statusIntervalMs || networkHealthDue()))
    {
        requestStatusUplink();
    }

    // Queue the contact/downlink receipt before the blocking BLE transaction.
    if (joined && statusUplinkNeeded())
    {
        sendStatusUplink();
    }

    if (pendingAlwaysOn)
    {
#if LEGACY_BLE_AT
        pendingAlwaysOn = false;
        bleBusy = true;
        const uint8_t requestedValue = smartMpptMode(runtimeConfig.bleFunctions[pendingLoadValue].kind);
        const bool report = pendingBleReport;
        for (uint8_t attempt = 1; attempt <= runtimeConfig.bleMaxAttempts; ++attempt)
        {
            lastBleAttempts = attempt;
            lastBleResult = runVictronTransaction(requestedValue);
            Serial.printf("Victron-poging %u, resultaat %u\r\n", attempt, lastBleResult);
            if (lastBleResult == BLE_OK || lastBleResult == BLE_BAD_SETTINGS ||
                lastBleResult == BLE_AT_FIRMWARE_MISSING)
            {
                break;
            }
            delay(1000);
        }
        bleBusy = false;
        if (report) requestStatusUplink();
#endif
    }

#if !LEGACY_BLE_AT
    serviceCompanion(now);
#endif

    if (lorawanConfigured && networkJoined() && statusUplinkNeeded())
    {
        sendStatusUplink();
    }

    if (pendingRebootAt != 0 &&
        (int32_t)(millis() - pendingRebootAt) >= 0)
    {
        api.system.reboot();
    }

#if !LEGACY_BLE_AT
    // Keep servicing both the LoRaWAN stack and the 512-byte ESP UART ring.
    delay(companionIsPowered() ? 5 : 20);
#else
    delay(legacyPortalIsRunning() ? 5 : 20);
#endif
}
