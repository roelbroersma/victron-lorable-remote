# Release checklist

This repository uses MIT for its original code. Binary SDK notices remain
separate. The first publication is **4.9.0-rc1**, a prerelease, not a claim of
universal factory-module compatibility.

1. Run the C++ config/frame tests, JavaScript UI/codec tests and Python packet tests.
2. Generate the portal asset and build `tools/build.ps1 -Public`. Confirm that
   `LORABLE_PUBLIC_BUILD` is present in build options. Never publish a personal build.
3. Build the matching ESP application using IDF 5.5.5. Pack it using
   `pack-esp-ota.py --version LoRaBLE-C2-26M-v4.9.0-rc1`, then verify the package.
4. Validate actual USB update, ESP OTA/reconnect, saved-setting migration and
   Bluetooth control. Record limits and hashes in `VALIDATION-v49.md`.
5. Run `node tools/prepare-release.mjs <new-empty-staging-directory>`. It copies an
   explicit source allowlist, public binaries and SDK notices, verifies public-build
   flags, checks known private installation values and creates hashes/manifest.
   It never removes existing files. Review the staged files separately before publishing.
6. Check that `settings.local.h`, raw flash dumps, ESP bootloader/partition binaries,
   research APK/resources, private reports and owner-only bench helpers are absent.
7. Commit the reviewed source plus `firmware/` artifacts. Keep native binaries in
   the release only; `git add -f firmware/` is needed because the global ignore
   intentionally excludes arbitrary build binaries.
8. Publish tag `v4.9.0-rc1` as a **prerelease**. Attach the STM32 .bin, ESP .packed,
   SHA256SUMS and complete ZIP. The ZIP retains installer/docs/dependency notices.
   A GitHub source archive alone also works once firmware files are included.

Do not remove the prerelease warning until factory bootstrap, TTN join/downlink,
protection-active scenarios and longer stability tests have been completed.
Automatic two-profile failover is a separate feature and is not present in 4.9.

No secrets belong in GitHub Actions variables for building these public images.
No public release should include a common private-network AppKey.
