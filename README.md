# Victron LoRaBLE Remote

**Zet je modem, router of andere apparatuur pas aan wanneer je die nodig hebt.**

[English](README.en.md) · [Download](https://github.com/roelbroersma/victron-lorable-remote/releases/latest) · [Installatie](docs/INSTALL.md) · [Handleiding 4.12.1](docs/MANUAL.md) · [LoRaWAN instellen](docs/NETWORKS.md) · [Voorbeelden](docs/EXAMPLES.md)

LoRaBLE Remote ontvangt een LoRaWAN-opdracht en bedient een apparaat via **Bluetooth of een relais**. Zo kan bijvoorbeeld je 4G/5G-modem uit blijven tot je op afstand verbinding nodig hebt. Een lokale spanningsingang kan dezelfde functies activeren.

Gebruik een eigen LoRaWAN-netwerk — bijvoorbeeld met een **Milesight UG63 / UG65** — of **The Things Network (TTN)**. Je kunt vier netwerken instellen, op prioriteit zetten en automatisch laten wisselen. Eén netwerk is tegelijk actief.

## Snel beginnen

1. Download de **Windows.zip** bij de [laatste release](https://github.com/roelbroersma/victron-lorable-remote/releases/latest) en pak alles uit.
2. Sluit je **RAK11162** via USB aan en open **Install.cmd**. De wizard begeleidt je door de installatie.
3. Verbind met WiFi **Victron LoRaBLE Remote**, wachtwoord **CHANGE-ME-FIRST**, en open **http://192.168.4.1/**. Wijzig het wachtwoord meteen.
4. Kies je apparaat, maak Bluetooth-functies aan, vul je LoRaWAN-gegevens in en klik **Opslaan**.

Eerste installatie gebruikt een Windows-pc met USB én WiFi; de wizard regelt de tijdelijke WiFi-verbinding. Daarna werk je bij met **één compleet .bin-bestand**, via USB of **Beheer → Firmware bijwerken**. Instellingen blijven behouden. [Installatie in stappen →](docs/INSTALL.md)

## Wat kun je bedienen?

| Apparaat | Functies |
|---|---|
| **Victron Smart MPPT** | Acht regelmodi voor de LOAD-uitgang |
| **Victron Smart BatteryProtect** | AAN/UIT; 12/24V-100A (product A3B1), of 48V-100A (product A3B3, bevestigd op firmware v2.11) |
| **Generic Bluetooth** | Eigen GATT-service, characteristic en opdrachtbytes |
| **Relais / droog contact** | Aan, uit of een puls met instelbare duur |

Maak maximaal **tien benoemde Bluetooth-functies** voor één doelapparaat. Koppel die aan LoRaWAN-opdrachten of de opgaande/neergaande flank van een ingang. Per actie bepaal je wat mag worden bediend.

De Nederlands/Engelse webinterface biedt status, recente gebeurtenissen, handbediening, energie-inschatting en veldhulp. WiFi kan na opstart of een trigger tijdelijk beschikbaar blijven.

## Instellen en controleren in 4.12.1

- **WiFi naar keuze:** direct accesspoint, eigen 2,4GHz-router via DHCP, of geheel uit. Routermodus wacht eerst op de router en probeert verbinding zonder AP. Pas bij mislukking of verbindingsverlies verschijnt AP-terugval op **192.168.4.1**, binnen het WiFi-venster. Na DHCP-succes gaat het AP uit; open het toegewezen IP-adres van dit board.
- **LoRa-vermogen:** stel een maximum van **0–22 dBm** in; standaard **14 dBm**. ADR en regionale beperkingen kunnen het vermogen verlagen. **0 dBm is 1 mW**, niet radio uit. De status toont het laatst ingestelde radiovermogen, geen meting.
- **Ingangstiming:** geef iedere flank een minimale AAN-/UIT-tijd en een extra actievertraging. Bijvoorbeeld 5 + 10 seconden vraagt 15 seconden onafgebroken dezelfde toestand. Een tegengestelde flank annuleert de wachtende actie; beide waarden 0 geven geen extra wachttijd na debounce.
- **Duidelijke diagnose:** radioacceptatie, lokale TX en netwerk-ACK staan apart. RSSI/SNR horen bij de laatste geldige ontvangst, met profiel en ouderdom. Bluetooth toont het resultaat van de laatste opdracht en bij fouten de betreffende stap.
- **Handmatig testen:** **Nu verzenden (ACK)** vraagt één bevestigd statusbericht; **Nu aanmelden** vraagt een nieuwe aanmelding op het voorkeursprofiel. Een aanvraag is nog geen geslaagde verzending of aanmelding. Lees de [uitleg en aandachtspunten](docs/MANUAL.md#handmatig-testen).

[Handleiding: WiFi, Bluetooth, ingangsflanken, LoRa en beheer →](docs/MANUAL.md)

<details>
<summary>Bekijk de webinterface: status, netwerken en beheer</summary>

![Status met voorbeeldinstellingen](docs/images/status.png)

![Netwerkprofielen en prioriteit](docs/images/networks.png)

![Back-up, herstellen, bijwerken en herstarten](docs/images/manage.png)

</details>

## Hardware

| Onderdeel | Functie |
|---|---|
| **RAK11162** met RAK11160-module | LoRaWAN, WiFi en Bluetooth |
| **RAK19010 + RAK19012** | Basisboard met USB/LiPo/solar-voeding en USB-programmeeraansluiting |
| **RAK19016**, alternatief na installatie | 5–24V-voedingsmodule |
| **RAK13001**, optioneel | Eén geïsoleerde 12–24V DC-input en één output/relais |
| **RAK13007**, optioneel | Eén output/relais, geen ingang |
| Antennes | LoRa en 2,4GHz |

Selecteer de gemonteerde I/O-module zelf; zonder I/O-module werkt LoRaWAN → Bluetooth ook. Gebruik één voedingsmodule per power-slot.

De RAK13001-ingang detecteert **12–24V DC aanwezig/afwezig**, geen accuspanning. Controleer de jumpers: input **WB_IO3**, relais **WB_IO4**. Sluit nooit 12V rechtstreeks op een processorpin aan. Relaiscontacten leveren zelf geen voeding; de relais verbruiken spoelstroom zolang ze aangetrokken zijn.

## Een opdracht sturen

Stuur **HEX** op de ingestelde FPort, standaard **10**, en schakel de bijbehorende ontvangstrechten in.

| HEX | Actie |
|---|---|
| `01` … `09`, `0A` | Bluetooth-functie 1 … 10 |
| `10` / `11` | Relais uit / aan |
| `12` | Relaispuls |
| `20` | Status opvragen |

Functie 10 is **`0A`**, niet `10`. Voor bediening zonder wachten op een uplink gebruik je **Class C** op board én netwerkserver. [TTN, Milesight en de payloadcodec instellen →](docs/NETWORKS.md)

## Goed om te weten

- **Opslaan is expliciet:** ook een nieuwe netwerkvolgorde wordt pas toegepast met Opslaan. Logs en uptime blijven in RAM.
- **Back-ups bevatten geen geheime gegevens:** bewaar AppKeys, Bluetooth-PIN en de wachtwoorden van accesspoint én router apart. De nieuwe instellingen gaan mee in back-ups; oudere back-ups krijgen daarvoor standaardwaarden.
- **Energie:** de winst zit in apparatuur die uit kan blijven. Class C luistert vrijwel continu en is geen microampère-slaapstand. De interface toont een dagraming in mAh of Wh.
- **Bluetooth:** WiFi pauzeert tijdens een opdracht. MPPT User defined/AES gebruikt bestaande VictronConnect-drempels. Kies voor BatteryProtect de juiste variant: 12/24V-100A (A3B1) of 48V-100A (A3B3). De productcode wordt vóór schakelen gecontroleerd; modus en echte uitgangsstatus worden teruggelezen. Beveiligingsdrempels en BMS-modus blijven ongemoeid.

Voed het board onafhankelijk van de geschakelde belasting en houd de gateway bereikbaar als je modem uitstaat. Gebruik passende zekeringen en een lokale uitschakelmogelijkheid; dit is geen veiligheidscontroller. Houd de voeding aangesloten tijdens updates.

Kies zendvermogen en testfrequentie passend bij de toegestane toepassing, antenne en netwerkregels. Een instelbare grens van 22 dBm betekent niet dat dit vermogen overal is toegestaan.

[Arduino-sketch](stm32/stm32.ino) · [Bouwen en broncode](docs/DEVELOPMENT.md) · [Hulp / issues](https://github.com/roelbroersma/victron-lorable-remote/issues)

© 2026 Roel Broersma · [MIT](LICENSE) · [Bibliotheeklicenties](THIRD-PARTY-NOTICES.md). Onafhankelijk project; niet verbonden aan Victron Energy, RAKwireless of Milesight.
