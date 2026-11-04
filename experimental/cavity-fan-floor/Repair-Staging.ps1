param([string]$PrinterIp)
$ErrorActionPreference='Stop'
if(!$PrinterIp){$PrinterIp=Read-Host 'Printer IPv4 address for SSH'}
if($PrinterIp -notmatch '^([0-9]{1,3}\.){3}[0-9]{1,3}$' -or @($PrinterIp.Split('.') | Where-Object {[int]$_ -gt 255}).Count){throw 'Invalid printer IPv4 address.'}
$text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'repair_staging.sh'),[Text.Encoding]::UTF8)
$encoded=-join ([Text.Encoding]::UTF8.GetBytes($text.Replace([string][char]13,'')) | ForEach-Object {'\0'+[Convert]::ToString($_,8).PadLeft(3,'0')})
& ssh.exe -o ConnectTimeout=15 "root@$PrinterIp" "printf '%b' '$encoded' | sh"
if($LASTEXITCODE){throw 'Inactive staging was not retired. Retain output; no installation or reboot requested.'}
