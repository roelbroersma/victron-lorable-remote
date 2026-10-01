# Protocol en configuratie / Protocol and configuration

[Bouwen / Building](DEVELOPMENT.md) · [LoRaWAN](NETWORKS.md)

Dit document beschrijft de ontwikkelaarsinterface van **4.12.1**. Eindgebruikers installeren één compleet `.bin`-bestand. De interne UART-frames zijn geen LoRaWAN-payloads; daarvoor staan de formatters en downlinks in [NETWORKS.md](NETWORKS.md).

This document describes the **4.12.1** developer interface. End users install one complete `.bin`. Internal UART frames are not LoRaWAN payloads; use the formatters and downlinks documented in [NETWORKS.md](NETWORKS.md).

## Instellingen / Settings

### Bluetooth-apparaatprofielen / Bluetooth device profiles

| `profile` / UART `driver` | Apparaat / Device | Functies / Functions |
| --- | --- | --- |
| 1 | Victron Smart MPPT | Existing MPPT function kinds |
| 3 | Smart BatteryProtect 12/24V-100A, A3B1 | 11 ON, 12 OFF |
| 4 | Smart BatteryProtect 48V-100A, A3B3 | 11 ON, 12 OFF |
| 2 | Generic Bluetooth / GATT | 3 write, 4 write + readback |

Beide BatteryProtect-profielen gebruiken instantie 0, modusregister `0x0200` (3 AAN, 4 UIT) en uitgangsstatus `0xEDA8`. De productidentiteit in `0x0100` moet bij het gekozen profiel passen voordat een schrijfopdracht wordt verstuurd. Opslagformaat 9, back-upschema 10 en bestaande profielnummers blijven ongewijzigd. Oudere firmware kent profiel 4 niet; gebruik de complete update.

Both BatteryProtect profiles use instance 0, mode register `0x0200` (3 ON, 4 OFF) and output state `0xEDA8`. Identity register `0x0100` must match the selected product before any switch write. Storage format 9, backup schema 10 and existing profile IDs are unchanged. Older firmware does not understand profile 4; install the complete update.

De A3B3-registergegevens en waarnemingen aan een 48V-100A met firmware 2.11 zijn aangeleverd door [mraygalaxy in PR #1](https://github.com/roelbroersma/victron-lorable-remote/pull/1). De actieve native driver voor A3B1 gebruikte al `0x0200`; `0xEDAB` hoort bij de MPPT-route. / A3B3 register evidence and observations on a 48V-100A running firmware 2.11 were contributed by mraygalaxy in PR #1. The active native A3B1 driver already used `0x0200`; `0xEDAB` belongs to MPPT control.

`GET /config` leest de opgeslagen instellingen. `POST /save` ontvangt de volledige URL-gecodeerde instellingenset, niet een gedeeltelijke PATCH. Nieuwe velden:

`GET /config` reads saved settings. `POST /save` accepts the complete URL-encoded settings form, not a partial PATCH. New fields:

| Veld / Field | Bereik / Range | Standaard / Default |
| --- | --- | --- |
| `tx_dbm` | 0–22 dBm | 14 |
| `wifi_mode` | `0` AP, `1` router + fallback AP, `2` WiFi off | 0 |
| `sta_ssid` | 1–32 printable ASCII characters in router mode | Empty |
| `sta_password` | 8–63 printable ASCII characters; spaces allowed | Empty |
| `sta_delay_s` | 0–600 seconds | 90 |
| `sta_timeout_s` | 10–300 seconds | 60 |
| `rise_hold_s`, `fall_hold_s` | 0–3600 seconds | 0 |
| `rise_delay_s`, `fall_delay_s` | 0–3600 seconds | 0 |

Een leeg `sta_password` bij opslaan behoudt het bestaande wachtwoord. Een routerprofiel vereist een SSID en opgeslagen wachtwoord. `/config` geeft alleen `sta_password_set` terug, nooit het wachtwoord. Back-ups bevatten geen wachtwoorden, AppKeys, Bluetooth-PIN of DevEUI. Geef geheimen niet door aan logs, schermafbeeldingen of releasebestanden. Op een nieuw board moeten deze opnieuw worden ingevuld.

An empty `sta_password` on save retains the existing password. Router mode requires an SSID and a saved password. `/config` returns only `sta_password_set`, never the password. Backups exclude passwords, AppKeys, the Bluetooth PIN and DevEUI. Do not include secrets in logs, screenshots or releases; enter them again on a new board.

De invoer moet gedurende de minimumtijd **plus** de actievertraging op het nieuwe niveau blijven. Een tegengestelde flank annuleert de lopende actie; een blijvend niveau herhaalt de actie niet. De handmatige invoersimulatie voert de gekozen actie direct uit. Alle timers zijn vluchtig.

The input must remain at its new level for the minimum duration **plus** the action delay. An opposite edge cancels the pending action; a held level does not repeat it. Manual input simulation executes the selected action immediately. All timers are volatile.

`tx_dbm` is het gevraagde maximum aan de radio-uitgang. ADR en de regionale radio-implementatie kunnen een lager vermogen kiezen. Het is geen gemeten EIRP: antenneversterking en kabelverlies blijven apart relevant.

`tx_dbm` is the requested conducted radio-output ceiling. ADR and regional radio behavior may choose less power. It is not measured EIRP; antenna gain and cable loss remain separate considerations.

## WiFi en DHCP / WiFi and DHCP

In routermodus blijft het AP aanvankelijk uit. Na `sta_delay_s` start de verbinding, inclusief DHCP, met een venster van `sta_timeout_s`. Associatie alleen telt niet als verbonden: daarvoor is `IP_EVENT_STA_GOT_IP` nodig. Bij mislukking wordt het beveiligde AP op **192.168.4.1** gestart. Automatisch opnieuw proberen blijft actief: normaal vijf seconden tussen mislukte pogingen binnen een venster en zestig seconden tussen vensters. Na verlies van een werkende verbinding komt AP-terugval beschikbaar en volgt een nieuwe routerpoging na vijf seconden.

Bij DHCP-succes gaat het fallback-AP uit, ook met verbonden AP-clients. Open daarna het door de router toegewezen adres. Er is geen vast STA-adres en **192.168.4.1 wordt niet als noodadres op het routernetwerk gebruikt**. `WIFI_EVENT_STA_DISCONNECTED` en `IP_EVENT_STA_LOST_IP` wissen de verbonden-status. Verlopen van het WiFi-venster of expliciet uitschakelen start geen fallback-AP. USB-onderhoud kan de lokale HTTP-server gebruiken zonder WiFi-radio. De WiFi-driver bewaart configuratie uitsluitend in RAM; opnieuw verbinden schrijft niets naar flash.

Router mode initially keeps AP off. After `sta_delay_s`, association and DHCP share a `sta_timeout_s` connection window. Only `IP_EVENT_STA_GOT_IP` means connected; association alone does not. Failure starts the secured AP at **192.168.4.1**. Retries continue automatically: normally five seconds between failed attempts within a window and sixty seconds between windows. Losing a working connection enables fallback AP and schedules a router retry after five seconds.

Successful DHCP shuts down fallback AP, including its connected clients. Use the router-assigned address afterward. There is no static STA address; **192.168.4.1 is never used as an emergency address on the router network**. Disconnect or lost-IP events clear connected status. An expired WiFi window or explicit WiFi-off does not start fallback AP. USB maintenance can use the local HTTP server without the WiFi radio. WiFi driver configuration and reconnection state are RAM-only.

## HTTP-status / HTTP status

`GET /session` geeft een tijdelijke `token`; schrijfverzoeken gebruiken die in de header `X-LoRaBLE`. Dit is bescherming tegen cross-site-verzoeken, geen gebruikerslogin. Het portaal gebruikt HTTP; gebruik een vertrouwd netwerk en stel het niet rechtstreeks aan internet bloot.

`GET /session` returns a temporary `token`; include it as `X-LoRaBLE` on write requests. This protects against cross-site requests, not against other users on the same network. The portal uses HTTP; use a trusted network and do not expose it directly to the internet.

WiFi-status wordt door de ESP toegevoegd aan zowel `/session` als `/status`:

The ESP adds WiFi observations to both `/session` and `/status`:

| Veld / Field | Betekenis / Meaning |
| --- | --- |
| `wifi_mode` | Saved mode: 0 / 1 / 2 |
| `wifi_phase` | `off`, `ap`, `delay`, `connecting`, `connected`, `fallback` |
| `wifi_sta_connected` | DHCP address acquired; false after disconnect/lost IP |
| `wifi_sta_ip`, `wifi_sta_ssid` | Router-assigned IP or empty; configured router SSID |
| `wifi_sta_rssi_dbm` | Router signal strength, or `null` when unavailable |
| `wifi_ap_active`, `wifi_ap_ip` | AP active; its address or empty |
| `wifi_clients` | AP client count, not router clients |
| `wifi_next_s` | Delay/fallback: next attempt; connecting: remaining connection window |

`/session` geeft daarnaast `wifi_rssi_dbm`: de beschikbare RSSI-waarden van AP-clients. Toon routerstatus alleen als verbonden wanneer `wifi_sta_connected` waar is én een geldig `wifi_sta_ip` beschikbaar is.

`/session` additionally returns `wifi_rssi_dbm`, an array of available AP-client RSSI readings. Display router connectivity only when `wifi_sta_connected` is true and a valid `wifi_sta_ip` is present.

LoRa-status maakt onderscheid tussen lokale acceptatie, zendafhandeling en netwerk-ACK. `lora_tx_state`: `0` geen poging, `1` aangenomen, `2` afgehandeld, `3` lokaal geweigerd, `4` fout bij afhandeling. `lora_tx_ack`: `0` niet gevraagd, `1` wachtend, `2` ontvangen, `3` niet ontvangen. Een ACK bevestigt geen uitgevoerde Bluetooth- of relaisactie. Zonder `lora_rx_seen` zijn RSSI/SNR onbekend; toon bij ontvangen waarden ook `lora_rx_age_s`. `lora_tx_dbm` is de laatst berekende radio-instelling (`-128` betekent onbekend), niet een RF-meting; `lora_max_dbm` is de opgeslagen bovengrens.

LoRa status distinguishes local acceptance, completion and network ACK. `lora_tx_state`: `0` none, `1` accepted, `2` completed, `3` locally rejected, `4` completion error. `lora_tx_ack`: `0` not requested, `1` pending, `2` received, `3` not received. An ACK does not confirm a Bluetooth/relay action. RSSI/SNR are unknown unless `lora_rx_seen`; display `lora_rx_age_s` with observed values. `lora_tx_dbm` is the last computed radio setting (`-128` unknown), not an RF measurement; `lora_max_dbm` is the saved ceiling.

## Interne UART / Internal UART

115200 baud, 8N1, geen hardware-flowcontrol. Een frame is `@1|TYPE|IIII|BASE64|CCCC\r\n`: `IIII` is een viercijferig hexadecimaal request-ID; `CCCC` is CRC16-CCITT-FALSE over `1|TYPE|IIII|BASE64`, zonder `@` of regeleinde. Het maximum is 384 gedecodeerde payloadbytes en 768 framebytes. Payloadvelden gebruiken URL-form-encoding; base64 en CRC bieden **geen encryptie**.

115200 baud, 8N1, no hardware flow control. Frames use `@1|TYPE|IIII|BASE64|CCCC\r\n`: `IIII` is the four-digit hexadecimal request ID; `CCCC` is CRC16-CCITT-FALSE over `1|TYPE|IIII|BASE64`, excluding `@` and the line ending. Limits are 384 decoded payload bytes and 768 frame bytes. Payload fields use URL-form encoding; base64 and CRC provide **no encryption**.

Voor ieder startverzoek stuurt de besturing eerst `WIFI_CONFIG` met een eigen niet-nul request-ID, gevolgd door `PORTAL_START` met `wifi_cfg=<dat ID>`:

Before each start request, the controller sends `WIFI_CONFIG` with its own nonzero request ID, then `PORTAL_START` with `wifi_cfg=<that ID>`:

```text
WIFI_CONFIG (ID 0x002A) payload: mode=1&delay=90&timeout=60&ssid=<encoded-SSID>&password=<encoded-router-password>
PORTAL_START payload: seconds=3600&wifi_cfg=42&password=<encoded-AP-password>&ssid=<encoded-AP-SSID>
```

`PORTAL_START` gebruikt de AP-gegevens: SSID van 1–32 afdrukbare ASCII-tekens en een wachtwoord van 12–63 afdrukbare ASCII-tekens zonder spaties. Dit zijn niet de routergegevens uit `WIFI_CONFIG`.

`PORTAL_START` uses AP credentials: a 1–32-character printable ASCII SSID and a 12–63-character printable ASCII password without spaces. These are separate from the router credentials in `WIFI_CONFIG`.

De veldnamen voor de UART wijken bewust af van de HTTP-veldnamen. `seconds` ligt tussen 60 en 86400. `wifi_cfg` is decimaal 1–65535 en moet overeenkomen met het succesvol toegepaste `WIFI_CONFIG`-frame. Een ontbrekende of afgewezen configuratie geeft `wifi_config_needed`, zonder het portaal te starten. Herhalen stuurt opnieuw configuratie plus start. Oudere hosts zonder `wifi_cfg` behouden hun bestaande AP-gedrag. Zonder een actief `PORTAL_START`-verzoek starten `WIFI_CONFIG`, `CONFIG_SNAPSHOT` en `CONTACT` geen WiFi.

UART field names intentionally differ from HTTP fields. `seconds` is 60–86400. `wifi_cfg` is decimal 1–65535 and must match the successfully applied `WIFI_CONFIG` frame. A missing/rejected matching configuration returns `wifi_config_needed` without starting the portal. Retries resend configuration and start together. Older hosts omitting `wifi_cfg` retain their existing AP behavior. Without an active `PORTAL_START` request, `WIFI_CONFIG`, `CONFIG_SNAPSHOT` and `CONTACT` do not start WiFi.

De STM32 is eigenaar van blijvende instellingen. De ESP ontvangt alleen de benodigde waarden voor de actieve sessie. Wijzigingen worden door de normale save-route gevalideerd; verbindingspogingen, statusmetingen en logs veroorzaken geen configuratieschrijfacties.

The STM32 owns persistent settings; the ESP receives values needed for the active session. Changes pass through normal save validation. Connection attempts, observations and logs do not write configuration records.
