param([string]$RuiRoot=(Join-Path $env:LOCALAPPDATA 'Arduino15/packages/rak_rui/hardware/stm32/4.2.4'))
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$compilerRoot=Join-Path $env:LOCALAPPDATA 'Arduino15/packages/rak_rui/tools/arm-none-eabi-gcc/9-2019q4/bin'
$coreRoot=Join-Path $RuiRoot 'cores/STM32WLE'
$device=Join-Path $coreRoot 'external/STM32CubeWL/Drivers/CMSIS/Device/ST/STM32WLxx/Include'
$cmsis=Join-Path $coreRoot 'external/STM32CubeWL/Drivers/CMSIS/Include'
if(-not (Test-Path "$device/stm32wlxx.h") -or -not (Test-Path "$cmsis/core_cm4.h")){throw 'Missing STM32WL CMSIS headers'}
$output=Join-Path $projectRoot 'build_ram_loader'
New-Item -ItemType Directory -Force -Path $output|Out-Null
& "$compilerRoot/arm-none-eabi-gcc.exe" -Os -std=c11 -mcpu=cortex-m4 -mthumb -DSTM32WLE5xx -DCORE_CM4 -ffreestanding -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdlib -I $device -I $cmsis "-Wl,-T,$projectRoot/updater/ram-loader.ld" "-Wl,-Map,$output/loader.map" "$projectRoot/updater/ram-loader.c" -o "$output/loader.elf"
if($LASTEXITCODE -ne 0){throw 'SRAM loader compile failed'}
& "$compilerRoot/arm-none-eabi-objcopy.exe" -O binary "$output/loader.elf" "$output/loader.bin"
if($LASTEXITCODE -ne 0){throw 'SRAM loader extraction failed'}
& node "$PSScriptRoot/embed-ram-loader.mjs"
if($LASTEXITCODE -ne 0){throw 'SRAM loader verification failed'}
