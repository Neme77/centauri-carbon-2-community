# Build the verified CC2 Control 1.1.30 component inside WSL.
[CmdletBinding()]
param([string]$Distro = 'Ubuntu')
$ErrorActionPreference = 'Stop'
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) { throw 'wsl.exe non disponibile.' }
$rootPath = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$pathOutput = & wsl.exe -d $Distro -u root --exec wslpath -a -u $rootPath
if ($LASTEXITCODE -ne 0) { throw 'Conversione del percorso WSL fallita.' }
$linuxRoot = ($pathOutput -join "`n").Trim()
$script = $linuxRoot.TrimEnd('/') + '/prepare.py'
& wsl.exe -d $Distro -u root --exec python3 -u $script
exit $LASTEXITCODE
