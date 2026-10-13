# Starts the relay and its Cloudflare Tunnel. Close this window (or Ctrl+C) to stop both.
# cloudflared prints update warnings on stderr; results are checked explicitly instead.
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
if (-not (Test-Path (Join-Path $here 'config.json'))) { node (Join-Path $here 'setup.mjs') }
if (-not (Test-Path (Join-Path $here 'cloudflared.yml'))) { & (Join-Path $here 'setup-tunnel.ps1') }
$tunnel = Start-Process -PassThru -WindowStyle Hidden -FilePath (Get-Command cloudflared).Source `
    -ArgumentList @('tunnel', '--no-autoupdate', '--config', (Join-Path $here 'cloudflared.yml'), 'run') `
    -RedirectStandardError (Join-Path $here 'cloudflared.log')
try { node (Join-Path $here 'server.mjs') }
finally { if (-not $tunnel.HasExited) { Stop-Process -Id $tunnel.Id -Force } }
