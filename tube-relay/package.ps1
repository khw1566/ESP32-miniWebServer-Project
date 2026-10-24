# Package only explicitly listed public files; never copy credentials or cache.
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $root 'build-exe.ps1') }
$releaseRoot = Join-Path (Split-Path $root -Parent) 'releases\tube-relay'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stage = Join-Path $releaseRoot "M3TubeRelay-$stamp"
New-Item -ItemType Directory -Path $stage -ErrorAction Stop | Out-Null
$files = @('M3TubeRelay.exe', 'server.mjs', 'relay_auth.mjs', 'ytmusic.mjs', 'lib.mjs', 'radio.mjs', 'radio_stations.json', 'audio_cache.mjs', 'music_cache.mjs', 'video_cache.mjs', 'package.json', 'README.md', 'StartTubeRelay.bat', 'start.ps1', 'setup.mjs', 'setup-tunnel.ps1', 'test.mjs', 'test_relay_auth.mjs', 'test_music_cache.mjs', 'test_video_cache.mjs', 'test_media.mjs', 'test_radio.mjs', 'verify-live.mjs', 'verify-radio.mjs', 'verify-music.mjs')
foreach ($name in $files) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $stage $name)
}
$manifest = foreach ($name in $files) {
    $file = Get-Item -LiteralPath (Join-Path $stage $name)
    [PSCustomObject]@{ file = $name; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
}
$manifest | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $stage 'manifest.json')
$zip = "$stage.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Write-Output "Package: $zip"
Write-Output "SHA256: $((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)"
