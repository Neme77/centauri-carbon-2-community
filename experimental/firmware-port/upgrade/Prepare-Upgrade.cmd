@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Prepare-Upgrade.ps1" %*
set "code=%ERRORLEVEL%"
if not "%code%"=="0" pause
exit /b %code%
