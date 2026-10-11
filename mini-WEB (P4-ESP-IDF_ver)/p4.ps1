param(
    [ValidateSet('Build', 'Flash', 'Monitor')][string]$Action = 'Build',
    [string]$Port,
    [string]$IdfPath = 'C:\esp\v5.5.5\esp-idf'
)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:IDF_CCACHE_ENABLE = '0'
$env:IDF_TARGET = 'esp32p4'
if ($Action -ne 'Build' -and -not $Port) { throw '-Port COM번호 is required' }
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    $idfExport = Join-Path $IdfPath 'export.ps1'
    if (-not (Test-Path -LiteralPath $idfExport)) { throw 'Open an ESP-IDF 5.5.5 terminal or specify -IdfPath' }
    # Match the complete local installation used by the camera example.
    $toolsRoot = Join-Path $env:USERPROFILE '.espressif'
    $pythonEnv = Join-Path $toolsRoot 'python_env\idf5.5_py3.10_env'
    if (Test-Path -LiteralPath $pythonEnv) {
        $env:IDF_TOOLS_PATH = $toolsRoot
        $env:IDF_PYTHON_ENV_PATH = $pythonEnv
    }
    . $idfExport
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF activation failed' }
}
$hashAlgorithm = [System.Security.Cryptography.SHA256]::Create()
try { $projectHash = ([BitConverter]::ToString($hashAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($PSScriptRoot)))).Replace('-', '').Substring(0, 12) }
finally { $hashAlgorithm.Dispose() }
$stage = Join-Path $env:TEMP "mini-web-p4-$projectHash"
if ($stage -match '[^\x00-\x7F]') { throw 'TEMP must be an ASCII path for this toolchain' }
if ($Action -eq 'Build') {
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    robocopy.exe (Join-Path $PSScriptRoot 'main') (Join-Path $stage 'main') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw 'Source sync failed' }
    foreach ($file in @('CMakeLists.txt', 'sdkconfig.defaults', 'partitions_16mb.csv')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $stage -Force
    }
    # Retain the user's configuration if they created sdkconfig in the source project.
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'sdkconfig')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'sdkconfig') -Destination $stage -Force
    }
} elseif (-not (Test-Path -LiteralPath (Join-Path $stage 'build\flasher_args.json'))) {
    throw 'Run .\p4.ps1 -Action Build first for this project path'
}
Push-Location -LiteralPath $stage
try {
    if ($Action -eq 'Build') {
        idf.py --no-ccache -B build reconfigure build
        if ($LASTEXITCODE -ne 0) { throw 'P4 build failed' }
        $output = Join-Path $PSScriptRoot 'firmware-output'
        New-Item -ItemType Directory -Path $output -Force | Out-Null
        foreach ($relative in @('mini-webserver-p4.bin', 'bootloader\bootloader.bin', 'partition_table\partition-table.bin', 'ota_data_initial.bin', 'flasher_args.json')) {
            $artifact = Join-Path $stage "build\$relative"
            if (-not (Test-Path -LiteralPath $artifact)) { throw "Missing build output: $relative" }
            Copy-Item -LiteralPath $artifact -Destination $output -Force
        }
        Write-Host "Firmware copied to $output"
    } elseif ($Action -eq 'Flash') {
        idf.py --no-ccache -B build -p $Port flash monitor
        if ($LASTEXITCODE -ne 0) { throw 'P4 flash/monitor failed' }
    } else {
        idf.py --no-ccache -B build -p $Port monitor
        if ($LASTEXITCODE -ne 0) { throw 'P4 monitor failed' }
    }
} finally { Pop-Location }
