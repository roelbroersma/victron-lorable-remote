# 4.12.1 — Smart BatteryProtect 48V support

## Nederlands

Bedien nu ook de **Victron Smart BatteryProtect 48V-100A (A3B3)** via Bluetooth, LoRaWAN of een lokale ingang.

- **Twee duidelijke BatteryProtect-keuzes:** 12/24V-100A (A3B1) en 48V-100A (A3B3), elk met AAN/UIT-functies.
- **Controle vóór schakelen:** de productcode moet bij het gekozen profiel passen. Na een opdracht worden modus en echte uitgangsstatus teruggelezen; beveiligingsdrempels en BMS-instellingen blijven ongemoeid.
- **Volledig geïntegreerd:** maximaal tien functies, LoRaWAN- en ingangsacties, opslaan en back-up/herstel ondersteunen beide varianten. Smart MPPT en Generic Bluetooth blijven beschikbaar.
- **Heldere NL/EN-interface:** alleen de bij het gekozen apparaat passende acties worden getoond.
- **Payloadcodec:** bijgewerkt voor A3B3-statusmeldingen. Neem de nieuwe `stm32/lorawan-payload-codec.js` over in je Milesight-gateway of TTN-uplinkformatter.

Met dank aan [mraygalaxy](https://github.com/mraygalaxy) voor [PR #1](https://github.com/roelbroersma/victron-lorable-remote/pull/1), met protocolgegevens en fysieke uitgangswaarnemingen van de A3B3 met firmware 2.11.

### Download en bijwerken

- **LoRaBLE-Remote-4.12.1.bin** — één complete update via **Beheer → Firmware bijwerken → Bladeren → Update**.
- **LoRaBLE-Remote-4.12.1-Windows.zip** — eerste installatie of USB-update; uitpakken en **Install.cmd** openen. Bevat hetzelfde complete firmwarebestand.
- **SHA256SUMS** — controlesommen van beide downloads.

Instellingen blijven behouden. Gebruik je de 48V-variant, kies dan na bijwerken **Bluetooth → Smart BatteryProtect (48V-100A, A3B3)**, stel AAN/UIT-functies in en klik **Opslaan**. Voor de bestaande 12/24V-variant hoef je het profiel niet te wijzigen. Houd de voeding aangesloten tijdens bijwerken.

[Installatie](https://github.com/roelbroersma/victron-lorable-remote/blob/v4.12.1/docs/INSTALL.md) · [Handleiding](https://github.com/roelbroersma/victron-lorable-remote/blob/v4.12.1/docs/MANUAL.md) · [LoRaWAN](https://github.com/roelbroersma/victron-lorable-remote/blob/v4.12.1/docs/NETWORKS.md)

## English

Control the **Victron Smart BatteryProtect 48V-100A (A3B3)** through Bluetooth, LoRaWAN or a local input.

- **Two clear BatteryProtect choices:** 12/24V-100A (A3B1) and 48V-100A (A3B3), each with ON/OFF functions.
- **Check before switching:** the product ID must match the selected profile. Commands read back both mode and actual output state; protection thresholds and BMS settings are left unchanged.
- **Complete integration:** both variants support up to ten functions, LoRaWAN and input actions, saved settings, and backup/restore. Smart MPPT and Generic Bluetooth remain available.
- **Clear Dutch/English interface:** function choices match the selected device.
- **Payload codec:** updated for A3B3 status messages. Copy the new `stm32/lorawan-payload-codec.js` into your Milesight gateway or TTN uplink formatter.

Thanks to [mraygalaxy](https://github.com/mraygalaxy) for [PR #1](https://github.com/roelbroersma/victron-lorable-remote/pull/1), including protocol evidence and physical-output observations for the A3B3 running firmware 2.11.

### Download and update

- **LoRaBLE-Remote-4.12.1.bin** — one complete update through **Manage → Update firmware → Browse → Update**.
- **LoRaBLE-Remote-4.12.1-Windows.zip** — first installation or USB update; extract and open **Install.cmd**. Contains the same complete firmware file.
- **SHA256SUMS** — checksums for both downloads.

Settings are retained. For the 48V model, select **Bluetooth → Smart BatteryProtect (48V-100A, A3B3)** after updating, configure ON/OFF functions and click **Save**. Existing 12/24V installations do not need a profile change. Maintain power throughout the update.

[Installation](https://github.com/roelbroersma/victron-lorable-remote/blob/v4.12.1/docs/INSTALL.md#english) · [Manual](https://github.com/roelbroersma/victron-lorable-remote/blob/v4.12.1/docs/MANUAL.md#english) · [LoRaWAN](https://github.com/roelbroersma/victron-lorable-remote/blob/v4.12.1/docs/NETWORKS.md#english)

© 2026 Roel Broersma. MIT project code; dependency licenses included.
