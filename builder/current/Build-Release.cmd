@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Release.ps1" %*
if errorlevel 1 (
  echo BUILD FALLITA. Non pubblicare o installare.
  pause
  exit /b 1
)
pause
