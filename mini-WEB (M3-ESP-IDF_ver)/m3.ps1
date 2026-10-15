param(
    [ValidateSet('Build', 'Flash', 'Monitor')][string]$Action = 'Build',
    [string]$Port,
    [string]$IdfPath = 'C:\esp\v5.5.5\esp-idf'
)
$ErrorActionPreference = 'Stop'
# Keep the window open on failure when the script was started by double-click.
trap {
    Write-Host "M3 script failed: $_" -ForegroundColor Red
    Read-Host 'Press Enter to close'
    exit 1
}
$env:PYTHONUTF8 = '1'
$env:IDF_CCACHE_ENABLE = '0'
$env:IDF_TARGET = 'esp32p4'
if ($Action -ne 'Build' -and -not $Port) { throw '-Port COMx is required (example: -Port COM5)' }
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    $idfExport = Join-Path $IdfPath 'export.ps1'
    if (-not (Test-Path -LiteralPath $idfExport)) { throw 'Open an ESP-IDF 5.5.5 terminal or specify -IdfPath' }
    # ESP-IDF Installation Manager layout (C:\Espressif\tools), then the
    # older idf_tools layout under the user profile.
    $eimTools = 'C:\Espressif\tools'
    $eimPython = Join-Path $eimTools 'python\v5.5.5\venv'
    $toolsRoot = Join-Path $env:USERPROFILE '.espressif'
    $pythonEnv = Join-Path $toolsRoot 'python_env\idf5.5_py3.10_env'
    if (Test-Path -LiteralPath $eimPython) {
        $env:IDF_TOOLS_PATH = $eimTools
        $env:IDF_PYTHON_ENV_PATH = $eimPython
    } elseif (Test-Path -LiteralPath $pythonEnv) {
        $env:IDF_TOOLS_PATH = $toolsRoot
        $env:IDF_PYTHON_ENV_PATH = $pythonEnv
    }
    . $idfExport
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF activation failed' }
}
$hashAlgorithm = [System.Security.Cryptography.SHA256]::Create()
try { $projectHash = ([BitConverter]::ToString($hashAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($PSScriptRoot)))).Replace('-', '').Substring(0, 12) }
finally { $hashAlgorithm.Dispose() }
$stage = Join-Path $env:TEMP "mini-web-m3-$projectHash"
if ($stage -match '[^\x00-\x7F]') { throw 'TEMP must be an ASCII path for this toolchain' }
if ($Action -eq 'Build') {
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    robocopy.exe (Join-Path $PSScriptRoot 'main') (Join-Path $stage 'main') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw 'Source sync failed' }
    robocopy.exe (Join-Path $PSScriptRoot 'cmake') (Join-Path $stage 'cmake') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw 'CMake compatibility source sync failed' }
    robocopy.exe (Join-Path $PSScriptRoot 'components') (Join-Path $stage 'components') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw 'Local TTS component source sync failed' }
    robocopy.exe (Join-Path $PSScriptRoot 'third_party') (Join-Path $stage 'third_party') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw 'Third-party source sync failed' }
    $assetTools = Join-Path $stage 'tools'
    New-Item -ItemType Directory -Path $assetTools -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tools\generate_web_gzip.py') -Destination $assetTools -Force
    foreach ($file in @('CMakeLists.txt', 'sdkconfig.defaults', 'partitions_16mb.csv')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $stage -Force
    }
    # Retain the user's configuration if they created sdkconfig in the source project.
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'sdkconfig')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'sdkconfig') -Destination $stage -Force
    }
} elseif (-not (Test-Path -LiteralPath (Join-Path $stage 'build\flasher_args.json'))) {
    throw 'Run .\m3.ps1 -Action Build first for this project path'
}
Push-Location -LiteralPath $stage
try {
    if ($Action -eq 'Build') {
        idf.py --no-ccache -B build reconfigure build
        if ($LASTEXITCODE -ne 0) { throw 'M3 build failed' }
        $output = Join-Path $PSScriptRoot 'firmware-output'
        New-Item -ItemType Directory -Path $output -Force | Out-Null
        foreach ($relative in @('mini-webserver-m3.bin', 'bootloader\bootloader.bin', 'partition_table\partition-table.bin', 'ota_data_initial.bin', 'flasher_args.json')) {
            $artifact = Join-Path $stage "build\$relative"
            if (-not (Test-Path -LiteralPath $artifact)) { throw "Missing build output: $relative" }
            Copy-Item -LiteralPath $artifact -Destination $output -Force
        }
        Write-Host "Firmware copied to $output"
    } elseif ($Action -eq 'Flash') {
        idf.py --no-ccache -B build -p $Port flash monitor
        if ($LASTEXITCODE -ne 0) { throw 'M3 flash/monitor failed' }
    } else {
        idf.py --no-ccache -B build -p $Port monitor
        if ($LASTEXITCODE -ne 0) { throw 'M3 monitor failed' }
    }
} finally { Pop-Location }
