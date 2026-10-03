@echo off
setlocal
title Bianca Web Server - ngrok
where python >nul 2>&1
if errorlevel 1 goto use_py
python "%~dp0web_examples\09_bianca_fernando\serve_board_ngrok.py" %*
goto finished
:use_py
where py >nul 2>&1
if errorlevel 1 goto missing_python
py -3 "%~dp0web_examples\09_bianca_fernando\serve_board_ngrok.py" %*
goto finished
:missing_python
echo [ERROR] Python was not found. Install Python or add it to PATH.
pause
exit /b 1
:finished
set "SERVER_EXIT_CODE=%ERRORLEVEL%"
echo.
echo Web server and tunnel stopped.
pause
exit /b %SERVER_EXIT_CODE%
