# Installeren en bijwerken / Installation and updates

[README](../README.md) · [Downloads](https://github.com/roelbroersma/victron-lorable-remote/releases/latest) · [English](#english)

## Nederlands

### Eerste installatie

Voor een **RAK11162 met RAK-fabrieksfirmware**, op een WisBlock-basisboard met **RAK19012 USB-aansluiting**. Benodigd: Windows 10/11, een USB-datakabel en een ingeschakelde WiFi-adapter met automatische IP-instellingen (DHCP).

1. Download **LoRaBLE-Remote-4.11.1-Windows.zip** en pak alles uit.
2. Koppel geschakelde belastingen los en sluit het board via USB aan. Sluit Serial Monitor als die openstaat.
3. Open **Install.cmd**, kies je taal en COM-poort en bevestig met `INSTALL`.
4. Sta de Windows-beheerdersvraag toe en, indien gevraagd, locatietoegang voor WiFi.
5. Wacht op **Installatie voltooid**. Laat USB aangesloten en het installatievenster open.

De wizard controleert de firmware en regelt tijdelijk een beveiligde WiFi-verbinding met het board. Daarna wordt je eerdere WiFi-verbinding hersteld. Internet via WiFi kan dus even wegvallen. De benodigde RAK-uploader wordt zo nodig vooraf gedownload; Arduino IDE, Python en losse programmeerpinnen zijn niet nodig.

### Open de instellingen

| | Standaardwaarde |
|---|---|
| WiFi-naam | **Victron LoRaBLE Remote** |
| WiFi-wachtwoord | **CHANGE-ME-FIRST** |
| Webadres | **http://192.168.4.1/** |

Wijzig het WiFi-wachtwoord meteen. WiFi blijft standaard één uur na opstart beschikbaar.

Kies je Bluetooth-apparaat en eventuele I/O-module, maak functies aan en vul de [LoRaWAN-gegevens](NETWORKS.md) in. Klik **Opslaan** om wijzigingen toe te passen — ook na het verplaatsen van netwerkprofielen. Controleer de bediening eerst zonder aangesloten belasting.

### Firmware bijwerken

Voor een bestaande installatie met de complete updater (**LoRaBLE 4.11 of nieuwer**). Instellingen blijven behouden. Download vooraf een back-up via **Beheer**.

**Via WiFi**

1. Download **LoRaBLE-Remote-4.11.1.bin** uit de [laatste release](https://github.com/roelbroersma/victron-lorable-remote/releases/latest).
2. Verbind met de WiFi van het board en open **Beheer → Firmware bijwerken**.
3. Kies **Bladeren… → het .bin-bestand → Updaten** en bevestig.
4. Laat de voeding aangesloten tot het board opnieuw is opgestart. Verbind daarna opnieuw.

**Via USB**

Pak de Windows-ZIP uit, sluit het board via USB aan en open **Install.cmd**. De wizard werkt een bestaande complete installatie volledig via USB bij; je pc hoeft dan niet van WiFi te wisselen.

Beide routes gebruiken **hetzelfde complete .bin-bestand**. Gebruik de meegeleverde installer of de webinterface, geen generieke chipflasher.

### Back-up, herstellen en herstarten

| Onder Beheer | Bediening |
|---|---|
| Back-up | **Back-up downloaden** |
| Herstellen | **Bladeren… → .json kiezen → Herstellen** |
| Opnieuw opstarten | **Herstarten** en bevestigen |

Een back-up bevat opgeslagen instellingen, maar **geen AppKeys, Bluetooth-PIN, WiFi-wachtwoord of DevEUI**. Bewaar die apart. Herstellen vervangt formulierwijzigingen en bewaart de bestaande sleutels op het board; controleer of die bij de geïmporteerde netwerkprofielen horen.

Herstarten is geen fabrieksreset: instellingen blijven staan, WiFi wordt onderbroken, recente gebeurtenissen worden gewist en het relais valt af.

### Hulp nodig?

- **Geen COM-poort?** Gebruik een USB-*datakabel*, controleer de USB-serieel-driver in Apparaatbeheer en sluit andere programma's die de poort gebruiken.
- **Installatie gestopt?** Laat de voeding aangesloten en bewaar de foutmelding. Wis niets; volg de herstelmelding van de wizard. Voor hulp: [maak een issue aan](https://github.com/roelbroersma/victron-lorable-remote/issues), zonder sleutels of wachtwoorden.
- **Downloads controleren?** De release bevat **SHA256SUMS**. Download firmware alleen uit een vertrouwde bron en onderbreek de voeding niet tijdens installatie.

Broncode, handmatig flashen en technische installatievoorwaarden staan bij [Bouwen](DEVELOPMENT.md).

## English

### First installation

For a **RAK11162 with RAK factory firmware**, on a WisBlock baseboard with a **RAK19012 USB connection**. You need Windows 10/11, a USB data cable and enabled WiFi with automatic IP settings (DHCP).

1. Download **LoRaBLE-Remote-4.11.1-Windows.zip** and extract everything.
2. Disconnect switched loads, connect the board by USB and close Serial Monitor.
3. Open **Install.cmd**, choose your language and COM port, then type `INSTALL`.
4. Allow Windows administrator access and WiFi location access if requested.
5. Wait for **Installation complete**. Keep USB connected and the installer open.

The wizard checks firmware and temporarily connects your PC to the board's protected WiFi network, then restores your previous connection. WiFi internet may briefly disconnect. The RAK uploader is downloaded beforehand if needed; Arduino IDE, Python and separate programming pins are not required.

### Open settings

Connect to WiFi **Victron LoRaBLE Remote**, password **CHANGE-ME-FIRST**, then open **http://192.168.4.1/**. Change the password immediately. WiFi defaults to one hour after startup.

Select your Bluetooth device and optional I/O module, add functions and enter [LoRaWAN credentials](NETWORKS.md#english). Click **Save** to apply changes, including network priority. Check operation with loads disconnected first.

### Update firmware

For an existing installation with the complete updater (**LoRaBLE 4.11 or later**). Settings are retained. Download a backup from **Manage** first.

**WiFi:** download **LoRaBLE-Remote-4.11.1.bin**, connect to the board and open **Manage → Update firmware → Browse… → select the file → Update**. Confirm, maintain power through restart and reconnect.

**USB:** extract the Windows ZIP, connect the board and open **Install.cmd**. Updates use USB only; your PC stays on its current WiFi network.

Both routes use **the same complete .bin file**. Use the supplied installer or web interface, not a generic chip flasher.

### Backup, restore and restart

Under **Manage**, use **Download backup**, **Browse… → Restore**, or **Restart**.

Backups contain saved settings, but **no AppKeys, Bluetooth PIN, WiFi password or DevEUI**. Keep these separately. Restore replaces unsaved form changes and retains existing device keys; ensure they match the imported network profiles. Restart retains settings, disconnects WiFi, clears recent events and releases the relay; it is not a factory reset.

### Need help?

- **No COM port?** Check the USB *data* cable and serial driver, and close other programs using the port.
- **Installation stopped?** Keep power connected, retain the error and erase nothing. Follow the installer's recovery message or [open an issue](https://github.com/roelbroersma/victron-lorable-remote/issues) without secrets.
- **Check a download:** use the release's **SHA256SUMS**. Install trusted firmware only and maintain power throughout installation.

See [Building](DEVELOPMENT.md#english) for source, command-line installation and technical requirements.
