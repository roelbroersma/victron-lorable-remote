# Third-party software

The MIT license covers this project's original source, not third-party SDKs,
firmware tools, trademarks or extracted research material.

## STM32 binary

Built with RAK RUI3 STM32 BSP 4.2.4 and its bundled LoRaMAC, STM32CubeWL,
CMSIS, newlib and GCC runtime components. RAK's root license permits source and
binary redistribution subject to its notice/disclaimer conditions **and use only
with a RAKwireless product**. Do not describe the complete linked binary as
unrestricted MIT software. Corresponding notices are collected in
`third_party_notices/RAK-RUI-4.2.4/` when preparing a release.

## ESP binary

Built with ESP-IDF 5.5.5, including NimBLE, FreeRTOS, lwIP, mbedTLS and Espressif
radio libraries. ESP-IDF's root license is Apache-2.0, with component-specific
licenses/exceptions. Release preparation copies available SDK/component license
and notice files into `third_party_notices/ESP-IDF-5.5.5/`. Compiler runtime
exceptions remain applicable. Consult upstream component headers when modifying
or redistributing individual SDK code.

The stock RAK/Espressif bootloader and partition table stay on the device; they
are not bundled or relicensed by this project. No official Victron application,
extracted application resource, private device dump or pairing key is distributed.

The first-install wizard uses the official RAK uploader. It can reuse a matching
local installation or download the fixed RAK release directly from RAK's server;
the archive and executable are checked against pinned SHA-256 hashes. The project
does not bundle or relicense that executable. The temporary setup helper is built
with the same RAK SDK, whose notices and RAK-product restriction also apply.

License collection is a reproducible packaging aid, not legal advice or a claim
that every future SDK combination has been reviewed. Re-audit when changing SDKs.
