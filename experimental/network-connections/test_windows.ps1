param([Parameter(Mandatory=$true)][string]$Package,[Parameter(Mandatory=$true)][string]$Mock,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$fixture=Join-Path $Output 'Windows fixture with spaces'
if(Test-Path -LiteralPath $fixture){throw 'Refuse to overwrite fixture'}
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
foreach($name in 'Install-CC2-Control.ps1','cc2-control-payload.tar.gz','cc2-connections.tar.gz','run-install.sh','prepare-disk.sh','verify-and-run.sh') {
    Copy-Item -LiteralPath (Join-Path $Package $name) -Destination $fixture
}
$env:PATH="$Mock;$env:PATH"
$env:CC2_ARGV_LOG=Join-Path $Output 'native-argv.log'
$env:CC2_ARGV_FAIL=''
if(Test-Path -LiteralPath $env:CC2_ARGV_LOG){throw 'Previous log exists'}
$script=Join-Path $fixture 'Install-CC2-Control.ps1'
& $script -PrinterIp '192.0.2.103'
$before=(Get-Content -LiteralPath $env:CC2_ARGV_LOG).Count
try {& $script -PrinterIp '192.0.2.103;reboot';throw 'Invalid address accepted'} catch {
    if($_.Exception.Message -eq 'Invalid address accepted'){throw}
}
if((Get-Content -LiteralPath $env:CC2_ARGV_LOG).Count -ne $before){throw 'Invalid address reached transport'}
$env:CC2_ARGV_FAIL='ssh.exe'
$rejected=$false
try {& $script -PrinterIp '192.0.2.103'} catch {$rejected=$true}
if(!$rejected){throw 'SSH failure accepted'}
$env:CC2_ARGV_FAIL=''
$before=(Get-Content -LiteralPath $env:CC2_ARGV_LOG).Count
[IO.File]::AppendAllText((Join-Path $fixture 'cc2-connections.tar.gz'),'changed')
$rejected=$false
try {& $script -PrinterIp '192.0.2.103'} catch {$rejected=$true}
if(!$rejected -or (Get-Content -LiteralPath $env:CC2_ARGV_LOG).Count -ne $before){throw 'Changed archive reached transport'}
Write-Host 'PASS: actual Windows PowerShell 5.1 installer, native SSH/SCP argv, paths with spaces, invalid address, SSH failure and checksum refusal. Printer untouched.'
