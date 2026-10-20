@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Send-To-Printer.ps1"
if errorlevel 1 echo COPY FAILED - COPIA NON RIUSCITA
pause
