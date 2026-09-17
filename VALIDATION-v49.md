# Validation — 4.9.0-rc1, 17 September 2026

This is a **prerelease**. Results below are evidence from the specific bench setup,
not blanket claims about every RAK/Victron module.

## Installed hardware and firmware

- RAK11162/RAK11160, STM32WLE5 + ESP8684/C2, 26MHz/2MB.
- RAK19010 base, RAK19012 USB power, RAK13001 I/O; unloaded test setup.
- Smart BatteryProtect 12/24V-100A, product ID A3B1; PIN pairing enabled.
- Public STM32 4.9.0 binary, RUI BSP 4.2.4; native ESP 4.9.0-rc1.
- Original ESP bootloader and partition table retained. Never erased or replaced
  as part of this release update. ESP OTA storage 0x2A000/0xA6000,
  application 0xD0000/0x130000; no automatic rollback.
- Existing WiFi name containing spaces, password and LoRa credentials retained.
  The public build falls back to the already provisioned RUI credentials rather
  than requiring a private AppKey compiled into its executable.

## Physical tests passed

1. USB STM32 flash completed through the normal board USB port.
2. Existing format-6 settings migrated to format 7; two original functions retained.
3. Dedicated RAK/ESP BatteryProtect OFF and ON: pairing, product/instance checks,
   mode write only if needed, fresh mode **and** output readback.
4. Independent PC read after RAK OFF: mode 4, output 0, off-reason 4.
   Independent read after RAK ON: mode 3, output 1, off-reason 0. Original ON restored.
5. Native ESP OTA accepted and verified. After startup READY retry fix, WiFi
   returned without manually resetting STM32; final version reported 4.9.0-rc1.
6. Ten functions saved on actual STM32, read back after restart. Identical save
   kept configuration revision unchanged. Manual function 10 ran on the real ESP:
   success, one attempt, BatteryProtect output-state 1. Original two-function
   configuration restored afterward; final configuration revision 9.
7. Public-key-free STM32 update rejoined the existing private UG65 network.
   Joined and local transmission status are not proof of every server payload decode.

No oscilloscope/load test was performed; “output state” is the device's register,
not an external voltage/contact measurement. The MPPT was not present.

## Offline tests passed

- Host C++ tests compile actual config_store.cpp and victron_frames.h: all CBOR
  truncations, malformed trailing frame, wrong instance, no embedded-byte-pattern
  false positives; format-6 migration; A/B CRC persistence; interrupted write
  fallback; unchanged-save suppression; ten full 20-byte GATT functions; bounds;
  profile isolation/mandatory PIN; all-zero JoinEUI accepted with nonzero key/DevEUI.
- Seven independent Python protocol tests: exact BP command encoding, instances,
  split notifications, intermediate state and malformed-input rejection.
- Packed-image tests: known stock format/roundtrip and truncation, extension,
  header/body corruption rejection.
- Codec tests: UG65 and TTN wrappers, BP ON/OFF/transient/failure, function 10,
  MPPT/Generic separation, legacy payload and invalid port/length.
- Isolated browser tests: maximum ten functions and stable routes, profile order,
  conditional I/O fields, automatic Class C notice and deliberate A override,
  unit conversion/arithmetic, Dutch/English, all pages at 390px with no overflow,
  no JavaScript exceptions and no unsolicited save.

UI screenshots in docs/images use simulated, non-private values, not live gateway
screenshots. UG65 configuration is provided as a settings table instead.

## Build sizes and hashes

STM32: 170880 bytes reported program usage / 200704 (85%); globals 44136 /
48640 reported usable data memory (90%). The linker separately reserves heap and
stack; the Arduino warning is still relevant and longer stress testing is needed.
ESP application: 896176 bytes, approximately 28% of its app slot free.

| Artifact | SHA-256 |
|---|---|
| Public STM32 .bin | 9ba8d772518f1dc6f77acb3d17e94ecbb99da1fc234ef89dedb994c861177de4 |
| ESP 4.9.0-rc1 .packed | a976e0007d6fcb314d10461e1f44c10eae97b8ece556e1e5773fb9f6afd7cf2c |

## Corrections found during validation

- NimBLE discovery cancellation does not emit the scan-complete event the driver
  awaited. Explicitly completing the scan handoff after successful cancellation
  fixed the first RAK connection failure (before any write).
- A single READY frame after OTA could be lost amid stock bootloader output,
  leaving STM32's OTA hold active. ESP now repeats READY until the STM handshake.
- Public firmware originally ignored old RUI credentials when a private build
  had seeded them but the portal-origin flag was unset. It now reads the retained
  tuple when no AppKey is compiled. No credentials were erased.

## Not yet validated / not implemented

- Brand-new stock-module bootstrap to this native build without test pads.
- Live TTN join, Class C downlink and TTN fair-use budgeting under real traffic.
- Automatic private/TTN priority, failure detection, failover and daily return:
  **not implemented**, design only.
- End-to-end physical input or gateway downlink driving each of ten BP functions.
  Manual function 10 and offline route/codec tests are not substitutes for that test.
- Physical MPPT actions; other BatteryProtect variants/firmware; RAK13007.
- Protection-active BP scenarios, prolonged radio/HTTP stress, power-failure
  injection during physical ESP OTA and complete-board current measurements.
- Whole-device wireless OTA, signed updates, automatic rollback or a universal
  self-contained Windows .exe installer.

## Reproduce

Use your own toolchain paths. C++ tests need a host C++11 compiler and
`-I tools/tests tools/tests/config-and-frames.cpp`. UI tests need Playwright and
Chrome; set PLAYWRIGHT_PATH if it is not installed locally. Python protocol tests
need cbor2. Build scripts never flash automatically. Owner-only physical helpers
and private test logs are intentionally excluded from the public package.
