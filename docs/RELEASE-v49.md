# 4.9.0-rc1 — testrelease / prerelease

## Nederlands

- Smart BatteryProtect 100A (A3B1) via RAK/ESP UIT en AAN getest, met onafhankelijke terugleescontrole.
- Maximaal tien benoemde Bluetooth-functies via +, ook beschikbaar als input- en LoRa-acties.
- BatteryProtect tweede profiel; pairing/profielbewaking en begrensde CBOR-parser.
- Compacte datasheet-stroomraming met mAh/Wh-keuze en relaisvergelijkingen.
- ESP-WiFi-OTA met startup-handshakeherstel; naam met spaties en opgeslagen instellingen behouden.
- Publieke STM32-build zonder privésleutels, NL/EN-handleiding, UG65/TTN-codec en Windows USB-wrapper.

**Download de complete ZIP** voor broncode, handleiding, installer en dependency-notices.
STM32 `.bin` is voor USB, ESP `.packed` voor Beheer → firmware bijwerken.
Geen automatische ESP-rollback; voeding aangesloten houden.

Nog niet klaar: automatische privé/TTN-fallback, fabrieksinstallatie zonder testpinnen
op een nieuwe module, live TTN-test, echte MPPT-test, andere BatteryProtect-varianten,
RAK13007 en een volledig zelfstandige installer-exe. Geen veiligheidskritieke toepassing.
Dit zijn expliciete beperkingen van de testrelease, geen verborgen functies.

## English

Physically tested RAK/ESP Smart BatteryProtect 100A control, ten named Bluetooth
functions, compact mAh/Wh estimates, profile guards, improved OTA startup handshake,
private-key-free STM32 build, bilingual docs, UG65/TTN codec and Windows USB wrapper.

Download the complete ZIP for source, instructions, installer and dependency notices.
Use STM32 `.bin` for USB and ESP `.packed` for browser OTA. Keep power connected;
no automatic ESP rollback. This is not whole-device wireless OTA.

Automatic two-network failover is not implemented. Fresh factory bootstrap, live
TTN, real MPPT, other BatteryProtect variants, RAK13007 and prolonged stress tests
remain pending. No standalone all-in-one installer exe or safety-critical guarantee.

Original code: MIT, © 2026 Roel Broersma. Linked SDKs keep their separate terms;
in particular the RAK BSP is restricted to RAKwireless hardware. See
THIRD-PARTY-NOTICES.md and VALIDATION-v49.md.
