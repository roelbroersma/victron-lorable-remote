# One bounded HTTP exchange through the board's own USB port. No WiFi required.
. (Join-Path $PSScriptRoot 'Bundle.ps1')
function Invoke-LoRaBLEUsbHttp {
 param([Parameter(Mandatory)][string]$Port,[ValidateSet('GET','POST')][string]$Method='GET',
       [Parameter(Mandatory)][string]$Route,[byte[]]$Body=@(),[string]$Token='',[switch]$DryRun,[string]$DiagnosticFile,
       [ValidateSet(1,2)][int]$Transport=2)
 if($Port -notmatch '^COM[1-9][0-9]*$' -or $Route -notmatch '^/(session|update|ota)$'){throw 'Invalid local USB request'}
 $serial=[IO.Ports.SerialPort]::new($Port,115200,'None',8,'One')
 $serial.DtrEnable=$false;$serial.RtsEnable=$false;$serial.ReadTimeout=500;$serial.WriteTimeout=3000
 $serial.ReadBufferSize=65536
 $stream=[IO.MemoryStream]::new()
 try{
  $serial.Open();Start-Sleep -Milliseconds 200
  # Finish an incomplete console line left by an interrupted previous client.
  $serial.Write("`r`n");Start-Sleep -Milliseconds 100;$serial.DiscardInBuffer();$serial.Write("ATC+USB=$Transport`r`n")
  $readyPattern=if($Transport -eq 2){'LBR_USB_READY2\r*\n'}else{'LBR_USB_READY\r*\n'}
  $watch=[Diagnostics.Stopwatch]::StartNew();$answer=''
  while($watch.Elapsed.TotalSeconds -lt 22){
   $answer+=$serial.ReadExisting()
   if($answer -match $readyPattern){
    if($answer.Contains('~LBR-END~')){throw 'USB connection closed before transfer. No firmware data sent.'}
    break
   }
   foreach($failure in @('LBR_USB_UNAVAILABLE','AT_PARAM_ERROR','AT_COMMAND_NOT_FOUND')){if($answer.Contains($failure)){
    $detail=$failure;if($answer -match 'LBR_USB_UNAVAILABLE link=[01] portal=[01] busy=[01] queued=[0-9]+'){$detail=$Matches[0]}
    throw "USB updater is not ready ($detail). Wait for startup or follow the migration instructions."
   }}
   Start-Sleep -Milliseconds 30
  }
  if($answer -notmatch $readyPattern){throw "USB updater did not answer (ready marker present=$($answer.Contains('LBR_USB_READY'))). No firmware data sent."}
  $headers="$Method $Route HTTP/1.1`r`nHost: 127.0.0.1`r`nConnection: close`r`nContent-Length: $($Body.Length)`r`n"
  if($Token){if($Token -notmatch '^[0-9A-Fa-f]{32}$'){throw 'Invalid update session'};$headers+="X-LoRaBLE: $Token`r`n"}
  if($Method -eq 'POST'){$headers+="Content-Type: application/octet-stream`r`n"}
  if($DryRun){$headers+="X-LoRaBLE-Dry-Run: 1`r`n"}
  $headerBytes=[Text.Encoding]::ASCII.GetBytes($headers+"`r`n")
  # Pace below the application UART ring capacity. RUI has no hardware flow control.
  $responseStarted=$false;$sentBody=0;$lastPercent=-1;$nextReport=5;[uint32]$sequence=0
  $uploadWatch=[Diagnostics.Stopwatch]::StartNew()
  # RUI 4.2.4 has a 128-byte hardware DMA buffer BEFORE its 512-byte software
  # ring. Keep each wire burst below128, with an idle gap to restart DMA. A
  # complete checked packet still fits the software ring; ACK gates the next.
  $blockSize=if($Transport -eq 2){256}else{64}
  $wireBurst=96;$wireGapMs=12
  :upload foreach($part in @($headerBytes,$Body)){
   for($offset=0;$offset -lt $part.Length;$offset+=$blockSize){
    $count=[Math]::Min($blockSize,$part.Length-$offset)
    if($Transport -eq 2){
     $packet=[LoRaBLE.Bundle]::Packet($part,$offset,$count,$sequence);$accepted=$false
     for($retry=0;$retry -lt 4 -and -not $accepted;$retry++){
      # 96 bytes take 8.34ms at115200/8N1. Waiting12ms after enqueue guarantees
      # an idle interval before the next burst, not just a pause inside its TX.
      for($wire=0;$wire -lt $packet.Length;$wire+=$wireBurst){
       $wireCount=[Math]::Min($wireBurst,$packet.Length-$wire)
       $serial.Write($packet,$wire,$wireCount)
       if($wire+$wireCount -lt $packet.Length){Start-Sleep -Milliseconds $wireGapMs}
      }
      $ack=[byte[]]::new(16);$used=0;$ackWatch=[Diagnostics.Stopwatch]::StartNew()
      while($used -lt 16 -and $ackWatch.Elapsed.TotalSeconds -lt 3){
       if($serial.BytesToRead){$used+=$serial.Read($ack,$used,[Math]::Min(16-$used,$serial.BytesToRead))}
       if($used -ge 4 -and [Text.Encoding]::ASCII.GetString($ack,0,4) -ceq 'HTTP'){
        $stream.Write($ack,0,$used);$responseStarted=$true;break upload
       }
       Start-Sleep -Milliseconds 2
      }
      $accepted=$used -eq 16 -and [LoRaBLE.Bundle]::Ack($ack,$sequence) -eq 0
      if(-not $accepted){Write-Verbose ("USB retry block={0} bytes={1} tag={2} code={3}" -f $sequence,$used,[BitConverter]::ToString($ack,0,4),[LoRaBLE.Bundle]::Ack($ack,$sequence))}
     }
     if(-not $accepted){throw "USB block $sequence could not be verified. Keep power connected and check device state before retrying."}
     $sequence++
    }else{$serial.Write($part,$offset,$count);Start-Sleep -Milliseconds 10}
    if($part.Length -gt 4096){
     $sentBody=$offset+$count;$percent=[int](100*$sentBody/$part.Length)
     if($percent -ne $lastPercent){Write-Progress -Activity 'LoRaBLE firmware upload' -PercentComplete $percent;$lastPercent=$percent}
     if($percent -ge $nextReport){
      $rate=[int]($sentBody/[Math]::Max(1,$uploadWatch.Elapsed.TotalSeconds))
      Write-Host "USB upload: $percent% ($rate bytes/s)";$nextReport+=5
     }
    }
    # An early HTTP error ends the tunnel: never keep pouring binary firmware
    # into the board's restored AT command parser after the receiver rejected it.
    if($serial.BytesToRead){
     $early=[byte[]]::new($serial.BytesToRead);$n=$serial.Read($early,0,$early.Length);$stream.Write($early,0,$n)
     $responseStarted=$true;break upload
    }
   }
  }
  Write-Progress -Activity 'LoRaBLE firmware upload' -Completed
  $watch.Restart();$buffer=[byte[]]::new(2048);$headerEnd=-1;$contentLength=-1;$status=0
  while($watch.Elapsed.TotalSeconds -lt 30){
   if($serial.BytesToRead){
    $count=$serial.Read($buffer,0,[Math]::Min($buffer.Length,$serial.BytesToRead));$stream.Write($buffer,0,$count)
    if($stream.Length -gt 65536){throw 'Oversized USB response'}
   }
   if($stream.Length){
    $data=$stream.ToArray()
    if($headerEnd -lt 0){
     $text=[Text.Encoding]::ASCII.GetString($data);$headerEnd=$text.IndexOf("`r`n`r`n")
     if($headerEnd -ge 0){
      $head=$text.Substring(0,$headerEnd)
      if($head -notmatch '^HTTP/1\.[01] (\d{3})'){throw 'Invalid USB HTTP response'};$status=[int]$Matches[1]
      if($head -notmatch '(?im)^Content-Length:\s*(\d+)\s*$'){throw 'Missing USB response length'};$contentLength=[int]$Matches[1]
     }
    }
    if($headerEnd -ge 0 -and $contentLength -ge 0 -and $data.Length -ge $headerEnd+4+$contentLength+9 -and
       [Text.Encoding]::ASCII.GetString($data,$headerEnd+4+$contentLength,9) -ceq '~LBR-END~'){
     $json=[Text.Encoding]::UTF8.GetString($data,$headerEnd+4,$contentLength)|ConvertFrom-Json
     Start-Sleep -Milliseconds 500
     if($status -ne 200){throw "Update request refused (HTTP $status; $($json.code); body bytes sent=$sentBody)."}
     return $json
    }
   }
   Start-Sleep -Milliseconds 20
  }
  throw "USB response timed out (bytes=$($stream.Length), header=$headerEnd, content=$contentLength, body sent=$sentBody, verified blocks=$sequence). Keep power connected and check device state before retrying."
 }finally{
  if($DiagnosticFile){[IO.File]::WriteAllBytes([IO.Path]::GetFullPath($DiagnosticFile),$stream.ToArray())}
  if($serial.IsOpen){$serial.Close()};$serial.Dispose();$stream.Dispose()
 }
}
