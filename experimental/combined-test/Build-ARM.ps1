[CmdletBinding()]
param([string]$Distro='Ubuntu')
$ErrorActionPreference='Stop'
$pathOutput=& wsl.exe -d $Distro --exec wslpath -a -u $PSScriptRoot
if ($LASTEXITCODE -ne 0) {throw 'Conversione percorso WSL fallita.'}
$linuxPath=($pathOutput -join "`n").Trim()
& wsl.exe -d $Distro --exec python3 -u "$linuxPath/build.py"
if ($LASTEXITCODE -ne 0) {throw 'Build combinata fallita. Non installare: incolla il primo errore.'}
