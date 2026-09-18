# LoRaWAN — eigen gateway en TTN / private gateway and TTN

[README](../README.md) · [English](#english)

## Nederlands

### Eén actief netwerk, vier profielen

LoRaBLE Remote bewaart maximaal vier OTAA-netwerkprofielen. Gebruik een eigen netwerkserver, bijvoorbeeld die van een **Milesight UG63 / UG65**, **The Things Network (TTN)** of een andere LoRaWAN-server. De gatewaymodellen zijn voorbeelden, geen vereiste.

Een gateway verzorgt de radioverbinding; de netwerkserver beheert je apparaatsessie. Het board kiest geen gateway-IP. Meerdere gateways binnen hetzelfde netwerk kunnen berichten doorgeven zonder een nieuwe aanmelding. Een Milesight-gateway die naar TTN doorstuurt, is dus iets anders dan een eigen lokaal netwerk op die gateway.

### Voorbeeld: Milesight UG63 / UG65 met eigen netwerkserver, EU868

Gebruik firmware met de ingebouwde netwerkserver ingeschakeld. Menunamen en beschikbare MAC-versies verschillen per gatewayfirmware.

| Instelling | Waarde |
|---|---|
| Frequentieplan | EU868, gelijk aan het board |
| Device profile / MAC version | LoRaWAN 1.0.4; als oudere Milesight-firmware alleen t/m 1.0.2 aanbiedt, gebruik 1.0.2 als compatibiliteitsprofiel, niet 1.1 |
| Class | C voor ontvangst zonder eerst een uplink te sturen; A voor ontvangst na een uplink |
| Activation | OTAA |
| RX1 delay / offset | 1 seconde / 0 |
| RX2 frequency | 869525000 Hz |
| RX2 datarate | DR0, SF12/BW125, voor dit lokale voorbeeld |
| Frequency list / CFList | Standaard of leeg wanneer je geen extra kanalen toevoegt |
| Application | Maak bijvoorbeeld `LoRaBLE Remote` aan |
| DevEUI | De automatisch uitgelezen DevEUI van je RAK-board |
| AppEUI / JoinEUI | Dezelfde 16 hextekens op board en server |
| AppKey | Unieke 32 hextekens, gelijk op board en server |
| Application FPort | 10, of je eigen gelijk ingestelde waarde |
| Payload codec | [lorawan-payload-codec.js](../stm32/lorawan-payload-codec.js), functie `Decode(fPort, bytes)` |

Wijs het device profile en de application aan het apparaat toe. Na OTAA moeten ook applicatie-uplinks in de packetlijst verschijnen. Een Join Accept is nog geen bevestiging dat een applicatiebericht is ontvangen. De codec vertaalt ontvangen bytes; hij bepaalt niet of radioverkeer binnenkomt.

### TTN / The Things Stack

1. Maak een application en registreer een end device in de [TTN Console](https://console.cloud.thethings.network/).
2. Kies een frequentieplan passend bij land en hardware. Nederland: **EU863–870 MHz**. Voor deze firmware: **LoRaWAN 1.0.4**, regionale parameters **RP002-1.0.3**.
3. Gebruik de DevEUI van het board, een eigen JoinEUI en een nieuwe AppKey. Registreer het end device op iedere server die je wilt gebruiken; alleen een gateway registreren is niet voldoende.
4. Maak hetzelfde netwerkprofiel op het board. Kies **TTN Sandbox** en laat RX2 op **automatisch**; neem de lokale Milesight-RX2-override niet over.
5. Zet Class C op beide kanten aan voor directe bediening. The Things Stack heeft na een join eerst een uplink nodig voordat Class C-downlinks beschikbaar zijn.
6. Kies **Payload formatters → Uplink → Custom JavaScript** en plak de [codec](../stm32/lorawan-payload-codec.js). De functie `decodeUplink(input)` is inbegrepen.
7. Queue een **unconfirmed** downlink op FPort 10, bijvoorbeeld hex `01` voor Bluetooth-functie 1. Geef die functie ook LoRa-toestemming op het board.

TTN Sandbox heeft een fair-usebudget van **30 seconden uplink-zendtijd en 10 downlinks per apparaat per 24 uur**. ACKs tellen als downlinks. Gebruik de vieruursintervallen als uitgangspunt en tel ook gebeurtenissen, joins en herhalingen mee. Het TTN-profiel begrenst periodieke berichten, maar is geen volledige airtimeboekhouding. [TTN fair use](https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/).

### Prioriteit, fallback en preempt

Open **LoRaWAN**, maak de gewenste profielen en verplaats ze met **↑ / ↓** of slepen. Bovenaan staat de hoogste prioriteit. Elk profiel heeft zijn eigen naam, aan/uit, netwerktype, JoinEUI, AppKey, preempt-tijd en optionele RX2-override. DevEUI, regio, subband, Class, FPort en opdrachttoestemmingen gelden voor het hele board.

| Voorbeeld | Type | Preempt |
|---|---|---|
| 1. Eigen netwerk — bijvoorbeeld Milesight UG63 / UG65 | Eigen / ander | Niet van toepassing zolang dit de hoogste actieve prioriteit is |
| 2. TTN | TTN Sandbox | 1440 minuten = 24 uur |
| 3–4 | Naar behoefte | Standaard 1440 minuten |

- Bij opstart probeert het board de hoogste ingeschakelde prioriteit.
- Mislukt aanmelden, dan volgt een reserve, met minimaal 60 seconden tussen pogingen.
- Een reserve blijft geselecteerd tot zijn **preempt-tijd** verstreken is, ook als aanmelden daar nog niet lukt. Herhalen: eigen netwerk elke vijf minuten; TTN minimaal elk uur binnen zijn joinbudget.
- Na afloop begint het opnieuw bij de hogere prioriteiten. Dat vereist een nieuwe OTAA-aanmelding, geen herstart van het board. Ontvangst is tijdens die overgang onderbroken.
- Falen de hogere profielen, dan keert het terug naar een eerder werkende reserve. Was die onbereikbaar, dan volgt de volgende lagere reserve. Zo blijven ook profiel 3 en 4 bereikbaar.
- Preempt is instelbaar van **15 minuten tot 7 dagen**. Een succesvolle voorkeursverbinding heeft geen preempt-timer. Alle profielen uit schakelt LoRa uit.

Gebruik verschillende JoinEUIs en onafhankelijke AppKeys voor ingeschakelde profielen. Een leeg AppKey-veld behoudt de bestaande sleutel van dat profiel. Sleutels blijven bij hun profiel wanneer je de volgorde wijzigt en worden niet teruggestuurd of geëxporteerd.

Bereikbaarheidscontrole gebruikt standaard iedere **240 minuten** één confirmed statusuplink. Alleen een ACK of ontvangen downlink bevestigt bereikbaarheid; een geslaagde uitzending alleen niet. Na twee gemiste controles volgt opnieuw aanmelden/fallback, rekening houdend met de lopende preempt-timer. Interval 0 schakelt deze controle uit; een stil weggevallen verbinding wordt dan niet ontdekt.

TTN-profielen begrenzen periodieke status en controles op minimaal vier uur, en joins op maximaal zes per profiel per 24-uursbudget. Deze budgettellers staan in RAM; herstarten is geen manier om fair use te omzeilen. Radio-duty-cyclebeperkingen kunnen extra wachttijd veroorzaken.

### Eén gateway voor lokaal verkeer én TTN

Bij een Milesight UG63 / UG65 hangt gelijktijdig doorsturen af van de aanwezige gatewayfirmware. Waar **Multi-Destination** beschikbaar is, behoud je **Embedded NS** voor lokaal gebruik en voeg je een **Semtech UDP**-bestemming toe voor TTN. Voor EU1: `eu1.cloud.thethings.network`, UDP-poorten **1700/1700**. Registreer de gateway bij TTN met zijn **Gateway EUI**, niet de DevEUI van het board.

Semtech UDP gebruikt geen Basic Station API-key of TLS. Volg voor een andere forwardermodus de bijbehorende gatewayhandleiding; meng de Basic Station- en Embedded NS-instructies niet.

Gebruik voor beide servers hetzelfde passende **radiokanaalplan**. Verdeel kanalen niet in “lokaal” en “TTN”: dezelfde radio-ontvangst kan worden doorgestuurd, maar alleen de server van de actieve apparaatsessie kan de applicatiepayload ontsleutelen. Controleer lokale packetdata én TTN Gateway Live Data.

Bewaar geen verouderde schakelopdrachten in serverwachtrijen. Stuur opdrachten via het momenteel actieve netwerk; de eenvoudige commandopayload bevat geen vervaltijd.

[Milesight UG63-handleiding](https://resource.milesight.com/milesight/iot/document/ug63-user-guide-en.pdf) · [Milesight UG65-handleiding](https://resource.milesight.com/milesight/iot/document/ug65-user-guide-en.pdf) · [Milesight Semtech/TTN-integratie](https://support.milesight-iot.com/support/solutions/articles/73000514119-the-things-stack-milesight-gateway-integration-via-semtech-packet-forwarder)

## English

### Network and device registration

Use your own LoRaWAN server — for example, the embedded server of a **Milesight UG63 / UG65** — or a network such as **TTN**. These gateway models are examples, not requirements. The board stores four OTAA profiles and maintains **one active session**. Gateways forward radio packets; network servers manage sessions.

For a private EU868 server, the example above uses OTAA, matching DevEUI/JoinEUI/AppKey, FPort 10, RX1 delay 1 s/offset 0 and RX2 869525000 Hz/DR0. The firmware uses LoRaWAN 1.0.4; older Milesight menus offering only 1.0.2 can use that compatibility profile, not 1.1. Select Class C on both node and server for reception without waiting for an uplink.

For TTN, register an application and end device with the matching regional plan, LoRaWAN **1.0.4** and **RP002-1.0.3**. Enter the board DevEUI and separate JoinEUI/AppKey in the console and a **TTN Sandbox** profile on the board. Leave RX2 automatic. Enable Class C at both ends; The Things Stack needs an initial uplink after joining. Registering a gateway alone does not register your device.

Paste [lorawan-payload-codec.js](../stm32/lorawan-payload-codec.js) into TTN's Custom JavaScript uplink formatter; it includes `decodeUplink`. Milesight uses `Decode` from the same file. Send unconfirmed hex downlinks on FPort 10 and enable the corresponding device permission.

### Priority and preemption

Arrange profiles with arrows or dragging; highest priority is at the top. Each profile has its own credentials, type and preemption interval. DevEUI, region, subband, Class, FPort and action permissions are shared. Use distinct JoinEUIs and independent keys. Blank AppKey retains the existing key; moving profiles does not swap their keys.

After a failed preferred join, a backup stays selected until its preemption time expires, **even while unable to join**. At expiry, higher priorities are tried again without rebooting. Reception pauses during OTAA. If higher profiles fail, a previously connected backup is retried; an unreachable backup yields to the next lower profile. Default preemption is **24 hours**, range **15 minutes–7 days**. Disabled profiles are skipped.

Health checks default to one confirmed status uplink every **240 minutes**. An ACK or received downlink, not transmission completion alone, confirms reachability. Two missed checks cause rejoin/fallback while respecting the backup timer. Interval 0 disables silent-loss detection.

Private retries are every five minutes. TTN retries are at least one hour apart, with six joins per profile per 24-hour RAM budget. TTN periodic status and health intervals are at least four hours. Events, joins and other messages still consume airtime. Sandbox fair use permits **30 seconds uplink airtime and 10 downlinks per node per day**, including ACKs; see [TTN's policy](https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/).

### Gateway forwarding and safe switching

Where supported by the Milesight gateway firmware, keep Embedded NS and add a Semtech UDP multi-destination for TTN. EU1 uses `eu1.cloud.thethings.network` and UDP 1700/1700. Register the **Gateway EUI**, not the device EUI. UDP is not authenticated or encrypted; Basic Station uses a different configuration.

Use a common radio channel plan, not separate “local” and “TTN” channels. Forwarded copies do not create simultaneous device sessions. Confirm both local packet data and TTN live data. Send commands through the active server and remove stale queued commands; command payloads have no expiry timestamp.

Settings use persistent storage; logs and countdowns stay in RAM. LoRaWAN security counters are stored separately from configuration exports. Do not erase those counters or issue standalone AT join/credential commands alongside the profile manager.
