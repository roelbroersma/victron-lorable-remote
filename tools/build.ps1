param(
 [string]$ArduinoCli='C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
 [ValidateSet('Native','Recovery','Bootstrap')][string]$Mode='Native',
 [switch]$Public
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$installedCores=& $ArduinoCli core list --format json | ConvertFrom-Json
if($LASTEXITCODE -ne 0){throw 'Cannot inspect installed Arduino cores'}
$rakCore=$installedCores.platforms | Where-Object {$_.id -eq 'rak_rui:stm32'}
if($rakCore.installed_version -ne '4.2.4'){
 throw 'This network manager requires RAK RUI STM32 BSP 4.2.4. Install that exact version; audit other BSPs before use.'
}
& node (Join-Path $PSScriptRoot 'build-web.mjs')
if($LASTEXITCODE -ne 0){throw 'Web asset generation failed'}
& (Join-Path $PSScriptRoot 'build-ram-loader.ps1')
# LoRaWAN only; unused P2P and LA915 are excluded to fit the STM32 flash.
# Optimize C++ across files without enabling LTO for the BSP's legacy C code,
# which contains mismatched implicit declarations diagnosed by full-core LTO.
$extra='-DDEBUG -DLEGACY_BLE_AT=0 -flto -fno-strict-aliasing'
$buildFolder='build_native411'
if($Public){$extra+=' -DLORABLE_PUBLIC_BUILD';$buildFolder='build_public411'}
if($Mode -eq 'Recovery'){
 $extra+=' -DESP_RECOVERY_SSID=1'
 $buildFolder='build_recovery410'
}
if($Mode -eq 'Bootstrap'){
 $extra+=' -DESP_MIGRATION_TO_NATIVE=1'
 $buildFolder=if($Public){'build_bootstrap_public410'}else{'build_bootstrap410'}
}
& $ArduinoCli compile --fqbn 'rak_rui:stm32:WisDuoRAK11160Board:supportlora=2,supportLA915=2' --build-property "compiler.cpp.extra_flags=$extra" --build-property 'compiler.c.elf.extra_flags=-flto -fno-strict-aliasing -Wl,--wrap=serial_fallback_handler' --build-property 'compiler.ar.cmd=arm-none-eabi-gcc-ar' --build-path (Join-Path $projectRoot $buildFolder) (Join-Path $projectRoot 'stm32')
if($LASTEXITCODE -ne 0){throw 'Firmware build failed'}
Write-Host 'Build only; no upload. See docs/DEVELOPMENT.md for packaging. Public builds exclude settings.local.h.'
