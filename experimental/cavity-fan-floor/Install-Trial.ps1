param([string]$PrinterIp)
$ErrorActionPreference='Stop'
if(!$PrinterIp){$PrinterIp=Read-Host 'Printer IPv4 address for SSH'}
& (Join-Path $PSScriptRoot 'Run-Native.ps1') -Action check -PrinterIp $PrinterIp
& (Join-Path $PSScriptRoot 'app/Install-CC2-Control.ps1') -PrinterIp $PrinterIp
& (Join-Path $PSScriptRoot 'Run-Native.ps1') -Action enable -PrinterIp $PrinterIp
Write-Host 'PRIVATE TRIAL INSTALLED. Reboot the complete idle printer manually, then refresh the browser.'
