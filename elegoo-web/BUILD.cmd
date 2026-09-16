@echo off
setlocal
cd /d "%~dp0"
set "ELEGOO_CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%ELEGOO_CSC%" set "ELEGOO_CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%ELEGOO_CSC%" (
 echo Compilatore .NET Framework non trovato. Serve .NET Framework 4.8.
 pause
 exit /b 1
)
"%ELEGOO_CSC%" /nologo /target:winexe /platform:anycpu /optimize+ /win32manifest:source\app.manifest /win32icon:favicon.ico /out:ElegooWeb.new.exe /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Web.Extensions.dll source\ElegooWeb.cs source\NetworkProbe.cs
if errorlevel 1 (
 echo Compilazione fallita. Invia il messaggio di errore.
 pause
 exit /b 1
)
move /y ElegooWeb.new.exe ElegooWeb.exe >nul
if errorlevel 1 (
 echo Chiudi ElegooWeb.exe e riprova.
 pause
 exit /b 1
)
echo Compilazione completata. Puoi avviare ElegooWeb.exe.
echo SHA256:
certutil -hashfile ElegooWeb.exe SHA256
pause
