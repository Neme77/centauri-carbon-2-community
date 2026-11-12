@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Release.ps1" %*
if errorlevel 1 (
  echo BUILD FAILED. Do not publish or install.
  pause
  exit /b 1
)
pause
