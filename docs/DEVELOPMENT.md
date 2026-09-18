# Bouwen / Building

[README](../README.md)

## Nederlands

De broncode is georganiseerd in `stm32` (Arduino/RUI), `esp8684` (WiFi/Bluetooth), `web` (interface), `updater` (interne updater) en `installer` (Windows). Voor eindgebruikers is er één compleet firmwarebestand; losse buildonderdelen zijn geen afzonderlijke installatiestappen.

### Benodigd

- Arduino CLI of Arduino IDE 2 met **RAK RUI STM32 BSP 4.2.4**.
- **ESP-IDF 5.5.5**, doel **ESP32-C2**, **26 MHz**, **2 MB flash**.
- Node.js en Python 3 voor assets en verpakking.
- Windows PowerShell voor de meegeleverde bouwscripts.

Arduino: open de volledige map `stm32` via `stm32.ino`, kies **RAK11160**, **Support LoRaWAN**, LA915 uit. De extra compileropties staan in `arduino/platform.local.txt`. Maak een back-up en plaats deze naast `platform.txt` van uitsluitend BSP 4.2.4; de override geldt ook voor andere sketches met dat BSP. Het bouwscript stelt deze opties zelf in.

### Firmware maken

```powershell
.\tools\build.ps1 -Public
.\esp8684\build.ps1 -BuildDirectory build_release411
.\tools\build-first-install.ps1
```

`-Public` sluit `stm32/settings.local.h` uit. Gebruik voor privé-installaties het voorbeeldbestand; publiceer nooit ingevulde lokale instellingen. Het webasset en de interne RAM-updater worden tijdens de publieke build gegenereerd.

Maak een nieuwe uitvoermap en combineer de onderdelen:

```powershell
python tools/pack-esp-ota.py esp8684/build_release411/lorable_esp8684.bin --version LoRaBLE-C2-26M-v4.11 --output esp8684/build_release411/LoRaBLE-ESP8684-4.11.0.packed
New-Item -ItemType Directory -Path dist/release-4.11.0
python tools/update-bundle.py --stm build_public411/stm32.ino.bin --esp esp8684/build_release411/LoRaBLE-ESP8684-4.11.0.packed --version 4.11.0 --output dist/release-4.11.0/LoRaBLE-Remote-4.11.0.bin
```

Bestaande uitvoerbestanden worden niet overschreven. De `.packed` is uitsluitend een bouwonderdeel. **Flash nooit** de door ESP-IDF gegenereerde bootloader, partitietabel of merged image; de oorspronkelijke RAK-indeling blijft behouden.

### Publiceren

1. Gebruik `node tools/prepare-release.mjs <nieuwe-stagingmap>`. De allowlist neemt alleen projectbestanden en licenties mee en controleert lokale privégegevens.
2. Controleer de map met `python tools/check-release.py --root <stagingmap>`. Deze controle omvat firmware, installer, documenten en verwijzingen.
3. Bouw de Windows-ZIP met `python tools/make-release-zip.py --root <stagingmap> --output <nieuwe-uitvoermap>/LoRaBLE-Remote-4.11.0-Windows.zip`.
4. Commit uitsluitend de opgeschoonde map. Laat de GitHub-controles afronden en maak daarna de versie-tag.
5. De releaseworkflow maakt een concept met `.bin`, Windows-ZIP en verse downloadchecksums. Controleer de assets en publiceer het concept.

`firmware/manifest.json` beschrijft het complete image en de ingebedde installatiehulp. De release bevat één `SHA256SUMS` voor de downloads; de Windows-ZIP heeft daarnaast interne bestandschecksums. SDK-licenties blijven in `third_party_notices`.

## English

Source folders: `stm32` (Arduino/RUI), `esp8684` (WiFi/Bluetooth), `web` (portal), `updater` (internal updater) and `installer` (Windows). End users install one complete image; component builds are not separate customer installation steps.

Use **RAK RUI STM32 BSP 4.2.4**, **ESP-IDF 5.5.5**, **ESP32-C2 / 26 MHz / 2 MB**, Node.js, Python 3 and Windows PowerShell. Arduino uses the whole `stm32` folder, RAK11160, LoRaWAN enabled and LA915 disabled. The build script applies required compiler options. For Arduino IDE, back up the BSP configuration before adding `arduino/platform.local.txt` beside that BSP's `platform.txt`.

Run the commands above to build public firmware and the setup helper, pack the connectivity application and combine it with the control application. Public builds exclude local credentials. Use new output names/directories; never distribute secrets. Never flash generated ESP-IDF bootloaders, partition tables or merged images into the retained RAK layout.

Prepare a new allowlisted staging directory, run `check-release.py`, then build the Windows ZIP with `make-release-zip.py`. Commit the reviewed source, let GitHub checks finish and create the matching version tag. The workflow creates a draft release with the complete `.bin`, Windows ZIP and fresh asset checksums. Review and publish the draft.

Original source is MIT; linked SDKs and runtime components retain their own licenses, including RAK's RAK-hardware restriction. Preserve the supplied notices when redistributing binaries.
