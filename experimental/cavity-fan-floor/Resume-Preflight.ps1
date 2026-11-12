param([string]$PrinterIp)
$ErrorActionPreference='Stop'
if(!$PrinterIp){$PrinterIp=Read-Host 'Printer IPv4 address for SSH'}
if($PrinterIp -notmatch '^([0-9]{1,3}\.){3}[0-9]{1,3}$' -or @($PrinterIp.Split('.') | Where-Object {[int]$_ -gt 255}).Count){throw 'Invalid printer IPv4 address.'}
$text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'resume_preflight.sh'),[Text.Encoding]::UTF8)
$encoded=-join ([Text.Encoding]::UTF8.GetBytes($text.Replace([string][char]13,'')) | ForEach-Object {'\0'+[Convert]::ToString($_,8).PadLeft(3,'0')})
if($encoded.Length -gt 30000){throw 'Preflight helper exceeds the bounded SSH command size.'}
& ssh.exe -o ConnectTimeout=15 "root@$PrinterIp" "printf '%b' '$encoded' | sh"
if($LASTEXITCODE){throw 'Preflight was not resumed. Retain the backup; no installation or reboot requested.'}
