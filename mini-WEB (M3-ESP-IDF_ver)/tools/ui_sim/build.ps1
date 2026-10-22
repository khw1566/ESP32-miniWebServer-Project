# Build and run the host UI preview. Requires Visual Studio 2022 C tools and Python with Pillow.
param([string]$Out = "$env:TEMP\m3_ui_sim", [int]$Lang = -1)
$ErrorActionPreference = 'Stop'
$sim = $PSScriptRoot; $root = Resolve-Path "$sim\..\.."
$lvgl = "$root\managed_components\lvgl__lvgl"
New-Item -ItemType Directory -Force "$Out\obj", "$Out\frames" | Out-Null
python "$sim\gen_sdkconfig.py" "$root\sdkconfig" "$Out\sim_sdkconfig.h"
$sources = Get-ChildItem -Recurse "$lvgl\src" -Filter *.c | Where-Object { $_.FullName -notlike '*\drivers\*' } | ForEach-Object { $_.FullName }
$sources += "$sim\sim_main.c", "$root\main\m3_font_korean.c", "$root\main\m3_2048.c", "$root\main\m3_crossing.c", "$root\main\m3_hangul.c", "$root\main\m3_calendar.c", "$root\main\m3_kana.c", "$root\main\m3_calculator.c", "$root\main\m3_flash_usage.c", "$root\main\m3_flash_usage_view.c", "$root\main\m3_inflate.c"
$sources += Get-ChildItem "$root\main" -Filter 'm3_font_*.c' | Where-Object { $_.Name -ne 'm3_font_korean.c' } | ForEach-Object { $_.FullName }
$sources += Get-ChildItem "$root\main" -Filter 'm3_i18n.c' | ForEach-Object { $_.FullName }
$rsp = "$Out\sources.rsp"; $sources | ForEach-Object { "`"$_`"" } | Set-Content -Encoding ascii $rsp
$flags = "/nologo /utf-8 /O2 /Zi /FS /MP /w /std:c11 /DLV_CONF_SKIP /DLV_CONF_KCONFIG_EXTERNAL_INCLUDE=`\`"sim_sdkconfig.h`\`" " +
  "/I`"$Out`" /I`"$sim\include`" /I`"$root\main\include`" /I`"$lvgl`" /I`"$lvgl\src`" /Fo`"$Out\obj\\`" /Fd`"$Out\obj\\`" /Fe`"$Out\m3_ui_sim.exe`""
$bat = "$Out\build.bat"
"@echo off`r`ncall `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`" >nul`r`ncl $flags @`"$rsp`" /link user32.lib dbghelp.lib" | Set-Content -Encoding ascii $bat
$ErrorActionPreference = 'Continue'
cmd /c "`"$bat`""
$compileExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($compileExit -ne 0) { throw "Host simulator compiler failed ($compileExit); refusing a stale EXE" }
if (!(Test-Path "$Out\m3_ui_sim.exe")) { throw 'build failed' }
Remove-Item "$Out\frames\*" -ErrorAction SilentlyContinue
& "$Out\m3_ui_sim.exe" "$Out\frames" $Lang
python "$sim\to_png.py" "$Out\frames"
Write-Output "Frames: $Out\frames"
