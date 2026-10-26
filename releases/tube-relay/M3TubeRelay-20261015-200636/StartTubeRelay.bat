@echo off
if exist "%~dp0M3TubeRelay.exe" (
    start "" "%~dp0M3TubeRelay.exe"
    exit /b 0
)
rem M3 YouTube relay launcher: relay server (server.mjs) + Cloudflare Tunnel (start.ps1).
rem Close this window or press Ctrl+C to stop both.
chcp 65001 >nul
title M3 YouTube Relay
cd /d "%~dp0"

rem The Microsoft Store "python" alias cannot run yt-dlp; put a real Python first.
if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" set "PATH=%LOCALAPPDATA%\Programs\Python\Python312;%LOCALAPPDATA%\Programs\Python\Python312\Scripts;%PATH%"
rem cloudflared installs outside PATH on some setups.
if exist "%ProgramFiles(x86)%\cloudflared\cloudflared.exe" set "PATH=%ProgramFiles(x86)%\cloudflared;%PATH%"
if exist "%ProgramFiles%\cloudflared\cloudflared.exe" set "PATH=%ProgramFiles%\cloudflared;%PATH%"
if exist "%LOCALAPPDATA%\Microsoft\WinGet\Links\cloudflared.exe" set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"

where node >nul 2>nul || (echo [오류] Node.js를 찾을 수 없습니다. Node.js 20 이상을 설치하세요. & goto fail)
where ffmpeg >nul 2>nul || (echo [오류] ffmpeg가 PATH에 없습니다. & goto fail)
where cloudflared >nul 2>nul || (echo [오류] cloudflared를 찾을 수 없습니다. winget install Cloudflare.cloudflared 로 설치하세요. & goto fail)

echo yt-dlp 버전:
python -m yt_dlp --version 2>nul || echo [경고] python -m yt_dlp 를 실행할 수 없습니다.
echo 재생이 몇 초 뒤 멈추면: python -m pip install -U "yt-dlp[default]"
echo.
echo 중계 서버와 터널을 시작합니다. 확인 주소: https://tube.audreaming.com/healthz
echo 종료하려면 이 창을 닫거나 Ctrl+C를 누르세요.
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1"
echo.
echo 중계 실행기 작업이 끝났습니다. 서버 상태는 위의 안내를 확인하세요.
pause
exit /b 0

:fail
pause
exit /b 1
