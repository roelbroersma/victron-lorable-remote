param([string]$Firmware=(Join-Path (Split-Path -Parent $PSScriptRoot) ((Get-Content (Join-Path $PSScriptRoot '../firmware/manifest.json') -Raw | ConvertFrom-Json).firmware.path)))
$ErrorActionPreference='Stop';$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'installer/FirstInstall.Core.ps1')
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
foreach($file in Get-ChildItem (Join-Path $root 'installer') -Filter *.ps1){
 $tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors)
 Assert ($errors.Count -eq 0) ('PowerShell parse failure: '+$file.Name)
}
$bundle=[LoRaBLE.Bundle]::Read([IO.Path]::GetFullPath($Firmware))
$helper=Read-SetupHelper (Join-Path $root 'installer/bootstrap-image.json')
Assert ($helper.Length -gt 100000) 'Missing compiled setup helper'
$temp=[IO.Path]::GetTempFileName()
try{
 $bad=Get-Content (Join-Path $root 'installer/bootstrap-image.json') -Raw|ConvertFrom-Json
 $bad.sha256='0'*64;[IO.File]::WriteAllText($temp,($bad|ConvertTo-Json))
 $rejected=$false;try{Read-SetupHelper $temp|Out-Null}catch{$rejected=$true}
 Assert $rejected 'Corrupt helper accepted'
}finally{Remove-Item -LiteralPath $temp}
$secret=New-SetupSecret
Assert ($secret -cmatch '^[0-9a-f]{32}$') 'Invalid random session token'
Assert ((New-SetupSecret 12).Length -eq 24) 'Invalid temporary password length'
$xml=[xml][LoRaBLE.Install.Wifi]::ProfileXml('LoRaBLE-Install-unit','SSID with spaces','password&<"value')
Assert ($xml.WLANProfile.MSM.security.sharedKey.keyMaterial -ceq 'password&<"value') 'WiFi password escaping failed'
Assert ($xml.WLANProfile.connectionMode -ceq 'manual') 'Temporary WiFi must not autoconnect in future'
Assert ($xml.WLANProfile.MSM.security.authEncryption.authentication -ceq 'WPA2PSK') 'Temporary WiFi is not protected'

function Get-LocalResponse([string]$Url,[string]$Method='GET'){
 $request=[Net.HttpWebRequest]::CreateHttp($Url);$request.Method=$Method;$request.Proxy=$null;$request.Timeout=5000
 $response=$null;$body=[IO.MemoryStream]::new()
 try{
  try{$response=$request.GetResponse()}catch [Net.WebException]{$response=$_.Exception.Response;if(-not $response){throw}}
  $response.GetResponseStream().CopyTo($body)
  return @{Status=[int]$response.StatusCode;Bytes=$body.ToArray()}
 }finally{if($response){$response.Dispose()};$body.Dispose()}
}
$server=[LoRaBLE.Install.FirmwareServer]::new('127.0.0.1','127.0.0.1',0,$secret,$bundle.Connectivity)
try{
 $base='http://127.0.0.1:'+$server.Port
 $health=Get-LocalResponse "$base/$secret/health"
 Assert ($health.Status -eq 200 -and [Text.Encoding]::ASCII.GetString($health.Bytes) -ceq "LORABLE_OTA_READY:$secret") 'Wrong health challenge'
 Assert ((Get-LocalResponse "$base/wrong/firmware").Status -eq 404) 'Wrong token accepted'
 Assert ((Get-LocalResponse "$base/$secret/../firmware").Status -eq 404) 'Traversal accepted'
 Assert ((Get-LocalResponse "$base/$secret/firmware" 'POST').Status -eq 405) 'HTTP writes accepted'
 $result=Get-LocalResponse "$base/$secret/firmware"
 Assert ($result.Status -eq 200 -and $result.Bytes.Length -eq $bundle.Connectivity.Length) 'Incomplete firmware transfer'
 $sha=[Security.Cryptography.SHA256]::Create()
 try{Assert ([Convert]::ToBase64String($sha.ComputeHash($result.Bytes)) -ceq [Convert]::ToBase64String($sha.ComputeHash($bundle.Connectivity))) 'HTTP body differs from verified bundle'}finally{$sha.Dispose()}
 Assert ($server.HealthRequests -eq 1 -and $server.CompletedRequests -eq 1) 'Wrong server request accounting'
 Assert ($server.BytesSent -eq $bundle.Connectivity.Length) 'Wrong transfer progress'
}finally{$server.Dispose()}
$server=[LoRaBLE.Install.FirmwareServer]::new('127.0.0.1','127.0.0.2',0,$secret,$bundle.Connectivity)
try{
 $refused=$false;try{Get-LocalResponse ('http://127.0.0.1:'+$server.Port+"/$secret/firmware")|Out-Null}catch{$refused=$true}
 Assert $refused 'Foreign source IP accepted'
 Assert ($server.CompletedRequests -eq 0) 'Foreign peer received firmware'
}finally{$server.Dispose()}

# Mock ONLY cleanup calls; never touch real WiFi/firewall state in these tests.
$script:cleanupCalls=[Collections.Generic.List[string]]::new();$script:failFirewall=$false
function Get-NetFirewallRule {param($Name,$ErrorAction) if($script:failFirewall){throw 'blocked by policy'};[pscustomobject]@{Name=$Name}}
function Remove-NetFirewallRule {param([Parameter(ValueFromPipeline)]$InputObject) process{$script:cleanupCalls.Add('firewall')}}
$id=[guid]::NewGuid();$profile='LoRaBLE-Install-'+('a'*32)
$fake=[pscustomobject]@{Id=$id;Profile=$profile;State=1}
$fake|Add-Member ScriptMethod Interfaces {[pscustomobject]@{Id=$this.Id;Profile=$this.Profile;State=$this.State}}
$fake|Add-Member ScriptMethod Connect {param($Id,$Name)$script:cleanupCalls.Add('connect:'+$Name);$this.Profile=$Name;$this.State=1}
$fake|Add-Member ScriptMethod Disconnect {param($Id)$script:cleanupCalls.Add('disconnect');$this.Profile='';$this.State=4}
$fake|Add-Member ScriptMethod DeleteTemporary {param($Id,$Name)$script:cleanupCalls.Add('delete:'+$Name)}
$state=@{interface=$id.ToString();profile=$profile;original='Original WiFi';rule='LoRaBLE-Install-'+('b'*32)}
Restore-SetupNetwork $fake $state
Assert ($script:cleanupCalls.Contains('connect:Original WiFi')) 'Original network was not restored'
Assert ($script:cleanupCalls.Contains('delete:'+$profile)) 'Temporary profile was not deleted'
$script:cleanupCalls.Clear();$fake.Profile='User selected another WiFi'
Restore-SetupNetwork $fake $state
Assert (-not $script:cleanupCalls.Contains('connect:Original WiFi')) 'Manual network choice was overwritten'
$script:cleanupCalls.Clear();$script:failFirewall=$true;$fake.Profile=$profile
$failed=$false;try{Restore-SetupNetwork $fake $state}catch{$failed=$true}
Assert $failed 'Cleanup failure was hidden'
Assert ($script:cleanupCalls.Contains('connect:Original WiFi') -and $script:cleanupCalls.Contains('delete:'+$profile)) 'Firewall failure prevented WiFi cleanup'
$script:failFirewall=$false;$script:cleanupCalls.Clear();$fake.Profile=$profile
$fake|Add-Member -Force ScriptMethod Connect {param($Id,$Name)$script:cleanupCalls.Add('connect:'+$Name)}
$failed=$false;try{Restore-SetupNetwork $fake $state -TimeoutSeconds 0}catch{$failed=$true}
Assert $failed 'An unconfirmed WiFi reconnect was reported as successful'
Assert ($script:cleanupCalls.Contains('delete:'+$profile)) 'Reconnect failure prevented temporary profile cleanup'
$script:cleanupCalls.Clear();$fake.Profile=$profile;$state.original=''
Restore-SetupNetwork $fake $state
Assert ($script:cleanupCalls.Contains('disconnect')) 'Initially disconnected WiFi was not restored'

# Ensure setup secrets cannot be serialized accidentally into the recovery file.
$main=Get-Content (Join-Path $root 'installer/First-Install.ps1') -Raw
Assert ($main -notmatch 'state\.(password|secret|token)\s*=') 'Secret stored in recovery journal'
Assert ($main.Contains("phase='DownloadRequested';Save-SetupJournal")) 'Missing pre-download recovery checkpoint'
Assert ($main.Contains('finishing the board first:')) 'Network cleanup failure must not interrupt final board programming'
Assert ($main.Contains('ATC+SETUP=1:{0}:{1}:{2}') -and $main.Contains('ATC+SETUP=2:{0}:{1}')) 'RUI parameters require colon separators, unlike ESP-AT'
Assert (-not $main.Contains('erase_flash')) 'Destructive erase command in installer'
Write-Host 'PASS: first-install parsers, helper integrity, protected profile, restricted HTTP transfer, cleanup and recovery guards.'
