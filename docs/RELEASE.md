# 4.11.0 — One file. USB or WiFi.

## Nederlands

**Eén compleet firmwarebestand, een Windows-installatiewizard en bediening via je eigen LoRaWAN-netwerk of TTN.**

- Eerste installatie met **Install.cmd**: USB aansluiten, taal en COM-poort kiezen en de wizard volgen. De tijdelijke WiFi-verbinding wordt automatisch geregeld.
- Volgende updates met hetzelfde complete **.bin** via USB of **Beheer → Firmware bijwerken**. Instellingen blijven behouden.
- Vier LoRaWAN-netwerkprofielen met eigen sleutels, prioriteitsvolgorde, fallback en instelbare preempt-tijd.
- Victron Smart MPPT, Smart BatteryProtect A3B1 en Generic Bluetooth; maximaal tien benoemde functies.
- Lokale inputacties en relaisbediening, afzonderlijke LoRa-ontvangstrechten en een gedeelde Milesight/TTN-payloadcodec.
- Nederlands/Engelse webinterface met veldhulp, status, recente gebeurtenissen, handbediening en energie-inschatting.

**Download:** kies **LoRaBLE-Remote-4.11.0-Windows.zip** voor installatie via Windows, of **LoRaBLE-Remote-4.11.0.bin** voor een bestaande complete updater. **SHA256SUMS** bevat de controlesommen van beide downloads. De broncode staat in deze repository en in GitHubs Source code-downloads.

Hardware: RAK11162 met RAK11160-module. De fabriekswizard ondersteunt de oorspronkelijke RAK-indeling met ESP-AT 3.3.0.0 / ESP32C2-2MB / SDK 5.0.6. LoRaBLE 4.10 en ouder vereisen afzonderlijke migratie; het complete bestand is geen directe upgrade voor die versies. Zie [installatie](https://github.com/roelbroersma/victron-lorable-remote/blob/main/docs/INSTALL.md).

Gebruik bijvoorbeeld een **Milesight UG63 / UG65** voor je eigen netwerkserver, of **The Things Network**. Deze gateways zijn voorbeelden; registreer het apparaat op iedere gewenste netwerkserver. Zie [netwerkinstellingen](https://github.com/roelbroersma/victron-lorable-remote/blob/main/docs/NETWORKS.md).

Houd de voeding aangesloten tijdens installatie. Gebruik uitsluitend het meegeleverde installatiepad, geen generieke chipflasher of erase-all.

## English

**One complete firmware file, a Windows installation wizard, and remote control through your own LoRaWAN network or TTN.**

- First installation with **Install.cmd**, including automatic temporary WiFi connection.
- Subsequent updates use the same complete **.bin** through USB or **Manage → Firmware update**, retaining settings.
- Four independent OTAA profiles with priority, fallback and configurable preemption.
- Victron Smart MPPT, Smart BatteryProtect A3B1 and Generic Bluetooth profiles, with up to ten named functions.
- Input-edge actions, relay control, individual downlink permissions and a shared Milesight/TTN payload codec.
- English/Dutch portal with field help, status, recent events, manual controls and energy estimates.

Download **LoRaBLE-Remote-4.11.0-Windows.zip** for Windows installation or **LoRaBLE-Remote-4.11.0.bin** for an existing complete updater. **SHA256SUMS** covers both downloads. Source is available in the repository and GitHub's Source code archives.

For RAK11162 with the RAK11160 module. Factory installation requires the original RAK layout and ESP-AT 3.3.0.0 / ESP32C2-2MB / SDK 5.0.6. LoRaBLE 4.10 and earlier require separate migration, not a direct complete-file update. Read [installation](https://github.com/roelbroersma/victron-lorable-remote/blob/main/docs/INSTALL.md) before proceeding.

Use a private LoRaWAN server, for example a **Milesight UG63 / UG65**, or **The Things Network**. These are examples, not gateway requirements. Maintain power throughout installation; do not use a generic chip flasher or erase-all.

© 2026 Roel Broersma. Original project code is MIT; dependencies retain their own licenses.
