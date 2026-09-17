# Victron LoRaBLE Remote

Een eerste schakelopdracht zonder een permanent draaiend modem.

[English](README.en.md) · [Installeren](docs/INSTALL.md) · [UG65 / TTN](docs/NETWORKS.md) · [Voorbeelden](docs/EXAMPLES.md) · [Teststatus](VALIDATION-v49.md)

## Waarom?

Een 4G/5G-modem of WiFi-router continu voeden om hem op afstand te kunnen bereiken
kost energie. LoRaBLE Remote ontvangt de eerste opdracht via LoRaWAN, of via een
lokale ingang, en schakelt daarna een Bluetooth-apparaat of relais. Daarmee kun
je bijvoorbeeld de voeding van dat modem inschakelen. Na afloop schakel je hem
weer uit. De LoRa-ontvanger en zijn gateway moeten zelf bereikbaar en gevoed blijven.

**Dit is geen microampère-ontvanger:** Class C luistert bijna continu. De huidige
firmware laat ook de STM32 wakker. De datasheetschatting zonder relais is circa
0,88 Wh per dag bij één uur WiFi, exclusief voedingsverliezen, leds en zenden.
Het voordeel is afhankelijk van het verbruik en de uit-tijd van je modem.
[Berekening en bronnen](POWER-BUDGET.md).

## Versie 4.9 — testrelease

Op het testboard is de Smart BatteryProtect 100A via de RAK/ESP UIT en AAN gezet.
Beide toestanden zijn onafhankelijk via pc-Bluetooth teruggelezen. WiFi-OTA van
de ESP werkt met de bestaande WiFi-naam inclusief spaties.

| Onderdeel | Status |
|---|---|
| Smart BatteryProtect 12/24V-100A, product A3B1 | AAN/UIT fysiek getest; modus én uitgangsstatus gecontroleerd |
| SmartSolar MPPT met LOAD | Acht modi geïmplementeerd; nog niet op de echte MPPT getest |
| Generic Bluetooth | GATT-service, kenmerk en 1–20 bytes per functie; geen scripts |
| Bluetooth-functies | Toevoegen met +, maximaal 10, eigen naam en stabiel nummer |
| I/O | Geen / RAK13001 / RAK13007 handmatig selecteerbaar |
| LoRa | Functies 1–10 en relais afzonderlijk toestaan; Class A of C |
| WiFi | Tijdvensters; pauzeert tijdens Bluetooth en keert daarna terug |
| Instellingen | NL/EN, help, export/import; CRC-gecontroleerde A/B-flashopslag |
| Status | Uptime, WiFi-clients/RSSI, doelapparaatcontrole, RAM-log en handmatig testen |
| Stroomweergave | Vaste datasheetschatting met mAh/Wh-keuze, geen verplichte rekeninvoer |
| Updates | STM32 via USB; ESP via browser met passend .packed-bestand |
| TTN | Configuratie en codec aanwezig; live join/downlink nog te testen |
| Automatische netwerkfallback | Ontwerp beschreven, **nog niet geïmplementeerd** |
| Nieuwe module zonder testpinnen | Experimentele interne USEROTA-route; nieuwe fabrieksmodule nog te valideren |

De testrelease is geen veiligheidsvoorziening en niet bedoeld voor onbewaakte
kritieke belastingen. Een LoRa-verzoek is geen aflevergarantie. Gebruik een zekering,
passende bedrading en een lokale uitschakelmogelijkheid.

## Hardware

![Statuspagina met voorbeeldgegevens](docs/images/portal-v49-status.png)

*Interfacevoorbeeld met testgegevens; geen live gatewaymeting.*

| Onderdeel | Functie |
|---|---|
| RAK11162 met RAK11160-module | STM32WLE5 voor LoRa/logica + ESP8684 voor WiFi/Bluetooth |
| RAK19010 | WisBlock-basisboard |
| RAK19012 | USB/LiPo/solar-voeding en USB-programmeerroute |
| RAK19016 | Alternatieve 5–24V-voedingsmodule; niet tegelijk in hetzelfde power-slot |
| RAK13001 | 1 geïsoleerde 12–24V DC-input + 1 output/relais, niet bistabiel |
| RAK13007 | 1 output/relais, geen ingang, niet bistabiel; nog niet fysiek getest |
| Antennes | Passende LoRa-antenne én 2,4GHz-antenne op de juiste connectoren |

Geen I/O-module is ook mogelijk: dan gebruik je alleen LoRa → Bluetooth.
De software herkent deze I/O-modules niet automatisch. Selecteer wat er echt
gemonteerd is. Relaiscontacten leveren geen voedingsspanning; ze onderbreken of
verbinden een extern circuit. Controleer maximale contactstroom, spanning en de
inschakelstroom van je belasting in de moduledatasheet.

**Nooit 12V op een MCU-pin.** RAK13001 is gespecificeerd voor 12–24V DC, niet als
betrouwbare 5V-ingang. Opgaande flank betekent externe spanning aanwezig;
neergaande flank betekent spanning weg. De interne optocoupler werkt omgekeerd.
De firmware verwacht de DI-route op WB_IO3 en het relais op WB_IO4; controleer
de modulejumpers. Booten veroorzaakt op zichzelf geen ingangsflank.

## Instellen

1. Installeer eerst de passende STM32- én ESP-firmware; zie de [installatiekeuze](docs/INSTALL.md).
2. Verbind met de WiFi van het board en open http://192.168.4.1/.
   Nieuwe publieke installaties gebruiken `Victron LoRaBLE Remote` /
   `CHANGE-ME-FIRST`; wijzig dat wachtwoord meteen. WiFi staat na opstart één uur aan.
3. Kies het apparaatprofiel, MAC en echte Bluetooth-PIN. Voor Victron is pairing
   verplicht. Smart BatteryProtect gebruikt automatisch instance 0.
4. Voeg benoemde functies toe met +. Eén profiel/MAC geldt voor alle functies:
   dit zijn maximaal tien opdrachten voor **één doelapparaat**, geen tien apparaten.
5. Kies de I/O-module en stel, indien aanwezig, per flank een Bluetooth-functie,
   statusbericht en/of relaisactie in.
6. Vul eigen LoRaWAN-gegevens in. DevEUI wordt uit het board gelezen.
   Schakel alleen de ontvangen opdrachten in die je wilt toestaan.
7. Een nieuw aangevinkte ontvangstroute kiest Class C in het formulier en toont
   uitleg. Zet de netwerkserver óók op Class C. Je kunt bewust Class A kiezen:
   ontvangst is dan alleen mogelijk na een eigen uplink. Opslaan past wijzigingen toe.
8. Test onder Status eerst zonder aangesloten belasting.

Opslaan bewaart de configuratie en herstart. De publieke first-boot-configuratie
schakelt geen onbekende ingang, uitgang of Bluetooth-route in en bevat geen
installatiesleutels. Bestaande opgeslagen instellingen worden bijwerken behouden.

## LoRa-opdrachten

Hexbytes op de ingestelde applicatie-FPort, standaard 10. Geen ASCII-cijfers.

| Hex | Actie |
|---|---|
| 01 … 09, 0A | Bluetooth-functie 1 … 10 |
| 10 | Relais UIT |
| 11 | Relais AAN |
| 12 | Relaispuls |
| 20 | Status opvragen |

De betrokken functie/uitgang én LoRa-toestemming moeten aan staan. Nummer 10
(decimaal) is byte **0A**, niet 10. Een puls is voor het lokale relais;
Bluetooth-functies voeren hun opgeslagen opdracht uit. Verwijderen kan alleen
vanaf de laatste functie, zodat bestaande downlinknummers niet verschuiven.

De [codec](stm32/ug65_payload_codec_v4.js) bevat zowel UG65 `Decode` als
TTN `decodeUplink`. Schema 4 onderscheidt MPPT-regelmodus van BatteryProtect-
uitgangsstatus en meldt het laatst gevraagde functienummer. Een verzoek en een
geslaagde uitvoering zijn verschillende statussen.

## Bluetooth-beperkingen

- BatteryProtect: alleen de geteste A3B1-productvariant mag schrijven. De andere
  varianten worden veilig geweigerd. BMS-modus en beveiligingsdrempels blijven ongemoeid.
- MPPT: Always on/off, BatteryLife, Conventional 1/2, User defined 1/2 en AES.
  User defined/AES gebruiken de reeds ingestelde spanningsdrempels en tijden.
  Always on/off herstelt geen eerder automatisch programma.
- Generic: volledige service- en characteristic-UUID, hexbytes, schrijven met
  antwoord; optioneel exact teruglezen. Een GATT-writebevestiging is geen bewijs
  dat een mechanische uitgang echt geschakeld heeft.
- Het aantal pogingen omvat de eerste poging. Er wordt alleen na mislukken herhaald.
- WiFi en Bluetooth worden tijdgedeeld. Er is geen permanente Bluetooth-verbinding.

## Flash en privacy

Instellingen: twee afwisselende STM32-records met CRC en terugleescontrole.
Ongewijzigd opslaan schrijft geen nieuw configuratierecord. De maximaal 24
logregels, uptime en waarnemingen blijven uitsluitend in RAM. LoRaWAN kan zelf
noodzakelijke nonces/tellers in NVM bewaren.

Export/import bevat geen AppKey, PIN, WiFi-wachtwoord of DevEUI/JoinEUI. Import
vult eerst een formulier; daarna bewust opslaan. Houd geheime gegevens apart.
De ESP gebruikt één applicatieslot: WiFi-OTA heeft **geen automatische rollback**.
Firmware is niet digitaal ondertekend; installeer uitsluitend vertrouwde bestanden.

## Broncode en bouwen

- Arduino-sketch: [stm32/stm32.ino](stm32/stm32.ino), RUI BSP **4.2.4**.
- Publieke USB-build: `tools/build.ps1 -Public`; sluit `settings.local.h` uit.
- ESP: ESP-IDF **5.5.5**, ESP32-C2, **26MHz, 2MB**, `esp8684/build.ps1`.
- Webbron: `web/index.html`; `node tools/build-web.mjs` genereert het kleine gzip-asset.
- Tests, exacte grenzen en bestanden: [validatie](VALIDATION-v49.md).
- [Release-instructies](docs/RELEASING.md); publiceer nooit de hele werkmap.

MIT voor de eigen projectcode: © 2026 Roel Broersma. Meegebouwde bibliotheken
houden hun eigen voorwaarden; zie [third-party notices](THIRD-PARTY-NOTICES.md).
Onafhankelijk project, niet afkomstig van of goedgekeurd door Victron Energy of RAKwireless.
