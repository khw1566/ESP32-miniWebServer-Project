$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'results\launcher-tests'
New-Item -ItemType Directory -Force $output | Out-Null
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $csc /nologo /codepage:65001 /target:exe /main:LauncherTests "/out:$output\LauncherTests.exe" /reference:System.Web.Extensions.dll /reference:System.Windows.Forms.dll /reference:System.Drawing.dll "$PSScriptRoot\M3TubeRelay.cs" "$PSScriptRoot\LauncherTests.cs"
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
& "$output\LauncherTests.exe" $output
if ($LASTEXITCODE -ne 0) { throw 'Launcher tests failed' }
