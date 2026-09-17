# 4.10.0 — netwerkprioriteit / network priority

## Nederlands

Vier LoRaWAN OTAA-profielen met eigen JoinEUI/AppKey, naam en aan/uit. Verplaats
de prioriteit met ↑/↓ of slepen. Stel per reserve de preempt-tijd in (standaard
24 uur): ook bij mislukte joins blijft het board op die reserve tot de tijd om is.
Daarna begint het weer bij de hoogste prioriteit. Voor drie/vier profielen is
de volgorde zo begrensd dat een onbereikbare reserve lagere profielen niet blokkeert.

- Bestaande aanmeldgegevens worden automatisch profiel 1; Bluetooth/I/O blijven behouden.
- Bereikbaarheidscontrole met echte ACKs, geen verwarring tussen TX_DONE en ontvangst.
- TTN-type: minimaal vier uur tussen status/controles en een begrensd joinbudget.
- Status met profiel, aanmeldfase, herhaaltijd en preempt-aftelling; gebeurtenissen alleen in RAM.
- Sleutels blijven aan hun profielslot gekoppeld bij verplaatsen en blijven buiten exports/logs.
- Afzonderlijke beveiligingstellers, A/B-opslag en vooraf gereserveerde DevNonces.
- Alle bestaande tien Bluetooth-functies, BatteryProtect/GATT, I/O en mAh/Wh-weergave blijven beschikbaar.

**Beide processors bijwerken:** STM32 `LoRaBLE-STM32-4.10.0.bin` via USB en
ESP `LoRaBLE-ESP8684-4.10.0.packed` via Beheer. Download de volledige ZIP voor
installer, broncode, NL/EN-uitleg en SDK-licenties. De LoRa-payloadcodec blijft schema 4.

Dit is een reguliere release voor de beschreven RAK11162-opstelling met bestaande
native ESP-installatie, geen universele factory-installer. TTN end-to-end op de
testinstallatie, een echte MPPT, andere BatteryProtect-varianten en RAK13007 hebben
nog eigen acceptatietests nodig. Installatie op een nieuwe fabrieksmodule zonder
testpinnen blijft experimenteel. ESP-OTA heeft geen automatische rollback; STM32
heeft USB nodig. Zie [validatie](../VALIDATION-v410.md) en [netwerkgedrag](NETWORKS.md).

## English

Four named OTAA network profiles with independent keys, enable switches, drag or
↑/↓ priority and per-backup preemption time. An unreachable backup keeps retrying
until its residence expires, then higher priorities are attempted again. Existing
credentials migrate to profile 1. Status exposes the active profile and timers.

ACK-based health checks, conservative TTN join/status intervals, persistent
security-counter protection, RAM-only logs and stable key slots are included.
Existing Bluetooth/I/O features and codec schema 4 are retained. Update both the
STM32 USB `.bin` and ESP browser `.packed`; download the complete ZIP.

Regular release scope: the documented RAK11162/native installation. Fresh factory
bootstrap without pads, live TTN acceptance, real MPPT, other BatteryProtect models
and RAK13007 remain separately unvalidated. Not a safety-critical controller, no
whole-device wireless OTA, no automatic ESP rollback and no all-in-one installer exe.

Original code: MIT, © 2026 Roel Broersma. SDK components retain their own terms.
