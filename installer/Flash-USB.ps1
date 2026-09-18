[CmdletBinding()]
param([string]$Port,[string]$Firmware,[switch]$CheckOnly,[switch]$Yes,[switch]$DryRun)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Bundle.ps1')
. (Join-Path $PSScriptRoot 'Usb-Portal.ps1')
$releaseRoot=Split-Path -Parent $PSScriptRoot
if(-not $Firmware){
 $files=@(Get-ChildItem -LiteralPath (Join-Path $releaseRoot 'firmware') -Filter 'LoRaBLE-Remote-*.bin' -File)
 if($files.Count -ne 1){throw 'Select the complete .bin with -Firmware, or extract the complete release ZIP.'}
 $Firmware=$files[0].FullName
}
$bundle=[LoRaBLE.Bundle]::Read([IO.Path]::GetFullPath($Firmware))
Write-Host "Complete LoRaBLE firmware $($bundle.Version) verified ($($bundle.Bytes.Length) bytes)."
if($CheckOnly){return}
$ports=@([IO.Ports.SerialPort]::GetPortNames()|Sort-Object)
if(-not $Port){Write-Host ('Available USB ports: '+($ports -join ', '));$Port=Read-Host 'Board USB port, e.g. COM3'}
if($Port -notmatch '^COM[1-9][0-9]*$' -or $ports -notcontains $Port){throw 'Select an existing board USB port.'}
Write-Host 'Close Serial Monitor. Keep board USB power connected until installation is complete.'
if(-not $Yes -and (Read-Host "Type UPDATE to install firmware $($bundle.Version) on $Port") -cne 'UPDATE'){Write-Host 'Cancelled';return}
$session=$null;$readyWatch=[Diagnostics.Stopwatch]::StartNew();$readyError='No response'
while(-not $session -and $readyWatch.Elapsed.TotalSeconds -lt 45){
 try{$session=Invoke-LoRaBLEUsbHttp -Port $Port -Route /session}catch{$readyError=$_.Exception.Message;Start-Sleep -Seconds 3}
}
if(-not $session){throw "USB updater unavailable. No firmware was sent. $readyError"}
if(-not $session.native){throw 'This device needs first-install migration. No firmware has been sent.'}
Write-Host 'Uploading the complete firmware through USB. WiFi connection is not required.'
$reply=Invoke-LoRaBLEUsbHttp -Port $Port -Method POST -Route /ota -Body $bundle.Bytes -Token $session.token -DryRun:$DryRun
if(-not $reply.ok){throw 'Device did not accept the firmware.'}
Write-Host 'Upload verified. Installing; keep power connected while the device restarts.'
$watch=[Diagnostics.Stopwatch]::StartNew();$wanted=if($DryRun){6}else{4};$lastPhase=-1
while($watch.Elapsed.TotalSeconds -lt 300){
 Start-Sleep -Seconds 4
 try{$state=Invoke-LoRaBLEUsbHttp -Port $Port -Route /update}catch{continue}
 if($state.phase -eq 5){throw "Installation stopped (device error $($state.error)). Keep power connected; do not erase the board."}
 if($state.phase -ne $lastPhase){Write-Host "Device update phase: $($state.phase)";$lastPhase=$state.phase}
 if($state.phase -eq $wanted){
  $after=Invoke-LoRaBLEUsbHttp -Port $Port -Route /session
  if(-not $DryRun -and $after.esp_firmware -cne $bundle.Version){throw 'Installed firmware version does not match the selected file.'}
  Write-Host $(if($DryRun){'USB transfer and device validation completed; installed firmware was not replaced.'}else{"LoRaBLE $($bundle.Version) installation complete. Settings retained."})
  return
 }
}
throw 'Completion could not be confirmed. Keep power connected and inspect device status before retrying.'
