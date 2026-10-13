@echo off
setlocal
set "PYTHONUTF8=1"
where python >nul 2>nul
if errorlevel 1 (
    echo Python 3.10 or newer is required. Install Python and add it to PATH.
    pause
    exit /b 1
)
python "%~dp0..\tools\esp32_audio_converter.py" %*
if errorlevel 1 pause
