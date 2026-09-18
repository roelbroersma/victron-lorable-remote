param([string]$Firmware=(Join-Path $PSScriptRoot '../firmware/LoRaBLE-Remote-4.11.0.bin'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../installer/Bundle.ps1')
$good=[LoRaBLE.Bundle]::Read([IO.Path]::GetFullPath($Firmware))
if($good.Version -ne '4.11.0'){throw 'Unexpected reference version'}
$payload=[Text.Encoding]::ASCII.GetBytes('123456789')
if([LoRaBLE.Bundle]::Crc($payload,0,9) -ne [Convert]::ToUInt32('cbf43926',16)){throw 'CRC32 known-answer mismatch'}
$packet=[LoRaBLE.Bundle]::Packet($payload,0,9,3)
$usbSource=Get-Content (Join-Path $PSScriptRoot '../installer/Usb-Portal.ps1') -Raw
if($usbSource -notmatch '\$blockSize=if\(\$Transport -eq 2\)\{(\d+)\}') {throw 'USB packet size is not statically bounded'}
if([int]$Matches[1]+16 -ge 512){throw 'USB packet crosses the RUI software receive ring'}
if($usbSource -notmatch '\$wireBurst=(\d+);\$wireGapMs=(\d+)'){throw 'USB wire bursts are not bounded'}
$burst=[int]$Matches[1];$gap=[int]$Matches[2]
if($burst -ge 128 -or $gap -lt [Math]::Ceiling($burst*10000/115200)+2){throw 'USB bursts need a DMA idle gap after transmission'}
if([BitConverter]::ToUInt32($packet,0) -ne 0x3255424c -or [BitConverter]::ToUInt32($packet,4) -ne 3 -or [BitConverter]::ToUInt32($packet,8) -ne 9){throw 'USB frame header mismatch'}
$joined=[byte[]]::new(21);[Array]::Copy($packet,0,$joined,0,12);[Array]::Copy($packet,16,$joined,12,9)
if([LoRaBLE.Bundle]::Crc($joined,0,21) -ne [BitConverter]::ToUInt32($packet,12)){throw 'USB frame CRC does not include header and body'}
$ack=[byte[]]::new(16);[Array]::Copy([BitConverter]::GetBytes([uint32]0x3241424c),0,$ack,0,4);[Array]::Copy([BitConverter]::GetBytes([uint32]3),0,$ack,4,4)
[Array]::Copy([BitConverter]::GetBytes([LoRaBLE.Bundle]::Crc($ack,0,12)),0,$ack,12,4)
if([LoRaBLE.Bundle]::Ack($ack,3) -ne 0 -or [LoRaBLE.Bundle]::Ack($ack,4) -ne -1){throw 'USB acknowledgement sequence check failed'}
$ack[8]=1;if([LoRaBLE.Bundle]::Ack($ack,3) -ne -1){throw 'Corrupt USB acknowledgement accepted'}
$tmp=[IO.Path]::GetTempFileName()
try{
 foreach($offset in @(0,8,12,16,47,48,52,56,60,64,96,128,164,252,256,260,500,($good.Bytes.Length-1))){
  $bad=[byte[]]$good.Bytes.Clone();$bad[$offset]=$bad[$offset] -bxor 1
  [IO.File]::WriteAllBytes($tmp,$bad)
  $rejected=$false;try{[void][LoRaBLE.Bundle]::Read($tmp)}catch{$rejected=$true}
  if(-not $rejected){throw "Corruption at $offset was accepted"}
 }
 Write-Host 'PowerShell bundle validation passed: valid image plus 18 corruptions rejected.'
}finally{Remove-Item -LiteralPath $tmp}
