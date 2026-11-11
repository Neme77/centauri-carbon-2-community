@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Backup-Community-State.ps1" %*
exit /b %ERRORLEVEL%
