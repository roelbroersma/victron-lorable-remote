# LoRaWAN: UG65 en TTN / UG65 and TTN

## Nederlands

### Eén netwerk tegelijk

Versie 4.10 bewaart vier OTAA-netwerkprofielen en gebruikt één sessie tegelijk.
Dat kan de ingebouwde netwerkserver van een Milesight UG65 zijn, TTN of een
andere LoRaWAN-server. Dezelfde applicatiepayload/codec werkt op beide.
TTN zelf is nog niet live getest met deze installatie.

Een gateway is de radioverbinding; een netwerkserver beheert de sessie. Het
board kiest dus niet een gateway-IP. Meerdere gateways van **hetzelfde** netwerk
kunnen dezelfde uplink doorgeven zonder dat het board opnieuw hoeft te joinen.
Een UG65 die naar TTN doorstuurt, is iets anders dan een UG65 met een eigen
ingebouwde netwerkserver.

### UG65: ingebouwde netwerkserver, EU868

Onderstaande instellingen zijn een reproduceerbaar voorbeeld, geen export van
een installatie. Namen van menu's kunnen per UG65-firmware verschillen.

| Onderdeel | Instelling |
|---|---|
| Region/frequency plan | EU868; ook EU868 in de node |
| Device profile: LoRaWAN MAC | 1.0.2 als compatibiliteitskeuze op de oudere geteste UG65; niet 1.1. RUI 4.2.4 zelf meldt 1.0.4 |
| Class | C voor snelle bediening; A voor ontvangst na een uplink. Beide kanten gelijk |
| OTAA | Aan; geen ABP-sessiesleutels invullen |
| RX1 delay / offset | 1 seconde / 0 |
| RX2 channel frequency | 869525000 Hz |
| RX2 datarate | DR0, SF12/BW125, voor dit private-netwerkvoorbeeld |
| Frequency list / CFList | Standaard/leeg als je geen extra kanalen toevoegt |
| Application | Maak `LoRaBLE Remote` aan, wijs de codec toe |
| Device EUI | De automatisch uitgelezen DevEUI van **jouw** RAK |
| AppEUI / JoinEUI | Dezelfde 16 hextekens in gateway en node |
| AppKey | Unieke 32 hextekens voor dit apparaat; beide kanten gelijk |
| Application FPort | 10 standaard; in node en downlink hetzelfde |
| Payload codec | [JavaScript](../stm32/ug65_payload_codec_v4.js), functie `Decode(fPort, bytes)` |

Na aanmelden moet behalve Join Request/Join Accept ook een **uplink** verschijnen.
Een decoder kan een radiopakket niet tegenhouden; fout decoderen en geen uplink
zijn verschillende problemen. De node-status `Joined` of `TX_DONE` bewijst niet
dat een bepaalde applicatie-uplink bij de server is aangekomen.

### TTN / The Things Stack

1. Maak in de TTN Console een application en een handmatig geregistreerd end device.
2. Kies het frequentieplan voor je land en hardware. Voor Nederland: EU863–870 MHz.
   Voor deze RUI-build: LoRaWAN 1.0.4. De meegeleverde LoRaMAC-headers noemen
   RP002-1.0.3 als regionale versie; controleer dit bij een andere BSP-versie.
3. Gebruik de DevEUI van het board, een eigen JoinEUI en een nieuw gegenereerde
   AppKey. `0000000000000000` als JoinEUI wordt door de firmware geaccepteerd;
   DevEUI en AppKey mogen niet nul zijn. Volg ook de registratie-eisen van je server.
4. Vul dezelfde waarden in bij LoRaWAN op de node. Laat RX2 op automatisch tenzij
   je netwerkbeheerder expliciet iets anders opgeeft. TTN kan de ontvangparameters
   bij/na join instellen; kopieer geen geforceerde UG65-RX2-override naar TTN.
5. Zet Class C-ondersteuning aan bij het device als de node Class C gebruikt.
   Na OTAA is eerst een uplink nodig voordat The Things Stack Class C-downlinks stuurt.
6. Payload formatters → Uplink → Custom JavaScript:
   plak hetzelfde codecbestand. De wrapper `decodeUplink(input)` is al aanwezig.
7. Queue een **unconfirmed** downlink op FPort 10. Payload is hex, bijvoorbeeld
   `01` voor functie 1. Schakel die toestemming ook op de node in.

Let op het TTN Sandbox fair-usebeleid: 30 seconden uplink-zendtijd per dag en
maximaal 10 downlinks per dag, inclusief ACKs. Elke 15 minuten een confirmed
controle zou 96 ACKs vragen en past daar niet in. Ook 96 **unconfirmed** berichten
bij SF12 (ongeveer 1,81 seconde elk) overschrijden 30 seconden. De publieke
first-boot-configuratie gebruikt daarom vier uur tussen statusberichten; ook dan
moet je gebeurtenissen, joins, retries en werkelijke datarate meetellen.

### Netwerkprofielen, prioriteit en preempt in 4.10

Open **LoRaWAN**. De bestaande 4.9-aanmeldgegevens migreren automatisch naar
het eerste profiel. Elk van de vier profielen heeft een naam, aan/uit, netwerktype,
JoinEUI, AppKey, preempt-tijd en optionele RX2-instelling. DevEUI, radio-regio,
subband, Class, FPort en opdrachttoestemmingen zijn gemeenschappelijk.

Verplaats de kaarten met **↑ / ↓** (ook op mobiel/toetsenbord) of sleep de **↕**.
Bovenaan = hoogste prioriteit. Uitgeschakelde profielen worden overgeslagen.
De sleutel blijft bij hetzelfde profielslot; verplaatsen verwisselt geen sleutels.
Een leeg AppKey-veld behoudt de sleutel van dat slot. AppKeys zijn niet uitleesbaar
of aanwezig in exports. Gebruik verschillende JoinEUIs voor ingeschakelde profielen
en eigen sleutels per netwerk. Alle wijzigingen worden pas na Opslaan actief.

| Voorbeeld | Type | Aan | Preempt |
|---|---|---|---|
| 1. Eigen UG65 | Eigen / ander | Ja | Niet van toepassing zolang hoogste prioriteit |
| 2. TTN | TTN Sandbox | Ja | 1440 minuten = 24 uur |
| 3–4 | Naar behoefte | Nee | 1440 minuten standaard |

**Twee netwerken:**

1. Bij starten probeert het board het hoogste ingeschakelde profiel eenmaal.
2. Bij een mislukte join gaat het naar profiel 2, met minstens 60 seconden tussen
   die pogingen. Regionale duty-cyclebeperkingen kunnen extra wachttijd afdwingen.
3. De preempt-timer begint bij selectie van het reserveprofiel, **niet pas bij
   succesvolle aanmelding**. Tot het einde blijft het daar: verbonden, of opnieuw
   proberend (eigen netwerk elke 5 minuten; TTN minimaal elk uur binnen zijn budget).
4. Na afloop probeert het opnieuw vanaf de hoogste prioriteit. Een werkende
   reserveverbinding wordt daarvoor verlaten: bediening is tijdens de nieuwe join
   tijdelijk niet mogelijk. Het board zelf hoeft niet opnieuw te starten.
5. Faalt de voorkeur opnieuw, dan gaat het terug naar de reserve met een nieuwe
   preempt-periode. Bij succes blijft het op de voorkeur zonder preempt-timer.

**Drie/vier netwerken:** bij preempt worden hogere profielen op volgorde één keer
geprobeerd. Als die falen, keert het terug naar de voorheen werkende reserve.
Was de reserve onbereikbaar, dan krijgt de volgende lagere reserve een beurt.
Na de laatste begint de reserveronde opnieuw. Zo kan een onbereikbaar profiel 2
profiel 3 niet voor altijd blokkeren. Preempt is instelbaar van 15 minuten tot 7 dagen.

**Bereikbaarheidscontrole:** standaard elke 240 minuten één confirmed statusuplink,
zonder automatische retransmissies. Alleen een echte ACK of ontvangen downlink
bevestigt de verbinding; gewone TX_DONE doet dat niet. Na twee gemiste controles
wordt opnieuw gejoined of de reserve gekozen. Op een reserve geldt nog steeds
de lopende preempt-timer: een verbroken reserve wordt tot die tijd zelf herprobeerd.
0 schakelt deze controle uit: joinfouten worden dan nog afgehandeld, maar een
stil weggevallen netwerk wordt niet automatisch ontdekt. Instelbaar 15–1440 minuten;
TTN gebruikt altijd minimaal 240 minuten.

**TTN Sandbox:** kies expliciet het TTN-type. Periodieke statusberichten worden
op dat netwerk op minimaal 240 minuten begrensd; privé mag een kortere ingestelde
interval gebruiken. Maximaal zes joinpogingen per profiel per 24-uursbudget en
minimaal één uur tussen herhaalde TTN-joinpogingen. Tellers staan in RAM en worden
bij een herstart opnieuw gestart. Dit is **geen volledige airtimeboekhouding**:
ingangsacties, handmatige tests, joinaccepts, MAC-antwoorden en downlinks tellen ook
mee. Frequent herstarten is geen manier om het fair-usebeleid te omzeilen.
Kies geen korte preempt-tijd op TTN voor normaal gebruik; 24 uur is de standaard.
Na zes joins blijft het profiel op zijn budget wachten of wordt een ander profiel
geprobeerd volgens de prioriteitslogica.

**Bewaren en veiligheid:** instellingen gebruiken CRC-gecontroleerde A/B-opslag.
Countdowns, retries en logs schrijven niet naar flash. Credentials worden bij
wisselen alleen in het RAM van de LoRa-stack gewijzigd. De globale DevNonce wordt
voor uitzending in blokken van 16 gereserveerd: na een reboot worden ongebruikte
waarden overgeslagen, nooit hergebruikt. Per netwerk wordt de laatste JoinNonce
apart A/B opgeslagen na een succesvolle join; een instellingenimport zet die niet
terug. De ledger bewaart maximaal acht historische OTAA-identiteiten. Bij volle/
beschadigde opslag, flashfouten of een uitgeputte DevNonce stopt aanmelden veilig.
Wis geen NVM om beveiligingstellers te herstellen. Gebruik geen losse AT+JOIN- of
credentialcommando's naast de profielmanager. Deze integratie is getest/bepaald
voor RAK RUI BSP 4.2.4; een andere BSP moet opnieuw gecontroleerd worden.

Status toont het actieve profiel, aanmelding, volgende poging, resterende
preempt-tijd en gemiste controles. Recente gebeurtenissen zijn maximaal 24 RAM-regels.

## UG65 tegelijk lokaal en TTN / Simultaneous local and TTN forwarding

**NL.** Maak eerst een account op [The Things Network](https://www.thethingsnetwork.org/)
en open de [EU1 Console](https://eu1.cloud.thethings.network/console).
Registreer onder Gateways je UG65 met de **Gateway EUI** uit de gatewaystatus
(niet de DevEUI van de RAK), een eigen uniek gateway-ID en EU863–870 MHz.
Voor Semtech UDP mag `Require authenticated connection` niet verplicht zijn;
UDP gebruikt geen Basic Station API-key en heeft geen TLS-beveiliging.

Ga op de UG65 naar Packet Forwarder → General → Multi-Destination. Bewaar
Embedded NS/localhost voor je lokale netwerk en voeg, als jouw firmware deze
combinatie aanbiedt, een tweede bestemming toe:

| Instelling / Setting | Waarde / Value |
|---|---|
| Type | Semtech |
| Server | eu1.cloud.thethings.network |
| Port Up / Port Down | 1700 / 1700, UDP |
| Gateway EUI/ID in Semtech-forwarder | Exact dezelfde Gateway EUI als bij TTN geregistreerd |
| Radio / Frequency plan | Overeenkomend EU868-kanaalplan voor beide servers |

De UG65-handleiding beschrijft Semtech + Embedded NS als ondersteunde
multi-destinationcombinatie. Controleer firmwareversie en menu's op jouw gateway
voordat je toepast. Verander niet blind de frequentiesynchronisatie: één fysieke
radio kan niet twee conflicterende kanaalplannen tegelijk uitvoeren. Test daarna
zowel TTN Gateway Live Data als een bestaande lokale uplink. Maak vervolgens de
TTN application/end-device volgens de stappen hierboven. Alleen de gateway
registreren registreert **niet** automatisch je RAK.

Dezelfde uplink kan naar beide servers worden doorgestuurd, maar slechts de
server van de actieve sessie kan hem ontsleutelen. Gebruik onafhankelijke
JoinEUI/AppKey-profielen om concurrerende joins te vermijden. De gatewaymodus zelf verzorgt geen node-fallback; daarvoor gebruikt versie 4.10
de bovenstaande profielmanager. Schakel niet
tegelijk ook Basic Station in volgens een andere handleiding: Milesights
Basic Station-procedure vraagt Embedded NS uit te zetten.

**EN.** Register the gateway in TTN EU1 with its Gateway EUI, not the node DevEUI.
For the documented multi-destination combination keep Embedded NS and add a
Semtech UDP destination to EU1 on ports 1700/1700. UDP is unauthenticated and
unencrypted; do not require Basic Station authentication for that gateway.
Confirm your gateway firmware supports the combination, match channel plans,
and check both local and TTN traffic. Separately register the end device.
Two forwarded copies do not create two simultaneous node sessions. Keep OTAA
identities/keys distinct and do not mix this procedure with a Basic Station guide
that disables Embedded NS.

References: [Milesight Semtech/TTN integration](https://support.milesight-iot.com/support/solutions/articles/73000514119-the-things-stack-milesight-gateway-integration-via-semtech-packet-forwarder),
[UG65 user guide, Packet Forwarder](https://resource.milesight.com/milesight/iot/document/ug65-user-guide-en.pdf),
[Basic Station procedure](https://support.milesight-iot.com/support/solutions/articles/73000514079-milesight-gateway-the-things-stack-ttn-integration-via-basic-station).

## English — profiles, registration and preemption

Version 4.10 stores four OTAA profiles, with **one active session**. Names,
enable switches, network type, JoinEUI, AppKey, preemption minutes and optional
RX2 overrides are per-profile; DevEUI, radio region, subband, Class, FPort and
action permissions are shared. Existing 4.9 credentials migrate into slot 1.
Order with ↑/↓ or drag ↕. Slots keep their own keys when moved. Blank AppKey
retains that slot's key. Keys are never returned or exported. Enabled profiles
must have distinct JoinEUIs; use independent keys.

On boot try the highest enabled priority once. After failure select the next
backup, waiting at least 60 seconds before its attempt. A backup stays selected
until its preemption period expires **even if it cannot join**. Private retries
are every 5 minutes; TTN retries no more than once per hour within its join budget,
including across priority switches.
Default preemption is 1440 minutes (24 hours), allowed range 15–10080 minutes.

At expiry try higher priorities from the top. Leave the old session and perform
OTAA again, without rebooting the MCU; there is a receive gap. If higher choices
fail, return to a formerly working backup. If that backup was unreachable,
advance to the next lower backup, wrapping after the last. This avoids starving
profiles 3/4 behind an unreachable profile 2. A successful preferred network has
no preemption timer. Disabled profiles are skipped; all disabled turns LoRa off.

Health checks default to one confirmed status uplink every 240 minutes, no
automatic retransmissions. Only an ACK or received downlink proves connectivity,
not TX_DONE. Two missed checks cause rejoining/fallback; a failed backup is still
retried until its preemption expiry. 0 disables health checks (silent network loss
then remains undetected). Allowed interval 15–1440 minutes; TTN minimum 240.

Select TTN Sandbox type to clamp periodic status and health to at least four
hours, with up to six join attempts per profile per 24-hour RAM budget. Counters
restart on reboot. This is not complete airtime accounting: events, manual tests,
MAC replies, joins and downlinks still consume fair use. Never use rebooting to
evade limits. Prefer 24-hour preemption. TTN Sandbox allows 30 seconds uplink
airtime and ten downlinks including ACKs per day.

On TTN register an application and end device: matching frequency plan, LoRaWAN
1.0.4, RP002-1.0.3 for the included RUI 4.2.4 BSP, board DevEUI, separate JoinEUI/
AppKey. Zero JoinEUI is accepted; zero DevEUI/AppKey is not. Leave RX2 automatic.
Enable Class C at both ends for prompt downlinks and send the initial uplink after
joining. Use the included decodeUplink formatter and unconfirmed hex commands on
FPort 10 with the matching node permission. Live TTN validation is still pending.

Credentials change in LoRa-stack RAM, not persistent settings on every switch.
DevNonce reservations of 16 are persisted before RF; reboot skips unused values.
Per-network JoinNonce uses separate A/B flash records, not imported settings.
There is space for eight historical OTAA identities. Full/corrupt nonce storage,
write failures or nonce exhaustion stop joins safely. Do not erase security
counters or mix standalone AT+JOIN/credential commands with the manager.
Retry/countdown/log state is RAM-only. BSP changes require a fresh audit.

## Sources

- [TTN classes](https://www.thethingsnetwork.org/docs/lorawan/classes/)
- [TTN duty cycle and Sandbox fair use](https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/)
- [The Things Stack Class C setup](https://www.thethingsindustries.com/docs/hardware/devices/configuring-devices/class-c/)
- [RAK RUI3 LoRaWAN API](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/lorawan/)
