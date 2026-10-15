param(
    [string]$BoardUrl = 'http://m1-v1-2-b0cbd899de80.local',
    [string]$Serial = 'b0cbd899de80',
    [ValidateSet('m1', 'm3', 's3', 'c3')][string]$Product = 'm1'
)
$ErrorActionPreference = 'Stop'
if ($Serial -notmatch '^[a-f0-9]{12}$') { throw 'Invalid board serial.' }
$boardUri = [Uri]$BoardUrl
if ($boardUri.Scheme -notin @('http', 'https') -or $boardUri.UserInfo -or $boardUri.Query -or $boardUri.Fragment) {
    throw 'Use the local board URL without credentials or query parameters.'
}
$baseUrl = $boardUri.GetLeftPart([UriPartial]::Authority)
$system = Invoke-RestMethod -Uri ($baseUrl + '/api/system') -TimeoutSec 15
if ($system.serial -ne $Serial) { throw 'Board MAC does not match the provisioned device.' }
if ($system.product -ne $Product) { throw 'Board product does not match the provisioned device.' }
$csvPath = Join-Path $PSScriptRoot "provisioned/$Product-$Serial.csv"
$credentialRows = Import-Csv -LiteralPath $csvPath
$deviceToken = ($credentialRows | Where-Object { $_.key -eq 'token' }).value
if ($deviceToken -notmatch '^[a-f0-9]{64}$') { throw 'Device credentials are missing or invalid.' }
$securePassword = Read-Host 'Local board administrator password' -AsSecureString
$adminPassword = [System.Net.NetworkCredential]::new('', $securePassword).Password
try {
    $reply = Invoke-RestMethod -Method Post -Uri ($baseUrl + '/api/settings') -TimeoutSec 15 -Body @{
        cur_pw = $adminPassword
        cloud_token = $deviceToken
    }
    if ($reply.status -ne 'ok' -or $reply.message -ne 'Device registered. Reboot the board.') {
        throw 'Registration was not confirmed. Install the updated firmware first.'
    }
    Write-Output 'Device key saved. Existing Wi-Fi and administrator settings preserved. Reboot the board.'
} finally {
    $adminPassword = $null
    $deviceToken = $null
    $securePassword.Dispose()
}
