# Validation — 4.10.0, 17 September 2026

Regular release for the documented native RAK11162 installation. These results
are evidence from one bench, not certification of every network or module.

## Hardware and update path

- RAK11162/RAK11160, STM32WLE5 + ESP8684/C2, 26 MHz, 2 MB ESP flash.
- RAK19010 base, RAK19012 USB power, RAK13001, unloaded relay output.
- Smart BatteryProtect 12/24V-100A, product A3B1. The MPPT is not present.
- Public STM32 4.10.0 built with RAK RUI 4.2.4; ESP 4.10.0 with ESP-IDF 5.5.5.
- STM32 USB YMODEM update completed. ESP browser OTA accepted the final packed
  application and returned to WiFi with the existing name/password, including spaces.
- Original ESP bootloader/table retained: storage 0x2A000/0xA6000 and application
  0xD0000/0x130000. No erase-all, test pads or new bootloader used for this update.
- Format-7/v4.9 configuration migrated to format 8. Existing OTAA credentials
  became network slot 0 without compiling installation secrets into the public image.

## Network bench tests

A temporary, deliberately unreachable profile was given highest priority, with
the real private UG65 profile second and a 15-minute preempt time. This exercises
a failed join and a real join-accept, not just simulated TX_DONE.

The first 4.10 integration build produced this RAM event sequence (seconds since boot):

| Time | Observed event |
|---:|---|
| 1 | Preferred test profile join attempt |
| 9 | Preferred join failed; backup residence began |
| 69 | UG65 backup join attempt |
| 76 | UG65 join successful |
| 81 | Uplink transmission completed |
| 909 | Preempt expired: preferred profile attempted again, without reboot |
| 916 | Preferred join failed |
| 976 | UG65 backup attempted again |
| 983 | UG65 join successful again |
| 988 | Uplink transmission completed again |

The final public build repeated the full cycle without reboot: preferred attempt
at 909 seconds, failure at 917, UG65 retry at 977, join success at 984 and uplink
completion at 988. Priorities and residence timers survived HTTP stress and a
Bluetooth command during this run. The temporary test profile/key was then removed
and the original private-network priority restored through the protected save API.

Final-build large-form regression: twelve fully populated 4033-byte, intentionally
invalid forms were rejected with HTTP 400 without reboot or configuration revision
change. A 7800-byte malformed form was rejected by STM32 with 400; 7801 bytes was
rejected by ESP with 413. The old 2400-byte STM32 assembly/content-length limit
found during this test was replaced with shared limits and a bounded 30-second
assembly deadline. No partial configuration was committed.

Re-saving identical settings preserved revision 11 and uptime (164 to 172 seconds),
without restarting. An idempotent BatteryProtect ON command then succeeded on
the RAK/ESP in one attempt: mode/output readback successful, output state 1, relay
still off. WiFi paused for Bluetooth and returned; LoRa remained joined.

An accepted join proves a response from the network. Local uplink completion
does not prove the gateway application decoded every payload. No actual TTN
end-device credentials were provisioned for this bench test.

## Offline regression tests passed

- Actual config/frame code: CBOR truncations and malformed tails, wrong instance,
  no embedded-pattern false positives, ten full GATT functions, range checks,
  profile isolation, zero JoinEUI, old-format migration, A/B CRC persistence,
  interrupted-write fallback and unchanged-save suppression.
- Network policy: four stable slots, priority and disabled profiles, staying on
  an unreachable backup until its timer expires, healthy backup return, progress
  to lower backups, successful preference retention and 32-bit time rollover.
- TTN budget: six attempts per RAM budget day, per-slot one-hour cooldown even
  across preempt/profile switches, minimum four-hour health interval, actual-ACK
  versus missed-check decisions and two-failure transition.
- Identity-keyed JoinNonce ledger: independent values, unchanged-save suppression,
  torn-write recovery and corruption failing closed. Settings imports do not
  contain this ledger. DevNonce block reservation saturates without wrapping.
- Actual network_manager.cpp with a RUI/LoRaMAC test double: correct RAM key pair
  selection, independent server nonces across preempt/rejoin, one reservation per
  sixteen attempts, reboot skipping, no RF on reservation failure/exhaustion,
  no accepted session after ledger failure, missed-ACK handling and TTN status clamp.
- Seven Python BatteryProtect protocol tests; UG65 and TTN codec wrappers,
  function 10, invalid ports/lengths and MPPT/Generic/BatteryProtect separation.
- ESP package roundtrip and rejection of truncation, extension and header/body
  corruption; shipped compressed UI exactly matches its HTML source.
- Isolated Chrome/Playwright UI tests: buttons and actual drag/drop priority,
  stable key slots, typing retains focus, four profiles, preempt status, largest
  percent-expanded form, ten functions, conditional fields, Class C notice and
  deliberate Class A override, power units, NL/EN and 390-pixel mobile layout.
  No JavaScript exceptions or unsolicited saves. Mobile screenshots inspected.

Screenshots in docs/images use simulated, non-private data, not live gateway
screenshots. UG65 settings are documented as a table.

## Flash behavior and memory

Settings write only on change; logs, countdowns, retry and airtime-budget counters
are RAM-only. Successful new JoinNonces necessarily write an A/B security record.
DevNonces are reserved in blocks of 16 before transmission in RUI NVM. This reduces
retry writes, but reboot skips unused reserved values. It is not a claim of zero
wear or proof against every physical power-failure scenario.

The new ledger uses user-flash pages 0x4800/0x5000; settings remain at 0x5800/0x6000.
Eight historical identity tuples are retained without eviction; exhaustion or a
corrupt ledger fails closed. Never erase it or downgrade to recover old nonces.
See [network documentation](docs/NETWORKS.md) for limits and RUI coupling.

STM32 reported program usage: 176216 / 200704 bytes (87%). Reported globals:
46792 / 48640 bytes (96%), so the Arduino low-memory warning remains relevant.
The linker also reserves 7680 bytes heap and 8192 bytes stack; the reported 1848
bytes is not the complete stack allocation. HTTP/config stress is required and
long-duration radio/UI stress remains a separate acceptance test.

ESP application: 899616 bytes; approximately 28% of its retained app slot free.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Public STM32 .bin | 191496 | e9d410af72a9cd8be129a00b72d671e0db78143fe78f9dd94c57e80e3e776649 |
| ESP browser .packed | 516792 | 13dfdf9d4a8569a3f766d839e48f6b830c80800bc7115b2829a5edcd66f7e251 |
| ESP uncompressed application | 899616 | d37fd0a8c0f481d29ff06af92a331c2512d75aa69cf55aff9ef7195ec2256fd3 |

## Remaining acceptance tests / limitations

- Live TTN join, Class C downlink and measured real-traffic fair use. Conservative
  intervals are not a complete airtime limiter; counters restart after reboot.
- Physical missed-ACK failover (decision logic is host-tested), every downlink/input
  route, every region, real MPPT, other BatteryProtect variants and RAK13007.
- Fresh factory ESP-AT to native installation without test pads, whole-device
  wireless OTA, signed images, automatic ESP rollback or self-contained installer exe.
- Complete-board power measurements, long-duration stress and physical power-loss
  injection during ESP rewriting. This is not a safety-critical controller.

Earlier physical BatteryProtect OFF/ON and independent PC readback were validated
for 4.9. The 4.10 release keeps that protocol and does not widen the product claim.

## Reproduce

Host C++11: compile tools/tests/config-and-frames.cpp and network-policy.cpp with
`-I tools/tests`; compile network-manager.cpp with `-I tools/tests/manager -I tools/tests`.
Run tools/test-codec.cjs, test-batteryprotect-protocol.py,
test-ota-pack.py and test-web-v410.cjs. The UI test requires Playwright and Chrome;
set PLAYWRIGHT_PATH if needed. Build scripts only build, never flash automatically.
The public packaging allowlist excludes private bench helpers, keys and raw dumps.
