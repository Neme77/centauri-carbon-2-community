@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Combined.ps1" -ResetHostKey %*
pause
