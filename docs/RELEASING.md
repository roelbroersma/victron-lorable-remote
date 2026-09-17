# Release checklist

Original project code is MIT; linked SDK terms/notices are separate.
4.10.0 is a regular release for the documented native RAK11162 installation.
Factory bootstrap and untested hardware remain explicitly qualified.

1. Run C++ config/frame, network-policy/nonce-store and manager-integration tests, JS UI/codec tests
   and Python Bluetooth/OTA tests. Inspect desktop and mobile screenshots.
2. Generate the portal asset. Build tools/build.ps1 -Public with RAK RUI 4.2.4.
   Verify LORABLE_PUBLIC_BUILD, flash/RAM limits and runtime stability.
3. Build the ESP32-C2/26MHz application with ESP-IDF 5.5.5; pack with the version
   marker LoRaBLE-C2-26M-v4.10 and test corruption rejection.
4. Test USB migration, ESP browser OTA, preserved settings, profile persistence,
   a failed preferred join, fallback, preemption and return. Verify a received
   network response; TX_DONE alone does not prove server reception.
5. Record actual tested paths and limits in VALIDATION-v410.md. Restore temporary
   test profiles and leave unloaded outputs in the intended state.
6. Run node tools/prepare-release.mjs with a new empty staging directory. Its
   allowlist copies public sources/images/SDK notices and scans private values.
7. Review staged files. Exclude settings.local.h, private reports, bench helpers,
   research files, ESP bootloader/partition tables and installation credentials.
8. Copy reviewed files to the release checkout. The ZIP builder includes the
   checksum-listed files and notices, not unrelated/historical repository artifacts.
   Commit/push main and wait for CI before tagging v4.10.0.
9. The tag workflow verifies hashes/tests, creates a draft, uploads both images,
   manifest, SHA256SUMS and full ZIP, then publishes. Verify public asset digests.

Never put device secrets in CI. A regular release flag is not a claim that every
region, factory bootstrap route, I/O module or Bluetooth product is tested.
