@echo off
setlocal
set "PYTHONUTF8=1"
where python >nul 2>nul
if errorlevel 1 (
    echo Python 3.10 or newer is required. Install Python and add it to PATH.
    pause
    exit /b 1
)
python -c "import PIL, tkinter" >nul 2>nul
if errorlevel 1 (
    echo Pillow and tkinter are required.
    echo Run: python -m pip install Pillow
    echo For tkinter, enable Tcl/Tk in the Python installer.
    pause
    exit /b 1
)
python "%~dp0..\tools\esp32_image_converter.py" %*
if errorlevel 1 pause
