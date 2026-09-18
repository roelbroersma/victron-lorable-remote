[CmdletBinding()]
param(
 [string]$Port,[string]$Firmware,[string]$Uploader,[string]$WifiInterface,
 [ValidateSet('','NL','EN')][string]$Language='',
 [switch]$CheckOnly,[switch]$InspectOnly,[switch]$Yes,[switch]$RequestElevation
)
$ErrorActionPreference='Stop'
if(-not $Language){
 $Language=if([Globalization.CultureInfo]::CurrentUICulture.Name -like 'nl*'){'NL'}else{'EN'}
 if(-not $Yes -and -not $CheckOnly -and -not $InspectOnly){
  $choice=(Read-Host "Taal / Language: NL or EN [$Language]").Trim().ToUpperInvariant()
  if($choice){if($choice -notin @('NL','EN')){throw 'Choose NL or EN'};$Language=$choice}
 }
}
$script:SetupLanguage=$Language
. (Join-Path $PSScriptRoot 'FirstInstall.Core.ps1')
$releaseRoot=Split-Path -Parent $PSScriptRoot
if(-not $Firmware){
 $images=@(Get-ChildItem -LiteralPath (Join-Path $releaseRoot 'firmware') -Filter 'LoRaBLE-Remote-*.bin' -File)
 if($images.Count -ne 1){throw 'Extract the complete installation ZIP, or select one complete .bin with -Firmware.'}
 $Firmware=$images[0].FullName
}
$Firmware=[IO.Path]::GetFullPath($Firmware)
$bundle=[LoRaBLE.Bundle]::Read($Firmware)
$helper=Read-SetupHelper (Join-Path $PSScriptRoot 'bootstrap-image.json')
Write-Host "Victron LoRaBLE Remote - $($bundle.Version)"
if($CheckOnly){Write-SetupMessage 'Firmware en installatiehulp gecontroleerd. Geen apparaat of netwerk gewijzigd.' 'Firmware and installation helper verified. No device or network changed.';return}

if(-not $Port){
 Write-SetupMessage 'Sluit het board aan met een USB-datakabel. Sluit Serial Monitor.' 'Connect the board using a USB data cable. Close Serial Monitor.'
 Get-CimInstance Win32_PnPEntity | Where-Object {$_.Name -match '\(COM[0-9]+\)'} | ForEach-Object {Write-Host $_.Name}
 $Port=(Read-Host 'USB COM port (e.g. COM3)').Trim().ToUpperInvariant()
}
if($Port -notmatch '^COM[1-9][0-9]*$' -or [IO.Ports.SerialPort]::GetPortNames() -notcontains $Port){throw 'Select an existing USB COM port.'}
$mutex=[Threading.Mutex]::new($false,"Local\LoRaBLE-Install-$Port");$owned=$false
$wifi=$null;$server=$null;$state=$null;$journal=$null;$work=$null;$completed=$false
$wifiMutex=$null;$wifiOwned=$false
try{
 try{$owned=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$owned=$true}
 if(-not $owned){throw 'Another LoRaBLE installer is already using this port.'}
 $eui=Get-SetupIdentity $Port
 Write-Host "RAK11162 / RAK11160 - DevEUI $eui - $Port"
 if($InspectOnly){
  $wifi=[LoRaBLE.Install.Wifi]::new();$wifi.Interfaces()|Select-Object Name,State,Id
  Write-SetupMessage 'Board herkend; niets geflasht en geen netwerk gewijzigd.' 'Board identified; no firmware or network changes.'
  return
 }
 if(-not $Yes){
  Write-SetupMessage 'Koppel belastingen los. Laat USB-voeding aangesloten tot de installatie klaar is.' 'Disconnect loads. Keep USB power connected until installation finishes.'
  if((Read-Host 'Type INSTALL to continue') -cne 'INSTALL'){return}
 }
 # Converted boards use the existing verified USB-only path; no WiFi changes/UAC.
 $native=$null
 try{$native=Invoke-LoRaBLEUsbHttp -Port $Port -Route /session}catch{}
 if($native -and $native.native -and $native.update_format -eq 1 -and $native.ota_target -eq 'rak11162'){
  $journal=Join-Path $env:LOCALAPPDATA ("LoRaBLE-Remote/install-state/$eui.json")
  if(Test-Path -LiteralPath $journal){
   $state=Get-Content -LiteralPath $journal -Raw|ConvertFrom-Json
   if($state.eui -cne $eui -or $state.digest -cne $bundle.Sha256){throw 'Resume the previous installation with exactly the same complete firmware file first.'}
   $wifi=[LoRaBLE.Install.Wifi]::new()
  }
  Write-SetupMessage 'Complete updater aanwezig: verder via alleen USB.' 'Complete updater detected: continuing through USB only.'
  & (Join-Path $PSScriptRoot 'Flash-USB.ps1') -Port $Port -Firmware $Firmware -Yes
  $completed=$true
  Write-SetupMessage 'Installatie voltooid. De bestaande instellingen zijn behouden.' 'Installation complete. Existing settings have been retained.'
  return
 }
 $administrator=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 if(-not $administrator){
  if($RequestElevation){
   # Interactive installer window intentionally visible; Windows asks for UAC.
   $arguments='-NoExit -NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Port "'+$Port+'" -Firmware "'+$Firmware+'" -Language '+$Language+' -Yes'
   if($Uploader){$arguments+=' -Uploader "'+[IO.Path]::GetFullPath($Uploader)+'"'}
   if($WifiInterface){$arguments+=' -WifiInterface "'+([guid]$WifiInterface).ToString()+'"'}
   $owned=$false;$mutex.ReleaseMutex()
   $windowsPowerShell=Join-Path ([Environment]::GetFolderPath('System')) 'WindowsPowerShell/v1.0/powershell.exe'
   Start-Process -FilePath $windowsPowerShell -Verb RunAs -ArgumentList $arguments -Wait
   return
  }
  throw 'First installation requires administrator rights for one temporary, restricted firewall rule. Use Start-First-Install.cmd or run PowerShell as administrator.'
 }
 $wifi=[LoRaBLE.Install.Wifi]::new();$interfaces=@($wifi.Interfaces()|Where-Object {$_.State -in @(1,4)})
 if($WifiInterface){$interfaces=@($interfaces|Where-Object {$_.Id -eq [guid]$WifiInterface})}
 if($interfaces.Count -eq 0){throw 'Enable a WiFi adapter. Allow Windows location access if required for WiFi control.'}
 $selected=0
 if($interfaces.Count -gt 1){
  for($i=0;$i -lt $interfaces.Count;$i++){Write-Host "$($i+1): $($interfaces[$i].Name)"}
  $choice=Read-Host 'WiFi adapter number'
  if($choice -notmatch '^\d+$' -or [int]$choice -lt 1 -or [int]$choice -gt $interfaces.Count){throw 'Invalid WiFi adapter selection'}
  $selected=[int]$choice-1
 }
 $wireless=$interfaces[$selected]
 $wifiMutex=[Threading.Mutex]::new($false,('Local\LoRaBLE-Wifi-Install-'+$wireless.Id.ToString()))
 try{$wifiOwned=$wifiMutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$wifiOwned=$true}
 if(-not $wifiOwned){throw 'Another installation is already using this WiFi adapter.'}
 $adapters=@(Get-NetAdapter | Where-Object {$_.InterfaceGuid -eq $wireless.Id})
 if($adapters.Count -ne 1){throw 'Cannot identify the WiFi network interface'}
 $index=$adapters[0].ifIndex
 $ipInterface=Get-NetIPInterface -InterfaceIndex $index -AddressFamily IPv4
 if($ipInterface.Dhcp -ne 'Enabled'){throw 'The selected WiFi adapter needs automatic IP addressing (DHCP). Its static configuration has not been changed.'}
 $uploaderPath=Get-SetupUploader -ExplicitPath $Uploader
 $directory=Join-Path $env:LOCALAPPDATA 'LoRaBLE-Remote/install-state';New-Item -ItemType Directory -Force -Path $directory|Out-Null
 $journal=Join-Path $directory ($eui+'.json')
 $work=New-Item -ItemType Directory -Path (Join-Path $env:TEMP ('LoRaBLE-Install-'+[guid]::NewGuid().ToString('N')))
 $resumeNative=$false
 if(Test-Path -LiteralPath $journal){
  $state=Get-Content -LiteralPath $journal -Raw|ConvertFrom-Json
  if($state.eui -cne $eui -or $state.digest -cne $bundle.Sha256){throw 'A previous installation is recorded for this board. Resume with exactly the same complete firmware file; no automatic overwrite was attempted.'}
  Restore-SetupNetwork $wifi $state
  # Refresh the saved connection after restoring an interrupted install's WiFi.
  Start-Sleep -Seconds 2
  $restored=@($wifi.Interfaces()|Where-Object {$_.Id -eq $wireless.Id})
  if($restored.Count -eq 1){$wireless=$restored[0]}
  if($state.phase -in @('DownloadRequested','NativeSeen','ControlInstalling')){
   Write-SetupMessage 'Vorige installatie controleren zonder reset...' 'Checking previous installation without resetting...'
   $ready=Invoke-SetupSerial -Port $Port -Command 'ATC+SETUP=4' -Until 'LBR_SETUP_NATIVE fw=([0-9a-z.-]+)\r?\n' -Timeout 60
   if($ready -notmatch ('LBR_SETUP_NATIVE fw='+[regex]::Escape($bundle.Version)+'\r?\n')){throw 'Previous installation needs recovery. Keep power connected; do not erase the board.'}
   $resumeNative=$true
  }
 }
 $state=[ordered]@{format=1;eui=$eui;digest=$bundle.Sha256;version=$bundle.Version;phase='Preparing';interface=$wireless.Id.ToString();original=$wireless.Profile;profile='';rule=''}
 Save-SetupJournal $journal $state
 if(-not $resumeNative){
  $subnet=Get-FreeSetupSubnet;$secret=New-SetupSecret;$password=New-SetupSecret 12
  $ssid='LoRaBLE-Setup-'+$eui.Substring(10)+'-'+$secret.Substring(0,4)
  $state.profile='LoRaBLE-Install-'+[guid]::NewGuid().ToString('N')
  $state.rule='LoRaBLE-Install-'+[guid]::NewGuid().ToString('N')
  Write-SetupMessage 'Stap 1/5: tijdelijke installatiehulp via USB plaatsen...' 'Step 1/5: installing the temporary USB setup helper...'
  $state.phase='HelperInstalling';Save-SetupJournal $journal $state
  Invoke-SetupUpload $uploaderPath $Port $helper $work.FullName
  $info=Invoke-SetupSerial -Port $Port -Command 'ATC+SETUP=0' -Until 'LBR_SETUP_V1 eui=[0-9A-F]{16} .*\r?\n' -Timeout 15
  if($info -notmatch "LBR_SETUP_V1 eui=$eui "){throw 'Setup helper identity does not match the selected board'}
  Write-SetupMessage 'Stap 2/5: fabrieksfirmware controleren en installatie-WiFi maken...' 'Step 2/5: checking factory firmware and preparing setup WiFi...'
  $ap=Invoke-SetupSerial -Port $Port -Command ('ATC+SETUP=1:{0}:{1}:{2}' -f $secret,$subnet,$password) -Until '(LBR_SETUP_AP [^\r\n]+|LBR_SETUP_NATIVE fw=[0-9a-z.-]+)\r?\n' -Timeout 90
  if($ap -match 'LBR_SETUP_NATIVE fw=([0-9a-z.-]+)'){
   # Preserve an existing companion. Do not silently send it stock-AT commands.
   if($Matches[1] -cne $bundle.Version){
    Invoke-SetupUpload $uploaderPath $Port $bundle.Control $work.FullName
    throw 'This is an older LoRaBLE installation, not factory firmware. The control application has been restored; the companion still needs version migration.'
   }
   $resumeNative=$true
  }else{
   if($ap -notmatch ('LBR_SETUP_AP ssid='+[regex]::Escape($ssid)+' ip=192\.168\.'+$subnet+'\.1\r?\n')){throw 'Unexpected setup network identity'}
   $state.phase='WiFiPrepared';Save-SetupJournal $journal $state
   Write-SetupMessage 'Stap 3/5: pc tijdelijk verbinden met de WiFi van het board...' 'Step 3/5: temporarily connecting the PC to the board WiFi...'
   $wifi.AddTemporary($wireless.Id,$state.profile,$ssid,$password);$password=$null
   $wifi.Connect($wireless.Id,$state.profile)
   $local=Wait-SetupWifi $wifi $wireless.Id $state.profile $ssid $subnet $index
   $remote="192.168.$subnet.1"
   $server=[LoRaBLE.Install.FirmwareServer]::new($local,$remote,0,$secret,$bundle.Connectivity)
   if($server.Port -lt 49152 -or $server.Port -gt 65535){throw 'Windows assigned a server port outside the permitted temporary range'}
   $program=(Get-Process -Id $PID).Path
   New-NetFirewallRule -Name $state.rule -DisplayName 'LoRaBLE temporary first installation' -Direction Inbound -Action Allow -Enabled True -Profile Any -Protocol TCP -LocalPort $server.Port -LocalAddress $local -RemoteAddress $remote -InterfaceAlias $adapters[0].Name -Program $program | Out-Null
   $last=[int]$local.Split('.')[-1]
   [void](Invoke-SetupSerial -Port $Port -Command ('ATC+SETUP=2:{0}:{1}' -f $last,$server.Port) -Until 'LBR_SETUP_CHECKED\r?\n' -Timeout 40)
   if($server.HealthRequests -lt 1){throw 'The device did not verify the local firmware server'}
   Write-SetupMessage 'Stap 4/5: firmware installeren. Laat de voeding aangesloten...' 'Step 4/5: installing firmware. Keep power connected...'
   $state.phase='DownloadRequested';Save-SetupJournal $journal $state
   $answer=Invoke-SetupSerial -Port $Port -Command 'ATC+SETUP=3' -Until 'LBR_SETUP_NATIVE fw=[0-9a-z.-]+\r?\n' -Timeout 240
   if($server.CompletedRequests -lt 1 -or $answer -notmatch ('LBR_SETUP_NATIVE fw='+[regex]::Escape($bundle.Version)+'\r?\n')){throw 'New firmware startup could not be verified. Keep power connected; do not reset or erase.'}
   $server.Dispose();$server=$null
  }
 }
 $state.phase='NativeSeen';Save-SetupJournal $journal $state
 # A missing home access point must not prevent finishing the board installation.
 # Retain the journal on cleanup failure; finally retries without losing the names.
 try{
  Restore-SetupNetwork $wifi $state
  $state.profile='';$state.rule='';Save-SetupJournal $journal $state
 }catch{Write-Warning ('Network cleanup needs attention; finishing the board first: '+$_.Exception.Message)}
 Write-SetupMessage 'Stap 5/5: installatie via USB afronden en controleren...' 'Step 5/5: finishing installation and checking through USB...'
 $state.phase='ControlInstalling';Save-SetupJournal $journal $state
 Invoke-SetupUpload $uploaderPath $Port $bundle.Control $work.FullName
 $session=$null;$watch=[Diagnostics.Stopwatch]::StartNew()
 while(-not $session -and $watch.Elapsed.TotalSeconds -lt 60){try{$session=Invoke-LoRaBLEUsbHttp -Port $Port -Route /session}catch{Start-Sleep -Seconds 2}}
 if(-not $session -or $session.esp_firmware -cne $bundle.Version -or $session.update_format -ne 1 -or $session.ota_target -ne 'rak11162' -or (Get-SetupIdentity $Port) -cne $eui){throw 'Final installation identity/version check did not pass. Keep power connected.'}
 $state.phase='Complete';Save-SetupJournal $journal $state;$completed=$true
 Write-SetupMessage 'Installatie voltooid! Verbind met de WiFi van het board en open http://192.168.4.1/.' 'Installation complete! Connect to the board WiFi and open http://192.168.4.1/.'
 Write-SetupMessage 'Nieuwe installatie: WiFi Victron LoRaBLE Remote, wachtwoord CHANGE-ME-FIRST. Wijzig dit meteen. Bestaande instellingen blijven behouden.' 'New installation: WiFi Victron LoRaBLE Remote, password CHANGE-ME-FIRST. Change it immediately. Existing settings are retained.'
}finally{
 if($server){$server.Dispose()}
 $clean=$true
 if($wifi -and $state){try{Restore-SetupNetwork $wifi $state}catch{$clean=$false;Write-Warning ('Could not finish network cleanup: '+$_.Exception.Message)}}
 if($wifi){$wifi.Dispose()}
 if($wifiMutex){if($wifiOwned){$wifiMutex.ReleaseMutex()};$wifiMutex.Dispose()}
 if($completed -and $clean -and $journal -and (Test-Path -LiteralPath $journal)){Remove-Item -LiteralPath $journal}
 # Remove only our now-empty unique working directory; never recursively delete.
 if($work -and (Test-Path -LiteralPath $work.FullName)){if(@(Get-ChildItem -LiteralPath $work.FullName -Force).Count -eq 0){Remove-Item -LiteralPath $work.FullName}}
 if($owned){$mutex.ReleaseMutex()};$mutex.Dispose()
}
