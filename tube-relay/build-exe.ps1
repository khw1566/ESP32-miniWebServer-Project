# Builds M3TubeRelay.exe (tray launcher) with the C# compiler of .NET Framework 4,
# which ships with Windows. The exe must stay in this folder next to server.mjs.
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (Get-Process M3TubeRelay -ErrorAction SilentlyContinue) { throw 'Close M3TubeRelay from its Exit menu before rebuilding.' }
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path $csc)) { throw ".NET Framework 4 C# compiler not found: $csc" }
$icon = Join-Path $here 'launcher\icon.ico'
if (-not (Test-Path $icon)) { python (Join-Path $here 'launcher\make_icon.py') }
& $csc /nologo /codepage:65001 /optimize+ /target:winexe "/out:$(Join-Path $here 'M3TubeRelay.exe')" "/win32icon:$icon" `
    /reference:System.Web.Extensions.dll /reference:System.Windows.Forms.dll /reference:System.Drawing.dll (Join-Path $here 'launcher\M3TubeRelay.cs')
if ($LASTEXITCODE -ne 0) { throw 'csc failed' }
Write-Output "Built $(Join-Path $here 'M3TubeRelay.exe')"
