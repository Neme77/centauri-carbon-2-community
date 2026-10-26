@echo off
cd /d "%~dp0"
where ssh >nul 2>&1
if errorlevel 1 goto missing
where py >nul 2>&1
if not errorlevel 1 (py -3 start.py & goto end)
where python >nul 2>&1
if not errorlevel 1 (python start.py & goto end)
:missing
echo Install Python 3.10+ and Windows OpenSSH Client first.
pause
:end
