param(
    [ValidateSet('Build', 'Flash', 'Monitor', 'Menuconfig')][string]$Action = 'Build',
    [ValidateSet('V1', 'V2')][string]$Panel = 'V2',
    [ValidateSet('v1', 'v3')][string]$Silicon = 'v1',
    [string]$Port,
    [string]$IdfPath = 'C:\esp\v5.5.5\esp-idf'
)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:IDF_CCACHE_ENABLE = '0'
$env:IDF_TARGET = 'esp32p4'
$env:IDF_PY_BUILD_JOBS = '2'
if ($Action -in @('Flash', 'Monitor') -and -not $Port) { throw '-Port COMx is required' }
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    $idfExport = Join-Path $IdfPath 'export.ps1'
    if (-not (Test-Path -LiteralPath $idfExport)) { throw 'Open an ESP-IDF 5.5.5 terminal or specify -IdfPath' }
    $eimTools = 'C:\Espressif\tools'
    $eimPython = Join-Path $eimTools 'python\v5.5.5\venv'
    $legacyTools = Join-Path $env:USERPROFILE '.espressif'
    $legacyPython = Join-Path $legacyTools 'python_env\idf5.5_py3.10_env'
    if (Test-Path -LiteralPath $eimPython) {
        $env:IDF_TOOLS_PATH = $eimTools
        $env:IDF_PYTHON_ENV_PATH = $eimPython
    } elseif (Test-Path -LiteralPath $legacyPython) {
        $env:IDF_TOOLS_PATH = $legacyTools
        $env:IDF_PYTHON_ENV_PATH = $legacyPython
    }
    . $idfExport
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF activation failed' }
}
$statePath = Join-Path $PSScriptRoot 'results\last-build.json'
if ($Action -in @('Build', 'Menuconfig')) {
    # Every configure/build gets a new ASCII directory; never delete old builds.
    $stage = Join-Path $env:TEMP ('mini-web-m4-' + [guid]::NewGuid().ToString('N'))
    if ($stage -match '[^\x00-\x7F]') { throw 'TEMP must be an ASCII path for this toolchain' }
    python -X utf8 (Join-Path $PSScriptRoot 'tools\prepare_build.py') $stage
    if ($LASTEXITCODE -ne 0) { throw 'M4 staging failed' }
    $defaults = 'sdkconfig.defaults'
    if ($Panel -eq 'V1') { $defaults += ';sdkconfig.panel-v1.defaults' }
    if ($Silicon -eq 'v3') { $defaults += ';sdkconfig.rev3.defaults' }
    # A saved menuconfig has precedence over defaults. Refuse contradictory
    # profile flags instead of silently compiling for a different board.
    $savedConfig = Join-Path $stage 'sdkconfig'
    if (Test-Path -LiteralPath $savedConfig) {
        $saved = Get-Content -LiteralPath $savedConfig -Raw
        $savedV2 = $saved -match '(?m)^CONFIG_M4_PANEL_V2=y\r?$'
        $savedOld = $saved -match '(?m)^CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y\r?$'
        if ($savedV2 -ne ($Panel -eq 'V2') -or $savedOld -ne ($Silicon -eq 'v1')) {
            throw 'Saved sdkconfig disagrees with -Panel/-Silicon. Select matching flags or move sdkconfig aside first.'
        }
    }
} else {
    if (-not (Test-Path -LiteralPath $statePath)) { throw 'Complete an M4 Build first' }
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $stage = $state.stage
    if (-not (Test-Path -LiteralPath (Join-Path $stage 'build\flasher_args.json'))) { throw 'Last successful M4 build is unavailable' }
    Write-Host "Using last successful M4 build: panel $($state.panel), silicon $($state.silicon)"
}
Push-Location -LiteralPath $stage
try {
    if ($Action -in @('Build', 'Menuconfig')) {
        $idfAction = if ($Action -eq 'Build') { 'build' } else { 'menuconfig' }
        idf.py --no-ccache -B build "-DSDKCONFIG_DEFAULTS=$defaults" $idfAction
        if ($LASTEXITCODE -ne 0) { throw "M4 $idfAction failed; the previous successful build remains selected" }
        Copy-Item -LiteralPath (Join-Path $stage 'sdkconfig') -Destination (Join-Path $PSScriptRoot 'sdkconfig') -Force
        if ($Action -eq 'Build') {
            $output = Join-Path $PSScriptRoot 'firmware-output'
            New-Item -ItemType Directory -Path $output -Force | Out-Null
            foreach ($name in @('mini-webserver-m4.bin', 'bootloader\bootloader.bin', 'partition_table\partition-table.bin', 'ota_data_initial.bin', 'flasher_args.json')) {
                $artifact = Join-Path $stage "build\$name"
                if (-not (Test-Path -LiteralPath $artifact)) { throw "Missing M4 artifact: $name" }
                # Preserve subdirectories named by flasher_args.json.
                $target = Join-Path $output $name
                New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
                Copy-Item -LiteralPath $artifact -Destination $target -Force
            }
            New-Item -ItemType Directory -Path (Split-Path $statePath) -Force | Out-Null
            @{stage=$stage; panel=$Panel; silicon=$Silicon} | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
            Write-Host "M4 firmware copied to $output"
        }
    } elseif ($Action -eq 'Flash') {
        idf.py --no-ccache -B build -p $Port flash
        if ($LASTEXITCODE -ne 0) { throw 'M4 flash failed' }
    } else {
        idf.py --no-ccache -B build -p $Port monitor
        if ($LASTEXITCODE -ne 0) { throw 'M4 monitor failed' }
    }
} finally { Pop-Location }
