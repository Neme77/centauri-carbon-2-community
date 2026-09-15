# CC2 builder v3.8 launcher: community signing.
[CmdletBinding()]
param(
    [string]$Distro = 'Ubuntu',
    [switch]$Preflight,
    [switch]$CheckKey,
    [switch]$KeepWork
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'launch_helpers_v3_8/invoke_cc2.ps1') -Mode 'community' -BuildRoot $PSScriptRoot -Distro $Distro -Preflight:$Preflight -CheckKey:$CheckKey -KeepWork:$KeepWork
exit $LASTEXITCODE
