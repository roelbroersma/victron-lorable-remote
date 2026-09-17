param(
 [string]$BuildDirectory='build_native49',
 [string]$IdfRoot=$env:IDF_PATH,
 [string]$IdfToolsPath=$env:IDF_TOOLS_PATH
)
$ErrorActionPreference='Stop'
if(-not $IdfRoot){$IdfRoot=Join-Path $env:USERPROFILE '.cache/esp-idf-v5.5.5'}
if(-not (Test-Path -LiteralPath (Join-Path $IdfRoot 'export.ps1'))){throw 'Install ESP-IDF 5.5.5 and pass -IdfRoot or activate its environment.'}
if(-not $IdfToolsPath){
 $candidateTools=Join-Path $env:USERPROFILE '.cache/espressif-v5.5.5-tools'
 if(Test-Path -LiteralPath $candidateTools){$IdfToolsPath=$candidateTools}
}
if($IdfToolsPath){$env:IDF_TOOLS_PATH=$IdfToolsPath}
$optionalRuntime=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies'
if(Test-Path -LiteralPath $optionalRuntime){$env:PATH="$optionalRuntime/python;$optionalRuntime/native/git/cmd;"+$env:PATH}
$env:CCACHE_DISABLE='1'
Push-Location $PSScriptRoot
try {
 . (Join-Path $IdfRoot 'export.ps1')
 & python "$env:IDF_PATH/tools/idf.py" --no-ccache -B $BuildDirectory -D "SDKCONFIG=$BuildDirectory/sdkconfig" build
 if($LASTEXITCODE -ne 0){throw 'ESP build failed'}
 Write-Host 'Application build only. DO NOT flash generated bootloader/table. Pack the app for the retained stock layout.'
} finally {Pop-Location}
