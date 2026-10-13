# Creates (once) the Cloudflare Tunnel "m3-tube" and the DNS record tube.audreaming.com,
# then writes cloudflared.yml for start.ps1. Requires an existing `cloudflared tunnel login`.
param([string]$Hostname = 'tube.audreaming.com', [string]$Name = 'm3-tube', [int]$Port = 8790)
# cloudflared prints update warnings on stderr; results are checked explicitly instead.
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$cloudflared = (Get-Command cloudflared -ErrorAction Stop).Source; if (-not $cloudflared) { throw "cloudflared not found" }
$list = & $cloudflared tunnel list --output json 2>$null | ConvertFrom-Json
$tunnel = $list | Where-Object { $_.name -eq $Name } | Select-Object -First 1
if (-not $tunnel) {
    & $cloudflared tunnel create $Name | Out-Host
    $tunnel = (& $cloudflared tunnel list --output json 2>$null | ConvertFrom-Json) | Where-Object { $_.name -eq $Name } | Select-Object -First 1
}
if (-not $tunnel) { throw "Tunnel $Name was not created" }
$credentials = Join-Path $env:USERPROFILE ".cloudflared\$($tunnel.id).json"
if (-not (Test-Path $credentials)) { throw "Missing tunnel credentials: $credentials" }
& $cloudflared tunnel route dns --overwrite-dns $Name $Hostname | Out-Host
@"
tunnel: $($tunnel.id)
credentials-file: $credentials
ingress:
  - hostname: $Hostname
    service: http://127.0.0.1:$Port
    originRequest:
      disableChunkedEncoding: false
      connectTimeout: 10s
  - service: http_status:404
"@ | Set-Content -Encoding ascii (Join-Path $here 'cloudflared.yml')
Write-Output "Tunnel $Name ($($tunnel.id)) -> https://$Hostname -> http://127.0.0.1:$Port"
