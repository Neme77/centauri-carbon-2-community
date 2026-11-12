@echo off
setlocal
for %%I in ("%~dp0.") do set "PORT=%%~fI"
if not exist "%PORT%\native13-check" mkdir "%PORT%\native13-check"
if /i "%~1"=="connections-camera" goto connections_camera
docker.exe run --rm --pull never --network none --cap-drop ALL --security-opt no-new-privileges --mount "type=bind,source=%PORT%,target=/port,readonly" --mount "type=bind,source=%PORT%\native13-check,target=/out" cc2-connections-builder:local-r5h python3 -B -u /port/Check-Native13.py %*
goto finish
:connections_camera
set "TRIAL=connections-camera-%RANDOM%-%RANDOM%"
wsl.exe --cd "%PORT%" -- python3 -B -u Check-Native13.py connections-camera --out native13-check/%TRIAL%
if errorlevel 1 goto finish
docker.exe run --rm --pull never --network none --cap-drop ALL --security-opt no-new-privileges --mount "type=bind,source=%PORT%,target=/port,readonly" --mount "type=bind,source=%PORT%\native13-check\%TRIAL%,target=/trial" cc2-connections-builder:local-r5h python3 -B -u /port/Check-Native13.py qualify-connections-camera --out /trial
:finish
set "RESULT=%ERRORLEVEL%"
pause
exit /b %RESULT%
