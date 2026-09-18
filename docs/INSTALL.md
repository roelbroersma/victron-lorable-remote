# Installeren en bijwerken / Installation and updates

[Terug naar README](../README.md) · [English](#english)

## Nederlands

### Kies de juiste route

| Je board | Installatieroute |
|---|---|
| RAK11162 met oorspronkelijke RAK-fabrieksfirmware | Windows-wizard: USB + tijdelijke WiFi-verbinding |
| LoRaBLE 4.11 of nieuwer, met complete updater | Dezelfde complete `.bin` via USB of de webinterface |
| LoRaBLE 4.10 of ouder | Afzonderlijke migratie nodig; gebruik deze wizard niet als directe upgrade |
| Andere hardware of aangepaste bootloader/flashindeling | Niet geschikt voor dit pakket |

De fabrieksroute ondersteunt **RAK11162 met RAK11160-module**, **ESP-AT 3.3.0.0 / ESP32C2-2MB / SDK 5.0.6** en de oorspronkelijke RAK-flashindeling. De wizard weigert een afwijkende fabrieksversie. Voor een oudere LoRaBLE-installatie: [maak een issue aan](https://github.com/roelbroersma/victron-lorable-remote/issues) met de huidige firmwareversies, zonder wachtwoorden of AppKeys.

### Eerste installatie — Windows

Benodigd: Windows 10/11, PowerShell 5.1, een USB-datakabel, een ingeschakelde WiFi-adapter met DHCP en beheerdersrechten voor een tijdelijke firewallregel. Gebruik de USB-data-aansluiting van het WisBlock-basisboard met RAK19012. Een losse RAK11160-module heeft zelf geen USB-aansluiting.

1. Download **LoRaBLE-Remote-4.11.0-Windows.zip** uit de [release](https://github.com/roelbroersma/victron-lorable-remote/releases/latest) en pak alles uit. Laat de mappen bij elkaar.
2. Koppel geschakelde belastingen los. Sluit het board via USB aan en sluit Serial Monitor of andere programma's die de COM-poort gebruiken.
3. Dubbelklik **Install.cmd**. Kies NL of EN, selecteer de COM-poort en bevestig met `INSTALL`.
4. Sta de Windows-beheerdersvraag toe. Geef desgevraagd locatietoegang voor WiFi-bediening.
5. Laat voeding en venster staan tot **Installatie voltooid** verschijnt.

De wizard herkent het board, plaatst een tijdelijke installatiehulp via USB en verbindt de pc met een uniek, WPA2-beveiligd netwerk van het board. De firmwareoverdracht is lokaal. Daarna wordt de installatie via USB afgerond en verbindt de pc opnieuw met het oorspronkelijke WiFi-netwerk. Is dat niet bereikbaar, dan krijg je een melding om zelf opnieuw te verbinden.

Er is geen thuisnetwerkwachtwoord nodig. WiFi-internet kan tijdelijk wegvallen. De officiële RAK-uploader wordt zo nodig vooraf gedownload (circa 6,6 MB); Arduino IDE en Python zijn niet nodig. De wizard voegt uitsluitend een beperkte tijdelijke firewallregel toe en verwijdert die na afloop. VPN- of bedrijfsbeleid wordt niet uitgeschakeld.

### Verbinden en configureren

Verbind met **Victron LoRaBLE Remote**, wachtwoord **CHANGE-ME-FIRST**, en open **http://192.168.4.1/**. Wijzig het wachtwoord meteen. WiFi blijft standaard één uur na opstart beschikbaar.

Stel daarna Bluetooth, de gemonteerde I/O-module en [LoRaWAN](NETWORKS.md) in. Alle Bluetooth-functies richten zich op hetzelfde gekozen apparaat. Schakel uitsluitend gewenste ingangen, uitgangen en ontvangstrechten in. Controleer de werking eerst zonder aangesloten belasting.

### Bijwerken via WiFi

1. Exporteer instellingen en bewaar geheime gegevens afzonderlijk.
2. Verbind met de WiFi van het apparaat en open **Beheer → Firmware bijwerken**.
3. Kies **LoRaBLE-Remote-4.11.0.bin** en start de update.
4. Laat de voeding aangesloten tijdens upload, installatie en herstart.
5. Verbind opnieuw en controleer versie en status.

### Bijwerken via USB

Pak de Windows-ZIP uit, sluit de gewone USB-data-aansluiting aan en open **Install.cmd**. Een board met complete updater kiest automatisch de USB-route, zonder WiFi-wissel. Je kunt ook rechtstreeks `installer/Start-USB-Flash.cmd` gebruiken.

Handmatig:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\installer\Flash-USB.ps1 -Port COM3 -Firmware .\firmware\LoRaBLE-Remote-4.11.0.bin
```

De `.bin` in het Windows-pakket is hetzelfde bestand als de losse download en de webupdate. Er zijn geen afzonderlijke firmwareonderdelen om te kiezen. Instellingen blijven behouden.

### Bestanden en herstel

- **Windows.zip**: installer, complete firmware en uitleg.
- **.bin**: complete update voor de webinterface of USB-installer.
- **SHA256SUMS**: controlesommen van de downloads. Vergelijk bijvoorbeeld met `Get-FileHash -Algorithm SHA256 .\LoRaBLE-Remote-4.11.0.bin`.
- **Source code**: broncode via GitHub, inclusief de Arduino-sketch; zie [bouwen](DEVELOPMENT.md).

Gebruik het complete `.bin` **niet** met esptool, een generieke RAK-flasher of Arduino Upload: het is een LoRaBLE-container, geen rauw chipimage. Vervang geen bootloader of partitietabel en gebruik geen erase-all. Firmware is niet digitaal ondertekend; checksums detecteren beschadiging, geen onbetrouwbare afzender. Download uit een vertrouwde bron.

Bij een fout: laat voeding aangesloten, bewaar de melding en wis niets. De wizard bewaart voortgang zonder wachtwoorden in `%LOCALAPPDATA%/LoRaBLE-Remote/install-state`. Hervat met hetzelfde firmwarebestand. Bij een onzekere overdracht voert hij geen blinde reset of tweede download uit. Er is geen automatische rollback; onderbroken flashschrijfacties kunnen hardwareherstel vereisen.

Geen COM-poort zichtbaar? Controleer de datakabel en de USB-serieel-driver in Windows Apparaatbeheer. Sluit andere programma's met toegang tot de COM-poort.

## English

### Compatibility

| Board state | Route |
|---|---|
| RAK11162 with original RAK factory firmware | Windows wizard: USB + temporary PC WiFi |
| LoRaBLE 4.11 or later with complete updater | The same complete `.bin` through USB or the portal |
| LoRaBLE 4.10 or earlier | Separate migration required; do not use this wizard as a direct upgrade |
| Other hardware or modified bootloader/flash layout | Not supported by this package |

Factory installation requires RAK11162 with the RAK11160 module, **ESP-AT 3.3.0.0 / ESP32C2-2MB / SDK 5.0.6** and the original RAK layout. Other factory versions are refused. For older LoRaBLE versions, [open an issue](https://github.com/roelbroersma/victron-lorable-remote/issues) with current versions, without passwords or AppKeys.

### First installation

Use Windows 10/11, PowerShell 5.1, a USB data cable, enabled WiFi using DHCP and administrator access for one restricted temporary firewall rule. Connect through the WisBlock baseboard and RAK19012 USB data port; the bare RAK11160 module has no USB connector.

1. Download **LoRaBLE-Remote-4.11.0-Windows.zip** and extract the entire ZIP.
2. Disconnect switched loads, connect USB and close Serial Monitor.
3. Open **Install.cmd**, select NL or EN and the COM port, then type `INSTALL`.
4. Allow Windows elevation and location access for WiFi control when requested.
5. Keep power and the window open until **Installation complete**.

The wizard identifies the board, installs a temporary USB helper, connects the PC to a unique WPA2-protected device network, transfers firmware locally and finishes over USB. It reconnects to the previous WiFi network afterward; if unavailable, select your network manually.

No home WiFi password, Arduino IDE, Python or separate programming pins are needed for this factory route. WiFi internet can briefly disconnect. The official RAK uploader may need a one-time 6.6 MB download before switching WiFi. Temporary network settings and the restricted firewall rule are cleaned up; VPN/company security policies are not disabled.

Connect to **Victron LoRaBLE Remote**, password **CHANGE-ME-FIRST**, open **http://192.168.4.1/** and change the password immediately. WiFi defaults to one hour after startup. Configure Bluetooth, fitted I/O hardware and [LoRaWAN](NETWORKS.md). Enable only intended actions and check operation with loads disconnected.

### Updates

**WiFi:** export settings, retain secrets separately, open **Manage → Firmware update**, select the complete **LoRaBLE-Remote-4.11.0.bin** and upload. Maintain power through restart, reconnect and check version/status.

**USB:** extract the Windows package, connect normal board USB and open **Install.cmd**. An existing complete updater uses USB only; no WiFi switch is required. The direct launcher is `installer/Start-USB-Flash.cmd`.

The same complete file is used for both routes. Settings are retained. Download checksums are in **SHA256SUMS**; source and Arduino build instructions are under [Development](DEVELOPMENT.md).

### If installation stops

Keep power connected and retain the error message. Progress is recorded without passwords under `%LOCALAPPDATA%/LoRaBLE-Remote/install-state`; resume with the same firmware file. An uncertain transfer is not blindly reset or repeated. Check the USB data cable, serial driver and other programs using the COM port.

The complete file is not a raw chip image: do not use generic flashers, Arduino Upload, erase-all or replacement bootloaders/partition tables. There is no automatic rollback or digital signature. Use trusted downloads; an interrupted flash write can require hardware recovery.
