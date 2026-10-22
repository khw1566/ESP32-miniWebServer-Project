param([ValidateSet('M1','M3')][string]$Model, [string]$Out, [int]$Lang=0)
$ErrorActionPreference='Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$project = Join-Path $repo "mini-WEB ($Model-ESP-IDF_ver)"
if (!$Out) { $Out = Join-Path $repo "results/product-ui-capture-20261015/$($Model.ToLower())-current" }
$Out = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force "$Out/obj","$Out/frames" | Out-Null
& python -X utf8 "$PSScriptRoot/prepare_host.py" $Model $Out
if ($LASTEXITCODE -ne 0) { throw 'Host source preparation failed.' }
$lvgl = Join-Path $project 'managed_components/lvgl__lvgl'
if ($Model -eq 'M3') {
    $sim = Join-Path $project 'tools/ui_sim'
    & python -X utf8 "$sim/gen_sdkconfig.py" "$project/sdkconfig" "$Out/sim_sdkconfig.h"
    $sources = @(Get-ChildItem -Recurse "$lvgl/src" -Filter *.c | Where-Object FullName -NotLike '*\drivers\*' | ForEach-Object FullName)
    $sources += "$Out/sim_main.c", "$project/main/m3_2048.c", "$project/main/m3_crossing.c", "$project/main/m3_hangul.c", "$project/main/m3_calendar.c", "$project/main/m3_kana.c", "$project/main/m3_calculator.c", "$project/main/m3_i18n.c"
    $sources += Get-ChildItem "$project/main" -Filter 'm3_font_*.c' | ForEach-Object FullName
    $include = "$sim/include"
    $defines = '/DLV_CONF_SKIP /DLV_CONF_KCONFIG_EXTERNAL_INCLUDE=\"sim_sdkconfig.h\"'
} else {
    $sim = Join-Path $PSScriptRoot 'm1'
    & python -X utf8 "$sim/gen_config.py" "$project/sdkconfig" "$Out/sdkconfig.h"
    $sources = @(Get-ChildItem -Recurse "$lvgl/src" -Filter *.c | ForEach-Object FullName)
    $sources += "$sim/sim_main.c", "$project/main/m1_dashboard.c", "$project/main/m1_keyboard.c", "$project/main/font_m1.c", "$Out/qrcode_host.c"
    $include = "$sim/include"
    $defines = '/DLV_CONF_SKIP /DLV_CONF_KCONFIG_EXTERNAL_INCLUDE=\"sdkconfig.h\" /experimental:c11atomics'
}
$sources | ForEach-Object { '"'+$_+'"' } | Set-Content -Encoding ascii "$Out/sources.rsp"
$exe = "$Out/$($Model.ToLower())_ui_sim.exe"
$flags = "/nologo /utf-8 /O2 /MP4 /w /std:c11 $defines /I`"$Out`" /I`"$include`" /I`"$project/main/include`" /I`"$lvgl`" /Fo`"$Out/obj/`" /Fe`"$exe`""
$bat = "@echo off`r`ncall `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`" >nul`r`ncl $flags @`"$Out/sources.rsp`" /link user32.lib dbghelp.lib`r`nexit /b %errorlevel%`r`n"
[IO.File]::WriteAllText("$Out/build.bat",$bat,[Text.Encoding]::ASCII)
$env:PYTHONUTF8='1'
& cmd /c "`"$Out/build.bat`""
if ($LASTEXITCODE -ne 0) { throw "PC $Model compiler failed; captures were not run." }
& $exe "$Out/frames" $Lang
if ($LASTEXITCODE -ne 0) { throw "PC $Model renderer failed." }
if ($Model -eq 'M3') { & python -X utf8 "$sim/to_png.py" "$Out/frames" }
else { & python -X utf8 "$sim/to_png.py" "$Out/frames" }
if ($LASTEXITCODE -ne 0) { throw 'PNG conversion failed.' }
Write-Output "Captured $Model in $Out/frames"
