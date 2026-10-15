@echo off
setlocal enabledelayedexpansion

title zrok Automated Installer

echo =======================================================
echo              zrok Automated Installer
echo =======================================================
echo.

set "INSTALL_DIR=%LOCALAPPDATA%\Programs\zrok"
set "TEMP_DIR=%TEMP%\zrok_install"

if exist "%TEMP_DIR%" rmdir /s /q "%TEMP_DIR%"
mkdir "%TEMP_DIR%"

if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

echo [1/4] Querying latest release URL from GitHub...
for /f "usebackq tokens=*" %%i in (`powershell -NoProfile -Command "(Invoke-RestMethod -Uri 'https://api.github.com/repos/openziti/zrok/releases/latest').assets | Where-Object { $_.name -like '*windows_amd64.tar.gz' -or $_.name -like '*windows_amd64.zip' } | Select-Object -ExpandProperty browser_download_url -First 1"`) do (
    set "DOWNLOAD_URL=%%i"
)

if "%DOWNLOAD_URL%"=="" (
    echo [ERROR] Failed to retrieve download URL.
    goto :cleanup_error
)

for %%F in ("%DOWNLOAD_URL%") do set "ARCHIVE_NAME=%%~nxF"
set "ARCHIVE_PATH=%TEMP_DIR%\%ARCHIVE_NAME%"

echo [2/4] Downloading binary archive...
echo URL: %DOWNLOAD_URL%
powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; (New-Object Net.WebClient).DownloadFile('%DOWNLOAD_URL%', '%ARCHIVE_PATH%')"

if not exist "%ARCHIVE_PATH%" (
    echo [ERROR] Failed to download archive file.
    goto :cleanup_error
)

echo [3/4] Extracting archive and deploying binary...
powershell -NoProfile -Command ^
    "$archive = '%ARCHIVE_PATH%';" ^
    "$extractDir = '%TEMP_DIR%\extracted';" ^
    "New-Item -ItemType Directory -Force -Path $extractDir | Out-Null;" ^
    "if ($archive.EndsWith('.zip')) {" ^
    "    Expand-Archive -Path $archive -DestinationPath$extractDir -Force;" ^
    "} else {" ^
    "    & tar -xf $archive -C$extractDir;" ^
    "    $innerTar = Get-ChildItem -Path$extractDir -Filter '*.tar' -File | Select-Object -First 1;" ^
    "    if ($innerTar) { & tar -xf $innerTar.FullName -C$extractDir; }" ^
    "}" ^
    "$target = Get-ChildItem -Path$extractDir -Filter 'zrok.exe' -Recurse -File | Select-Object -First 1;" ^
    "if (-not $target) {" ^
    "    $target = Get-ChildItem -Path '%TEMP_DIR%' -Filter 'zrok.exe' -Recurse -File | Select-Object -First 1;" ^
    "}" ^
    "if ($target) {" ^
    "    Copy-Item -Path $target.FullName -Destination '%INSTALL_DIR%\zrok.exe' -Force;" ^
    "    exit 0;" ^
    "} else {" ^
    "    Write-Host 'Diagnostics: Listed files in temp:';" ^
    "    Get-ChildItem -Path '%TEMP_DIR%' -Recurse | Select-Object -ExpandProperty FullName;" ^
    "    exit 1;" ^
    "}"

if %errorlevel% neq 0 (
    echo [ERROR] Failed to locate or deploy zrok.exe.
    goto :cleanup_error
)

echo [4/4] Configuring user PATH environment variable...
powershell -NoProfile -Command ^
    "$userPath = [Environment]::GetEnvironmentVariable('Path', 'User');" ^
    "if ($userPath -split ';' -notcontains '%INSTALL_DIR%') {" ^
    "    [Environment]::SetEnvironmentVariable('Path', $userPath + ';%INSTALL_DIR%', 'User');" ^
    "    Write-Host 'Target directory added to user PATH.';" ^
    "} else {" ^
    "    Write-Host 'Target directory is already present in user PATH.';" ^
    "}"

rmdir /s /q "%TEMP_DIR%"

echo.
echo =======================================================
echo              Installation Completed
echo =======================================================
echo Target Path: %INSTALL_DIR%\zrok.exe
echo.
echo Notice:
echo Please restart your terminal session to apply PATH changes.
echo.
echo Verification commands:
echo   zrok version
echo   zrok invite
echo =======================================================
pause
exit /b 0

:cleanup_error
if exist "%TEMP_DIR%" rmdir /s /q "%TEMP_DIR%"
echo.
echo [ABORTED] Installation terminated due to an error.
pause
exit /b 1