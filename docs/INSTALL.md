# Installeren en bijwerken / Installation and updates

## Eerst kiezen / Choose your starting point

| Toestand / State | Route |
|---|---|
| LoRaBLE native ESP 4.8 of nieuwer / or newer | STM32 .bin via USB, ESP .packed via de browser / browser |
| Nieuwe module met ESP-AT / Factory ESP-AT | Experimentele bootstrap hieronder / experimental bootstrap below |
| Eerste defecte native 4.7-updater / Broken first native 4.7 updater | Niet geschikt voor deze OTA-route / not compatible with this OTA route |
| Andere module, klok of flashindeling / Different module, clock or layout | Stop; niet flashen / do not flash |

**4.9 is een testrelease, geen universele éénklikinstallatie.** De werking is op
één bestaande RAK11162-opstelling getest. De installatie op een nieuwe
fabrieksmodule is nog een afzonderlijke acceptatietest.

## Nederlands — bestaande installatie

1. Download en pak het hele releasepakket uit. Bewaar eerst een instellingenexport
   en de afzonderlijke geheime gegevens. Lees de releasebeperkingen.
2. Sluit de USB-data-aansluiting van het RAK-basis-/voedingsboard aan. Geen ESP
   testadapter nodig voor een STM32-update. Sluit Arduino Serial Monitor.
3. Gebruik de officiële **RAK Device Firmware Upgrade Tool** (Windows .exe), of
   `installer/Start-USB-Flash.cmd` als de RAK RUI-uploader al geïnstalleerd is.
   De wrapper controleert SHA-256, vraagt de juiste COM-poort en vraagt bevestiging.
   Hij bundelt geen RAK-executable en installeert geen drivers.
4. Kies uitsluitend `LoRaBLE-STM32-4.9.0.bin` voor deze USB-route. Wacht op
   `Upgrade Complete`; voeding niet onderbreken. Geen erase-all gebruiken.
5. Verbind opnieuw met de WiFi. Open **Beheer → Firmware bijwerken**.
   Kies `LoRaBLE-ESP8684-4.9.0-rc1.packed` en upload terwijl Bluetooth niet bezig is.
6. Laat de voeding staan; de ESP herstart. Verbind opnieuw en controleer onder
   Beheer beide versies: STM32 4.9.0 en ESP 4.9.0-rc1. Controleer ook je instellingen.

De twee bestanden horen bij twee verschillende processoren. Een ESP .bin is
**niet** geschikt voor de browserupload en een STM32 .bin is **geen** ESP-image.
De browser werkt alleen de WiFi/Bluetooth-processor bij. Er is één ESP-applicatieslot
en geen automatische rollback. Onderbreking tijdens overschrijven door de behouden
bootloader kan hardwareherstel nodig maken. Download alleen vertrouwde releases;
de checksums zijn integriteitscontroles, geen digitale handtekening.

### Nieuwe module zonder testpinnen: experimenteel

De STM32 kan de fabrieks-ESP via de interne UART opdracht geven een image via
WiFi te downloaden (`AT+USEROTA`). Dat heeft op het ontwikkelboard eerder de eerste
native applicatie geïnstalleerd. De daaropvolgende testpin-herstelactie was nodig
door een fout in die **oude native updater**, niet omdat WiFi-installatie altijd
onmogelijk is. De huidige native updater is hersteld en herhaaldelijk getest.

**Nog niet bewezen:** deze nieuwste build van fabrieksfirmware naar native op een
ongebruikte module. De bootstrapcode inspecteert niet zelfstandig alle fysieke
bootloader-/partition-table-bytes. Hij is daarom uitsluitend een ontwikkelhulpmiddel,
geen firmwarebestand dat beginners blind moeten flashen. Er zit bewust geen
algemene “installeer op ieder nieuw board”-knop in deze testrelease.

Technische route voor de nog uit te voeren acceptatietest:

1. Bevestig ESP32-C2/ESP8684, 26MHz, 2MB en de passende fabrieksfirmware/bootloader.
   De geteste originele indeling heeft storage `0x2A000/0xA6000` en app
   `0xD0000/0x130000`. Een AT-versienummer alleen bewijst die indeling niet.
2. Bouw `tools/build.ps1 -Public -Mode Bootstrap` en flash de STM32 via USB.
   Lees de console; `ATC+MIGRATE=1` bereidt alleen tijdelijk WiFi/AT voor.
3. Verbind de pc met die WiFi. De huidige ontwikkelhulp verwacht pc-adres
   `192.168.4.2` en een gecontroleerd image op poort 8765, aangeboden door
   `tools/serve-esp-ota.py` met zijn verwachte SHA-256. Controleer die adressen eerst.
   Geef zo nodig uitsluitend de lokale verbinding firewalltoegang; schakel de
   firewall niet algemeen uit.
4. `ATC+MIGRATE=2` controleert de server; stap `3` vraagt de daadwerkelijke ESP-update
   aan. Niet herhalen bij onduidelijke uitkomst; voeding niet onderbreken.
5. Na bevestigde native opstart kiest stap `4` de companion-route. Installeer daarna
   de normale publieke STM32-build en controleer WiFi, bewaren, Bluetooth en OTA.

Bewijs deze volledige route op een tweede fabrieksmodule voordat een stabiele
“zonder testpinnen”-release wordt uitgebracht. Flash **nooit** de gegenereerde
ESP-IDF bootloader/partitietabel of een generieke merged image in deze opstelling.

### Zelf bouwen / Arduino IDE

Installeer Arduino IDE 2 / Arduino CLI en de officiële RAK RUI STM32 BSP **4.2.4**
via de [RAK quickstart](https://docs.rakwireless.com/product-categories/wisduo/rak11160-module/quickstart/).
Open `stm32/stm32.ino`. Alle .h/.cpp-bestanden in die map zijn nodig; alleen de
.ino kopiëren is niet voldoende. Board: RAK11160, LoRa support: **Support LoRaWAN**,
LA915 uit. Kies de regio in de webinterface; de fysieke radio/antenne moet passen.

De volledige UI heeft C++-LTO nodig om te passen. `tools/build.ps1 -Public` stelt
de geteste compileropties in en bouwt zonder flashen. Voor bouwen vanuit de IDE
zelf staat hetzelfde in `arduino/platform.local.txt`: plaats dit na een back-up
naast `platform.txt` van uitsluitend BSP 4.2.4, herstart de IDE. Deze override
geldt ook voor andere sketches met dat BSP; verwijder/herstel hem als je klaar bent.
De IDE-route met handmatig geplaatste override is nog niet interactief getest;
de overeenkomstige CLI-build is wel getest.

Publieke builds negeren `settings.local.h`. Voor eigen vaste installatiegegevens
gebruik je het voorbeeldbestand en een niet-publieke build, die je nooit publiceert.
Voor de ESP: installeer ESP-IDF 5.5.5, activeer zijn omgeving en gebruik
`esp8684/build.ps1 -IdfRoot <jouw-esp-idf-map>`. Doel ESP32-C2, 26MHz/2MB.
Pak de applicatie met `tools/pack-esp-ota.py`; gebruik geen algemene `idf.py flash`.

## English

For an existing native 4.8+ installation, export settings and retain secrets
separately. Connect the board's normal USB data port, close Serial Monitor and
flash the **STM32 .bin** using the official RAK Device Firmware Upgrade Tool or
the included Windows wrapper (requires an installed RAK uploader). Wait for
Upgrade Complete. Reconnect WiFi, open Manage, upload the matching **ESP .packed**,
keep power connected and verify both running versions afterward.

The wrapper verifies SHA-256 and asks for port/confirmation; it is not a bundled,
signed all-in-one executable or a driver installer. STM32 still needs USB.
ESP OTA has no automatic rollback or signature. Never upload the wrong processor's
image, an IDF-generated bootloader/table or a generic merged binary.

Factory-to-native installation without test pins is an **experimental internal
UART + stock USEROTA path**, not a validated universal installer. It previously
installed the first native app; a bug in that old native updater later required
testpad recovery. The latest native updater has been tested, but a fresh-module
bootstrap acceptance test is still required. The four migration steps above are
developer-only; matching factory bootloader/layout must be verified independently.
Do not blindly try it on another ESP-AT release or flash layout.

Source builds require RUI BSP 4.2.4 (RAK11160, LoRaWAN only, LA915 off) and the
complete `stm32` folder. The tested CLI script applies the required C++-LTO flags.
An optional IDE platform override is supplied; back up any existing override,
remember it affects other BSP sketches, and restore it afterward. ESP requires
IDF 5.5.5, C2/26MHz/2MB. Public builds deliberately exclude local secrets.

References: [RAK USB setup/DFU](https://docs.rakwireless.com/product-categories/wisduo/rak11160-module/quickstart/),
[Espressif USEROTA](https://docs.espressif.com/projects/esp-at/en/release-v3.3.0.0/esp32c2/Compile_and_Develop/How_to_implement_OTA_update.html).
