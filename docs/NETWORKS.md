# LoRaWAN: UG65 en TTN / UG65 and TTN

## Nederlands

### Eén netwerk tegelijk

Versie 4.9 gebruikt één OTAA-configuratie. Dat kan de ingebouwde netwerkserver
van een Milesight UG65 zijn, of The Things Network. TTN heeft geen andere
applicatiepayload nodig. TTN is nog niet live getest met deze installatie.

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

### Twee netwerken met prioriteit — ontwerp, nog niet geïmplementeerd

De gewenste keuzen zijn mogelijk: alleen privé, alleen TTN, privé → TTN, of TTN →
privé. Daarvoor zijn **twee onafhankelijke OTAA-profielen** nodig: elk een
JoinEUI/AppKey en passende regio-/RX2-instellingen. Geen gedeelde AppKey gebruiken.
Er is één actieve LoRaWAN-sessie: beide netwerken tegelijk ontvangen kan niet.

Een veilige implementatie hoort:

- eerst de voorkeur te proberen, met begrensde joinpogingen en wachttijden;
- alleen op ontbrekende bevestigingen/netwerkantwoorden falen, niet op het
  ontbreken van een willekeurige downlink of een lokale `TX_DONE`;
- na meerdere gemiste, gespreide controles naar profiel 2 te gaan en opnieuw OTAA
  te joinen; de MCU hoeft daarvoor normaal niet opnieuw op te starten;
- op de reserveverbinding hooguit dagelijks één begrensde poging naar de voorkeur
  te doen en bij mislukken terug te keren; tijdens join is bediening onderbroken;
- DevNonce-/framecounterbescherming te behouden, instellingen atomair te bewaren
  en joinlussen of voortdurend flashschrijven te voorkomen;
- een TTN-airtime/ACK-budget te hanteren dat ook de eigen schakelberichten meetelt.

Dit is **geen actieve functie in 4.9**. Er zijn nog geen tweede credentials of
live failovertests. Voor nu wijzig je de ene netwerkconfiguratie via WiFi en
join je opnieuw. De firmware slaat dit op en herstart na bewaren.

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
JoinEUI/AppKey-profielen om concurrerende joins te vermijden. Deze gatewaymodus
implementeert niet automatisch de nog ontbrekende node-fallback. Schakel niet
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

## English — node registration and failover

Version 4.9 supports **one active OTAA configuration**, usable with a private
UG65 network server or TTN. Live TTN testing is pending. The node selects a
LoRaWAN network identity, not a gateway IP; gateways belonging to the same
network do not require device-side failover.

For the older tested private UG65 use compatibility profile 1.0.2, matched Class A/C, RX1
delay 1 s/offset 0, RX2 869525000 Hz/DR0, matching DevEUI/JoinEUI/AppKey, FPort 10.
Use your own unique credentials, never an example device's identity or key.

On TTN create an application and manually register a device: matching regional
frequency plan, LoRaWAN 1.0.4. This RUI build's LoRaMAC headers specify
RP002-1.0.3; recheck when changing BSP versions. Configure its
DevEUI, JoinEUI and a unique AppKey on the node. All-zero JoinEUI is accepted,
but all-zero DevEUI/AppKey is not. Leave node RX2 overrides disabled. Enable
Class C at both ends for prompt downlinks; send an uplink after joining first.
Use the included `decodeUplink` JavaScript formatter. Queue unconfirmed hex
downlinks on the configured application port, with matching node permissions.

TTN Sandbox permits 30 s uplink airtime and 10 downlinks including ACKs per day.
Neither 96 confirmed checks/day nor 96 SF12 status packets/day fit that budget.
Public first-boot defaults use a four-hour status interval, not a promise that
all possible event traffic fits. Account for actual airtime and extra events.

**Priority/failover is planned, not implemented in 4.9.** It needs two distinct
OTAA profiles, bounded retries, verified network health, protected persistent
nonces and airtime-aware checks. Switching requires a fresh join, normally no
MCU reboot; there is a receive gap. A daily preferred-network retry is feasible,
but must return to the fallback on failure. An unconfirmed TX completion is not
proof that the server received it. Current manual configuration changes reboot
the node after saving.

## Sources

- [TTN classes](https://www.thethingsnetwork.org/docs/lorawan/classes/)
- [TTN duty cycle and Sandbox fair use](https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/)
- [The Things Stack Class C setup](https://www.thethingsindustries.com/docs/hardware/devices/configuring-devices/class-c/)
- [RAK RUI3 LoRaWAN API](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/lorawan/)
