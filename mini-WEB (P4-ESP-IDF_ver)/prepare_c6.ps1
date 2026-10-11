# Prepare the matching, unmodified official ESP-Hosted C6 project. Does not build/flash.
param([string]$HostedComponentPath)
$ErrorActionPreference = 'Stop'
$algorithm = [Security.Cryptography.SHA256]::Create()
try { $projectHash = ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($PSScriptRoot)))).Replace('-', '').Substring(0, 12) }
finally { $algorithm.Dispose() }
if (-not $HostedComponentPath) {
    $HostedComponentPath = Join-Path $env:TEMP "mini-web-p4-$projectHash\managed_components\espressif__esp_hosted"
}
$manifest = Join-Path $HostedComponentPath 'idf_component.yml'
if (-not (Test-Path -LiteralPath $manifest)) { throw 'Run the P4 Build first, or pass -HostedComponentPath to its resolved esp_hosted 1.4.1 component.' }
if ((Get-Content -LiteralPath $manifest -Raw) -notmatch 'version:\s*[''"'']?1\.4\.1[''"'']?\s') { throw 'This P4 stack requires esp_hosted 1.4.1 and its matching C6 firmware.' }
$source = Join-Path $HostedComponentPath 'examples\slave'
if (-not (Test-Path -LiteralPath (Join-Path $source 'main\coprocessor_fw_version.txt'))) { throw 'Matching official slave example is missing' }
$destination = Join-Path $env:TEMP "mini-web-p4-c6-$projectHash"
if ($destination -match '[^\x00-\x7F]') { throw 'TEMP must be an ASCII path' }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
robocopy.exe $source $destination /E /XD build managed_components .git /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'C6 example sync failed' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'c6_sdkconfig.defaults') -Destination (Join-Path $destination 'mini_web_c6.defaults') -Force
Write-Host "C6 project: $destination"
Write-Host 'In an ESP-IDF 5.5.5 terminal run:'
Write-Host "Set-Location -LiteralPath '$destination'"
Write-Host '$env:IDF_TARGET = "esp32c6"'
Write-Host 'idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;mini_web_c6.defaults" set-target esp32c6'
Write-Host 'idf.py build'
Write-Host 'idf.py -p <C6-UART-COM> flash monitor'
Write-Host 'Return to the P4 project and set IDF_TARGET=esp32p4 before building P4 again.'
