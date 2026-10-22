# Starts the relay and its Cloudflare Tunnel. Close this window (or Ctrl+C) to stop both.
# cloudflared prints update warnings on stderr; results are checked explicitly instead.
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$launchLock = New-Object System.Threading.Mutex($false, 'Local\M3TubeRelayLauncher')
try { $launchOwned = $launchLock.WaitOne(0) }
catch [System.Threading.AbandonedMutexException] { $launchOwned = $true }
if (-not $launchOwned) {
    Write-Output 'M3 YouTube relay launcher is already running. Use the existing server.'
    $launchLock.Dispose()
    return
}
try {
# A second launcher must not start another media proxy or tunnel.
try {
    $health = Invoke-RestMethod 'http://127.0.0.1:8790/healthz' -TimeoutSec 2
    if ($health.ok -and $health.service -eq 'm3-tube-relay') {
        Write-Output 'M3 YouTube relay is already running. Use the existing server.'
        return
    }
} catch { }
if (-not (Test-Path (Join-Path $here 'config.json'))) { node (Join-Path $here 'setup.mjs') }
if (-not (Test-Path (Join-Path $here 'cloudflared.yml'))) { & (Join-Path $here 'setup-tunnel.ps1') }
# Start-Process joins ArgumentList without preserving argument boundaries.
# Quote the config path explicitly because the workspace name contains spaces.
$tunnelConfig = Join-Path $here 'cloudflared.yml'
$tunnel = Start-Process -PassThru -WindowStyle Hidden -FilePath (Get-Command cloudflared).Source `
    -ArgumentList @('tunnel', '--no-autoupdate', '--config', ('"{0}"' -f $tunnelConfig), 'run') `
    -RedirectStandardError (Join-Path $here 'cloudflared.log') -ErrorAction Stop
try { node (Join-Path $here 'server.mjs') }
finally { if ($tunnel -and -not $tunnel.HasExited) { Stop-Process -Id $tunnel.Id -Force } }
} finally { $launchLock.ReleaseMutex(); $launchLock.Dispose() }
