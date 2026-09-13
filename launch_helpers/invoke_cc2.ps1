# Shared launcher. Invokes WSL directly, without building a shell command string.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('stock','community')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [string]$Distro = 'Ubuntu',
    [switch]$Preflight,
    [switch]$CheckKey,
    [switch]$KeepWork
)
$ErrorActionPreference = 'Stop'
if ($Preflight -and $CheckKey) { throw 'Usa -Preflight oppure -CheckKey, non entrambi.' }
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) { throw 'wsl.exe non disponibile.' }
$rootPath = (Resolve-Path -LiteralPath $BuildRoot).Path
$pathOutput = & wsl.exe -d $Distro -u root --exec wslpath -a -u $rootPath
$pathExit = $LASTEXITCODE
if ($pathExit -ne 0) { throw "Conversione percorso WSL fallita (codice $pathExit)." }
$linuxRoot = ($pathOutput -join "`n").Trim()
if (-not $linuxRoot.StartsWith('/') -or $linuxRoot.Contains("`n")) { throw 'Percorso WSL inatteso.' }
$runner = $linuxRoot.TrimEnd('/') + '/launch_helpers/run_build.py'
$wslArguments = @('-d', $Distro, '-u', 'root', '--exec', 'python3', '-u', $runner, '--root', $linuxRoot, '--mode', $Mode)
if ($Preflight) { $wslArguments += '--preflight' }
if ($CheckKey) { $wslArguments += '--check-key' }
if ($KeepWork) { $wslArguments += '--keep-work' }
& wsl.exe @wslArguments
exit $LASTEXITCODE
