param([string]$Port, [string]$Uploader, [switch]$CheckOnly)
$ErrorActionPreference='Stop'
$releaseRoot=Split-Path -Parent $PSScriptRoot
$manifestPath=Join-Path $releaseRoot 'firmware/manifest.json'
if(-not (Test-Path -LiteralPath $manifestPath)){throw 'Extract the complete release ZIP first; firmware/manifest.json is missing.'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$entry=$manifest.images|Where-Object {$_.target -eq 'stm32-usb'}
if(@($entry).Count -ne 1){throw 'Invalid manifest: expected one STM32 image.'}
$imagePath=[IO.Path]::GetFullPath((Join-Path $releaseRoot $entry.path))
$firmwareRoot=[IO.Path]::GetFullPath((Join-Path $releaseRoot 'firmware'))+[IO.Path]::DirectorySeparatorChar
if(-not $imagePath.StartsWith($firmwareRoot,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($imagePath) -ne '.bin'){throw 'Invalid image path.'}
if((Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash -ne $entry.sha256){throw 'Firmware checksum mismatch; download a fresh release.'}
Write-Host "Verified $($manifest.version) STM32 image. This does NOT install ESP firmware."
if($CheckOnly){return}
if(-not $Uploader){
 $uploaderRoot=Join-Path $env:LOCALAPPDATA 'Arduino15/packages/rak_rui/tools/uploader_ymodem'
 if(Test-Path -LiteralPath $uploaderRoot){$Uploader=Get-ChildItem -LiteralPath $uploaderRoot -Filter 'uploader_ymodem.exe' -File -Recurse|Sort-Object FullName -Descending|Select-Object -First 1 -ExpandProperty FullName}
}
if(-not $Uploader -or -not (Test-Path -LiteralPath $Uploader)){throw 'Official RAK uploader not found. Install the RAK RUI BSP, specify -Uploader, or use the official RAK Device Firmware Upgrade Tool with the verified .bin. See docs/INSTALL.md.'}
$ports=@([IO.Ports.SerialPort]::GetPortNames()|Sort-Object)
Write-Host ('Available USB/serial ports: '+($ports -join ', '))
if(-not $Port){$Port=Read-Host 'STM32 board USB port (NOT an ESP test adapter), e.g. COM3'}
if($Port -notmatch '^COM[1-9][0-9]*$' -or $ports -notcontains $Port){throw 'Select an existing COM port.'}
Write-Host 'Close Serial Monitor. Disconnect sensitive loads. Keep board USB power connected.'
if((Read-Host "Type FLASH to update the RAK11160/RAK11162 STM32 on $Port") -cne 'FLASH'){Write-Host 'Cancelled';return}
& $Uploader -f $imagePath -p $Port
if($LASTEXITCODE -ne 0){throw 'Uploader reported an error. Do not erase/reset the board; inspect its state before retrying.'}
Write-Host 'Uploader finished. Confirm Upgrade Complete, then reconnect WiFi and check both firmware versions and settings.'
