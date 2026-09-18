# MIT, Copyright (c) 2026 Roel Broersma.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Bundle.ps1')
. (Join-Path $PSScriptRoot 'Usb-Portal.ps1')
if(-not ('LoRaBLE.Install.Wifi' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'FirstInstall.Windows.cs')}

function Write-SetupMessage([string]$Nl,[string]$En){Write-Host $(if($script:SetupLanguage -eq 'NL'){$Nl}else{$En})}
function New-SetupSecret([int]$Bytes=16){
 $data=[byte[]]::new($Bytes);$rng=[Security.Cryptography.RandomNumberGenerator]::Create()
 try{$rng.GetBytes($data);[BitConverter]::ToString($data).Replace('-','').ToLowerInvariant()}finally{$rng.Dispose()}
}
function Invoke-SetupSerial {
 param([string]$Port,[string]$Command,[string]$Until='(?m)^OK\r?$', [int]$Timeout=8)
 if($Port -notmatch '^COM[1-9][0-9]*$'){throw 'Invalid board USB port'}
 $s=[IO.Ports.SerialPort]::new($Port,115200,'None',8,'One');$s.DtrEnable=$false;$s.RtsEnable=$false;$s.WriteTimeout=3000
 try{
  $s.Open();Start-Sleep -Milliseconds 150;$s.Write("`r`n");Start-Sleep -Milliseconds 100;$s.DiscardInBuffer()
  $s.Write($Command+"`r`n");$watch=[Diagnostics.Stopwatch]::StartNew();$answer=''
  while($watch.Elapsed.TotalSeconds -lt $Timeout){
   $answer+=$s.ReadExisting();if($answer.Length -gt 16384){$answer=$answer.Substring($answer.Length-8192)}
   if($answer -match 'LBR_SETUP_ERROR ([a-z_]+)'){throw "Board setup stopped: $($Matches[1]). No automatic reset or erase is performed."}
   if($answer -match $Until){return $answer}
   if($answer -match '(?m)^(AT_(PARAM_ERROR|ERROR|COMMAND_NOT_FOUND)|ERROR)\r?$'){throw 'Board does not support this setup command.'}
   Start-Sleep -Milliseconds 40
  }
  throw 'Board response timed out. Keep power connected; do not erase or reset during installation.'
 }finally{if($s.IsOpen){$s.Close()};$s.Dispose()}
}
function Get-SetupIdentity([string]$Port){
 $model=Invoke-SetupSerial -Port $Port -Command 'AT+HWMODEL=?'
 if($model -notmatch '(?im)^AT\+HWMODEL=(rak11160|rak11162)\s*$'){throw 'Not a supported RAK11160/RAK11162 board. Nothing flashed.'}
 $device=Invoke-SetupSerial -Port $Port -Command 'AT+DEVEUI=?'
 if($device -notmatch '(?im)^AT\+DEVEUI=([0-9a-f]{16})\s*$'){throw 'Cannot read a valid board identity. Nothing flashed.'}
 $eui=$Matches[1].ToUpperInvariant();if($eui -in @('0000000000000000','FFFFFFFFFFFFFFFF')){throw 'Invalid board identity'}
 return $eui
}
function Read-SetupHelper([string]$Path){
 $resource=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
 if($resource.format -ne 1 -or $resource.target -ne 'RAK11162' -or $resource.protocol -ne 1){throw 'Wrong first-install helper'}
 $bytes=[Convert]::FromBase64String($resource.image_base64)
 if($bytes.Length -lt 256 -or $bytes.Length -gt 0x31000 -or $bytes.Length%8){throw 'Invalid helper image size'}
 $sp=[BitConverter]::ToUInt32($bytes,0);$entry=[BitConverter]::ToUInt32($bytes,4)
 if($sp -lt 0x20000000 -or $sp -gt 0x20010000 -or $sp%8 -or -not ($entry-band 1) -or $entry -lt 0x08006000 -or $entry -ge 0x08006000+$bytes.Length){throw 'Invalid helper application vectors'}
 $sha=[Security.Cryptography.SHA256]::Create()
 try{$digest=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
 if($digest -cne $resource.sha256){throw 'First-install helper checksum mismatch'}
 return ,$bytes
}
function Get-SetupUploader {
 param([string]$ExplicitPath)
 $expected='57fc9cb007cc9eabfad10cc96ca50d01b86fc554c116414ebaE270c7d260800d'.ToLowerInvariant()
 $cache=Join-Path $env:LOCALAPPDATA 'LoRaBLE-Remote/tools/rak-uploader-1.0.1'
 $exe=Join-Path $cache 'win32/uploader_ymodem.exe'
 $installed=Join-Path $env:LOCALAPPDATA 'Arduino15/packages/rak_rui/tools/uploader_ymodem/1.0.1/uploader_ymodem.exe'
 $candidates=if($ExplicitPath){@($ExplicitPath)}else{@($installed,$exe)}
 foreach($candidate in $candidates){if(Test-Path -LiteralPath $candidate){
   if((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){throw 'RAK uploader checksum mismatch'}
   return [IO.Path]::GetFullPath($candidate)
 }}
 if($ExplicitPath){throw 'Selected RAK uploader does not exist'}
 Write-SetupMessage 'Officieel RAK-hulpprogramma downloaden (eenmalig, 6,6 MB)...' 'Downloading official RAK utility (once, 6.6 MB)...'
 New-Item -ItemType Directory -Force -Path $cache|Out-Null
 $archive=Join-Path $cache ('download-'+[guid]::NewGuid().ToString('N')+'.tar.gz')
 try{
  [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
  Invoke-WebRequest -UseBasicParsing -Uri 'https://downloads.rakwireless.com/RUI/RUI3/Tools/uploader_ymodem_win32_v1.0.1.tar.gz' -OutFile $archive
  if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '51D95A353D30ECF89298FD9A792CCA295D4580481A2CA2070A222D1819132518'){throw 'RAK download checksum mismatch'}
  # Fixed, checksum-pinned vendor archive; extract only its known executable.
  & tar.exe -xzf $archive -C $cache 'win32/uploader_ymodem.exe'
  if($LASTEXITCODE -ne 0){throw 'Could not unpack the official RAK utility'}
  if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){throw 'RAK uploader checksum mismatch'}
  return $exe
 }finally{if(Test-Path -LiteralPath $archive){Remove-Item -LiteralPath $archive}}
}
function Invoke-SetupUpload([string]$Uploader,[string]$Port,[byte[]]$Image,[string]$Directory){
 $imageFile=Join-Path $Directory ('control-'+[guid]::NewGuid().ToString('N')+'.bin')
 [IO.File]::WriteAllBytes($imageFile,$Image)
 try{
  $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Uploader
  $start.Arguments='-f "'+$imageFile+'" -p '+$Port;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
  $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
  $process=[Diagnostics.Process]::new();$process.StartInfo=$start
  try{
   if(-not $process.Start()){throw 'Could not start the official RAK uploader'}
   $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
   $watch=[Diagnostics.Stopwatch]::StartNew();$report=10
   while(-not $process.WaitForExit(500)){
    Write-Progress -Activity 'USB firmware installation' -Status ('Keep power connected - '+[int]$watch.Elapsed.TotalSeconds+' s')
    if($watch.Elapsed.TotalSeconds -ge $report){Write-Host ('USB: '+[int]$watch.Elapsed.TotalSeconds+' s');$report+=10}
   }
   $log=$stdout.Result+"`n"+$stderr.Result
   Write-Progress -Activity 'USB firmware installation' -Completed
   if($process.ExitCode -ne 0 -or $log -notmatch 'Upgrade Complete'){Write-Verbose $log;throw 'USB programming did not finish. Keep power connected; do not erase or reset the board.'}
  }finally{$process.Dispose()}
  # Official uploader leaves RUI ready; ATZ starts the application without erasing settings.
  try{[void](Invoke-SetupSerial -Port $Port -Command 'ATZ' -Until '(?m)^OK\r?$|LBR_SETUP_V1' -Timeout 5)}catch{
   # Reset may close the reply; the caller must independently verify the new application.
  }
  Start-Sleep -Seconds 3
 }finally{Remove-Item -LiteralPath $imageFile}
}
function Get-FreeSetupSubnet {
 $routes=@(Get-NetRoute -AddressFamily IPv4 | Where-Object {$_.DestinationPrefix -ne '0.0.0.0/0'})
 foreach($third in (230..250)+(120..229)+(20..119)){
  $candidate=[uint64](192*16777216L+168*65536L+$third*256L);$used=$false
  foreach($route in $routes){
   $parts=$route.DestinationPrefix.Split('/');$bits=[int]$parts[1];if($bits -lt 1 -or $bits -gt 32){continue}
   $octets=[Net.IPAddress]::Parse($parts[0]).GetAddressBytes();$address=[uint64]($octets[0]*16777216L+$octets[1]*65536L+$octets[2]*256L+$octets[3])
   $size=[uint64][Math]::Pow(2,32-$bits);$start=[uint64]([Math]::Floor($address/$size)*$size)
   if($candidate -lt $start+$size -and $candidate+256 -gt $start){$used=$true;break}
  }
  if(-not $used){return $third}
 }
 throw 'No conflict-free temporary subnet available. Disconnect the VPN or ask your network administrator.'
}
function Save-SetupJournal([string]$Path,$State){
 # No password, firmware contents or URL token in this recovery record.
 $text=$State|ConvertTo-Json -Depth 4
 $temp=$Path+'.new';[IO.File]::WriteAllText($temp,$text,[Text.UTF8Encoding]::new($false))
 Move-Item -LiteralPath $temp -Destination $Path -Force
}
function Restore-SetupNetwork($Wifi,$State,[int]$TimeoutSeconds=20){
 $failures=[Collections.Generic.List[string]]::new()
 if($State.rule -and $State.rule -match '^LoRaBLE-Install-[0-9a-f]{32}$'){
  try{Get-NetFirewallRule -Name $State.rule -ErrorAction SilentlyContinue | Remove-NetFirewallRule -ErrorAction Stop}catch{$failures.Add($_.Exception.Message)}
 }
 if($State.interface -and $State.profile -match '^LoRaBLE-Install-[0-9a-f]{32}$'){
  $id=[guid]$State.interface
  try{
   $current=@($Wifi.Interfaces() | Where-Object {$_.Id -eq $id})
   # Do not override a deliberate manual switch to an unrelated network.
   if($current.Count -ne 1){throw 'The original WiFi adapter is no longer available.'}
   if($current[0].Profile -eq $State.profile -or $current[0].State -ne 1){
    if($State.original){$Wifi.Connect($id,$State.original)}else{$Wifi.Disconnect($id)}
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while($true){
     $now=@($Wifi.Interfaces() | Where-Object {$_.Id -eq $id})
     if($now.Count -eq 1){
      # A manual choice made while reconnecting also takes precedence.
      if($now[0].State -eq 1 -and $now[0].Profile -and $now[0].Profile -ne $State.profile){break}
      if(-not $State.original -and $now[0].State -eq 4){break}
     }
     if($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds){throw 'WiFi reconnection could not be confirmed. Select your previous network in Windows.'}
     Start-Sleep -Milliseconds 500
    }
   }
  }catch{$failures.Add($_.Exception.Message)}
  try{$Wifi.DeleteTemporary($id,$State.profile)}catch{
   if($_.Exception.InnerException.NativeErrorCode -ne 1168 -and $_.Exception.NativeErrorCode -ne 1168){$failures.Add($_.Exception.Message)}
  }
 }
 if($failures.Count){throw ($failures -join '; ')}
}
function Wait-SetupWifi($Wifi,[guid]$Id,[string]$Profile,[string]$Ssid,[int]$Subnet,[int]$Index){
 $watch=[Diagnostics.Stopwatch]::StartNew()
 while($watch.Elapsed.TotalSeconds -lt 60){
  $current=@($Wifi.Interfaces()|Where-Object {$_.Id -eq $Id})
  if($current.Count -eq 1 -and $current[0].State -eq 1 -and $current[0].Profile -ceq $Profile -and $current[0].Ssid -ceq $Ssid){
   $ip=@(Get-NetIPAddress -InterfaceIndex $Index -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object {$_.IPAddress -like "192.168.$Subnet.*" -and $_.IPAddress -ne "192.168.$Subnet.1" -and $_.AddressState -eq 'Preferred'})
   if($ip.Count -eq 1){return $ip[0].IPAddress}
  }
  Start-Sleep -Milliseconds 500
 }
 throw 'Temporary device WiFi did not connect or obtain an IP address. Check WiFi/location permissions and DHCP; no factory firmware download has started.'
}
