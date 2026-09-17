# Smart BatteryProtect Bluetooth control — 2026-09-17

## Result

**ON/OFF control is now verified on the owner's actual Smart BatteryProtect 12/24V-100A (product ID 0xA3B1), using this Windows PC's Intel Bluetooth adapter.** At approximately 12:01 local time on 2026-09-17, one guarded OFF → ON cycle succeeded. Fresh reads confirmed mode, actual output-state register and off-reason. A separate reconnect at 12:03 confirmed the original ON state remained restored. This supersedes the earlier conclusion that a verified command was still missing.

**Update: RAK/ESP control is now physically verified in 4.9.** A dedicated driver paired, verified product A3B1/instance 0, switched OFF and back ON, and checked fresh mode plus output-state reads. Independent PC reconnects verified OFF at 11:29 UTC and restored ON at 11:34 UTC on 2026-09-17. The device was left ON. This supersedes the 4.8.2 discovery-only restriction.

## Live evidence

Raw logs remain private under `artifacts_private/`; no PIN, bonding keys or extracted app resources belong in a public repository.

- `ble-inventory-20260917-094408-142676.json`: complete GATT inventory, successful connection without pairing.
- `ble-inventory-20260917-094816-257228.json`: unpaired control read and channel-0 subscriptions rejected with ATT Insufficient Authentication (0x05).
- `ble-inventory-20260917-094852-459890.json`: Windows ProvidePin pairing succeeded with the user's supplied PIN; protected control read and notifications then succeeded. The Windows bond remains stored. WinRT reported protection level NONE in this result, so no claim about the exact negotiated security level is made solely from that field.
- `ble-inventory-20260917-095141-124118.json`: actual device/serial/status reads and live updates; instance 0; GetPathList rejected with control reply `f7 03 00`.
- `batteryprotect-control-20260917-095926-866567.json`: useful negative test—GetValue before Subscribe got no value reply; no setting writes occurred.
- `batteryprotect-control-20260917-100058-497548.json`: successful guarded switching cycle, no parser errors, original mode/output restored, disconnected cleanly.
- `batteryprotect-control-20260917-100309-444477.json`: independent reconnect/read confirms ON afterward.

| Readback | Before | After OFF | After restoring ON |
|---|---:|---:|---:|
| Device mode (`0x0200`) | 3 | 4 | 3 |
| Device state (`0x0201`) | 249 | 0 | 249 |
| Actual output state (`0xEDA8`) | 1 | 0 | 1 |
| Off reason (`0x0207`) | 0 | 4 (software off) | 0 |
| Input voltage (`0xED8D`, /100 V) | 12.15 V | 12.15 V | 12.15 V |

Output state briefly reported **3** during re-enable before reaching 1. Do not treat any nonzero value as ON. No oscilloscope, load or external voltmeter was used. The unloaded output-voltage readout remained near input voltage even while output-state was OFF; voltage alone is therefore not proof of switch state in this setup.

## Verified GATT route and messages

The device exposes standard Generic Access/Generic Attribute services, base Victron service `97580001-ddf1-48be-b73e-182664615d8e`, and both `306b...dfd0` / `306b...dfd1` services. The working application route is **dfd0**:

| UUID | Role verified in this session |
|---|---|
| `306b0001-b081-4037-83dc-e59fcc3cdfd0` | Main service |
| `306b0002-b081-4037-83dc-e59fcc3cdfd0` | Control read, chunk negotiation and receive credits |
| `306b0003-b081-4037-83dc-e59fcc3cdfd0` | Requests and final notification fragment (LastData) |
| `306b0004-b081-4037-83dc-e59fcc3cdfd0` | Non-final notification fragments (Data) |

Tested sequence, after pairing:

1. Subscribe to all three dfd0 characteristics. Read Control (observed `00 01 00 01 50 14 00`).
2. Control write-without-response `fa 80 ff`, followed by `f9 80`.
3. LastData `01` (GetDevices) returns `02 9f 00 00 ff`: only instance **0**, not MPPT instance 3.
4. LastData `03 00` subscribes to instance 0; response `07 00 03 00`. This is needed before register GETs on the tested device.
5. Query identity and initial state before allowing a switch. VReg GET is concatenated CBOR `5, 0, [register]`, e.g. mode read `05 00 81 19 02 00`.
6. OFF: LastData **`06 00 82 19 02 00 41 04`**.
7. ON: LastData **`06 00 82 19 02 00 41 03`**.
8. Verify mode AND actual output via fresh GETs; inspect off-reason if output does not follow. Do not override BMS/protection thresholds.
9. Close the connection. For sustained sessions, service receive credits; the diagnostic sends `f9 41` after a bounded number of received fragments.

The two mode writes encode CBOR `6, 0, [0x0200, byte-string(value)]`. No separate write ACK was observed in the cycle, so write submission must not be treated as success. Fresh mode and output readbacks provided confirmation.

Data notification fragments must be joined until LastData before CBOR decoding. Value records are `8, instance, register, byte-string`. The test saw both single-frame and split responses. This is not a simple raw ON/OFF byte written to an arbitrary characteristic.

No application writes were sent to the base 9758 service, no PIN changes/DFU were attempted, and no battery/BMS thresholds were changed. The transport keepalive register `0x0093` was not written; longer-lived behavior remains a separate test.

## Official-app cross-check (before switching)

The official [Victron software download page](https://www.victronenergy.com/support-and-downloads/software) linked [VictronConnect 6.42 APK](https://updates.victronenergy.com/feeds/VictronConnect/android/VictronConnect-v6.42.apk). It was downloaded for **offline resource inspection only**, not installed or executed.

- APK SHA-256: `A5072B211260B616E77C89330D0FBED1F5A0193619808A05C3D754FCF20A107B`.
- Private extracted resource at ELF zlib offset `0x11e0813`: BatteryProtect settings switch uses enabled value 3 and disabled value 4 on `items.settings.mode`.
- Resource `0x10cee88`: that BatteryProtect property maps to `/Mode`, separately from `/Settings/SbpMode` (normal versus Li-ion mode).
- Resource `0x4fd0eb3`: `/Mode` maps to `VE_REG_DEVICE_MODE`; `/Load/State` maps to `VE_REG_DC_OUTPUT_STATUS`.
- Resource `0x4fabc48`: numeric definitions identify `0x0200` (u8 mode), `0xEDA8` (read-only output state), `0xED8D` (signed voltage /100), and `0x0207` (off-reason).

These mappings established the command's purpose before the live write. Proprietary resource contents remain in ignored `research/`, not in published firmware sources.

## Sources inspected

- [Victron Community: Control of Victron device over BLE in custom application](https://community.victronenergy.com/t/control-of-victron-device-over-ble-in-custom-application/38008): the original author explicitly wants to switch a Smart BatteryProtect from their own application. The visible topic has the request and a follow-up, but no working command or characteristic. It demonstrates the same unresolved question, not a protocol specification.
- [victron-ble BatteryProtect implementation](https://github.com/keshavdv/victron-ble/blob/main/victron_ble/devices/smart_battery_protect.py): a parser for **received, decrypted Instant Readout advertisements**. Output-state enum values describe telemetry; they must not be treated as write opcodes or GATT payloads.
- [victron-ble project](https://github.com/keshavdv/victron-ble): Instant Readout advertisement parsing. Not a BatteryProtect control implementation.
- [birdie1/victron](https://github.com/birdie1/victron): lists tested SmartShunt, SmartSolar and Orion devices. The documented support list does not establish BatteryProtect ON/OFF support.
- [vvvrrooomm/victron](https://github.com/vvvrrooomm/victron): original transport/protocol research. Useful for the common 306b service, not by itself proof of BatteryProtect switching.
- [patlux/ve-smart-telemetry protocol notes](https://github.com/patlux/ve-smart-telemetry/blob/main/analysis/victronconnect-protocol-reference.md): CBOR request/response structure, GetDevices/Subscribe/GetValues/SetValues and Data/LastData assembly. Its original APK provenance is explicitly qualified by the author. Relevant behavior was independently cross-checked with the official 6.42 resources and this real device.
- [Victron product page](https://www.victronenergy.com/battery_protect/smart-battery-protect) and [VictronConnect release notes](https://www.victronenergy.com/live/victronconnect%3Abeta): Bluetooth/VictronConnect support is documented, but these are not a third-party write-protocol specification.
- [Pekaway discussion](https://forum.pekaway.de/t/victron-gerate-verbindung-via-bluetooth/2138?page=3): users describe received BatteryProtect status and parser enum problems. This discussion is a research lead only; the implementation above is the primary source for the parser behavior.

Searches included BatteryProtect with GATT, write_gatt_char, BLE control, command, register and protocol, including GitHub and Victron's current/archived communities. Absence from these searches is not proof that no implementation exists elsewhere.

## Driver and remaining validation

The ESP driver now implements this dedicated instance-0 route, mandatory SMP,
Data/LastData reassembly, receive credits, product identity checks, bounded
timeouts, fresh mode/output readback and WiFi recovery. STM/UI use distinct
BatteryProtect function kinds 11/12, never the MPPT write routine. The scan
cancel path explicitly signals scan completion; waiting for a cancelled NimBLE
scan's nonexistent completion event caused the first failed RAK attempt.

OFF and ON succeeded on the RAK, not just the PC. Longer soak tests,
protection-active scenarios, physical input/downlink-to-Bluetooth end-to-end
tests, and other BatteryProtect variants/firmware remain separate acceptance
tests. The owner's SmartSolar MPPT remains elsewhere.

## Reproducible local diagnostics

- `tools/probe_ble_readonly.py`: targeted inventory, optional PIN pairing, GATT reads, and opt-in protocol discovery/GETs. No settings/LOAD writes. Set `LORABLE_TEST_PIN` only when pairing; the script does not log it.
- `tools/probe_batteryprotect_control.py`: requires `--mac` and `--serial`; verifies exact device identity and tested product ID. Default reads only. Explicit `--switch-cycle` performs OFF then restores original ON, and refuses to start if the initial output is not ON or an off-reason is present. It attempts restoration in `finally` and reports any recovery failure.
- `tools/test-batteryprotect-protocol.py`: seven offline tests passed, including exact ON/OFF encoding, instance parsing, split frames, intermediate output-state preservation and malformed-frame rejection.
- Isolated ignored environment `research/ble-venv`: Python 3.12, Bleak 3.0.2, cbor2 5.7.1. Offline APK inspection additionally used pyelftools 0.32. No project/global runtime was replaced.

## Related hardware/LoRa checks

- [RAK13001 datasheet](https://docs.rakwireless.com/product-categories/wisblock/rak13001/datasheet/): default wet-contact input is rated **12–24 V DC**. 5 V behavior and an exact switching threshold are not specified. UI edges refer to external voltage appearing/disappearing; the optocoupler inverts this internally (active LOW). The firmware requires 80 ms stability and does not synthesize an edge at boot. RAK's page inconsistently names SW1 and IO3 routing; the firmware still expects IO3 and routing must be checked on the physical module.
- [LoRaWAN device classes](https://www.thethingsnetwork.org/docs/lorawan/classes/): Class A can receive in the windows following its uplink. Class C keeps its receiver available almost continuously except during transmission. Downlink latency/delivery still depends on the server, RF conditions and restrictions. Class C must be enabled consistently on node and network server; no UG65 setting was changed here.
