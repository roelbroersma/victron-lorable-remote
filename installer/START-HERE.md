# Victron LoRaBLE Remote 4.11.0

## Nederlands

1. Pak deze ZIP volledig uit.
2. Koppel geschakelde belastingen los en sluit het RAK11162-board aan via de normale USB-data-aansluiting van het basisboard met RAK19012.
3. Dubbelklik **Install.cmd**. Kies NL of EN en de COM-poort.
4. Bevestig met `INSTALL` en sta de Windows-beheerdersvraag toe als die verschijnt.
5. Laat voeding en venster staan tot **Installatie voltooid**.

Bij een fabrieksboard regelt de wizard tijdelijk een beveiligde WiFi-verbinding. Je hebt Windows 10/11, PowerShell 5.1, WiFi met DHCP en een USB-datakabel nodig. De officiële RAK-uploader wordt zo nodig vooraf gedownload. Arduino IDE en Python zijn niet nodig.

De fabrieksroute vereist de oorspronkelijke RAK-indeling met ESP-AT 3.3.0.0 / ESP32C2-2MB / SDK 5.0.6. Een bestaande complete updater (LoRaBLE 4.11+) gebruikt alleen USB. Voor LoRaBLE 4.10 en ouder is afzonderlijke migratie nodig: gebruik deze wizard niet als directe upgrade.

Na installatie: WiFi **Victron LoRaBLE Remote**, wachtwoord **CHANGE-ME-FIRST**, adres **http://192.168.4.1/**. Wijzig het wachtwoord meteen. Volgende updates gebruiken `firmware/LoRaBLE-Remote-4.11.0.bin` via Beheer of de USB-installer. Instellingen blijven behouden.

Bij een fout: voeding aangesloten laten, melding bewaren, niets wissen. Gebruik geen generieke flasher, erase-all of vervangende bootloader. Er is geen automatische rollback.

[Volledige handleiding](https://github.com/roelbroersma/victron-lorable-remote/blob/main/docs/INSTALL.md) · [LoRaWAN instellen](https://github.com/roelbroersma/victron-lorable-remote/blob/main/docs/NETWORKS.md)

## English

Extract the entire ZIP, disconnect switched loads, connect normal board USB through the RAK19012, then open **Install.cmd**. Select language and COM port, type `INSTALL`, allow Windows elevation when requested and keep power connected until **Installation complete**.

Factory installation uses Windows 10/11, PowerShell 5.1, WiFi with DHCP and a USB data cable. The wizard handles a temporary protected WiFi connection and downloads the official RAK uploader beforehand if needed. Arduino IDE and Python are not required.

Supported factory layout: original RAK ESP-AT 3.3.0.0 / ESP32C2-2MB / SDK 5.0.6. Boards with the complete updater (LoRaBLE 4.11+) use USB only. LoRaBLE 4.10 and earlier need separate migration; do not use this wizard as a direct upgrade.

Connect to **Victron LoRaBLE Remote**, password **CHANGE-ME-FIRST**, open **http://192.168.4.1/** and change the password immediately. Later updates use the same complete file in the firmware folder through Manage or USB. Settings are retained.

On errors, keep power connected, retain the message and erase nothing. Do not use generic flashers, erase-all or replacement bootloaders. There is no automatic rollback.

[Installation guide](https://github.com/roelbroersma/victron-lorable-remote/blob/main/docs/INSTALL.md) · [Project and source](https://github.com/roelbroersma/victron-lorable-remote)

© 2026 Roel Broersma.
