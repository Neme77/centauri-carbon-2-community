# CC2 builder v3.7 launcher: stock signing.
[CmdletBinding()]
param(
    [string]$Distro = 'Ubuntu',
    [switch]$Preflight,
    [switch]$CheckKey,
    [switch]$KeepWork
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'launch_helpers/invoke_cc2.ps1') -Mode 'stock' -BuildRoot $PSScriptRoot -Distro $Distro -Preflight:$Preflight -CheckKey:$CheckKey -KeepWork:$KeepWork
exit $LASTEXITCODE
