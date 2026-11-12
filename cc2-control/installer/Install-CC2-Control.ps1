param([string]$PrinterIp, [switch]$ResetHostKey)
$ErrorActionPreference = 'Stop'
if (-not $PrinterIp) { $PrinterIp = Read-Host 'Printer IP address' }
if ($PrinterIp -notmatch '^[A-Za-z0-9.-]+$') { throw 'Invalid printer address.' }
$archive = Join-Path $PSScriptRoot 'cc2-control-payload.tar.gz'
if (-not (Test-Path $archive)) { throw 'The updater payload archive is missing.' }
foreach ($tool in 'ssh.exe', 'scp.exe') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool is required. Enable Windows OpenSSH Client." }
}
$target = "root@$PrinterIp"
if ($ResetHostKey) {
    if (-not (Get-Command ssh-keygen.exe -ErrorAction SilentlyContinue)) {
        throw 'ssh-keygen.exe is required to reset the saved host key.'
    }
    Write-Host "The saved SSH host key for $PrinterIp will be removed."
    Write-Host 'Continue only if you expected the printer host key to change.'
    Write-Host 'OpenSSH will ask you to verify and accept the new fingerprint.'
    $confirmation = Read-Host "Type the printer address ($PrinterIp) to confirm"
    if ($confirmation -cne $PrinterIp) {
        throw 'Host-key reset cancelled. No installation was performed.'
    }
    & ssh-keygen.exe -R $PrinterIp
    if ($LASTEXITCODE -ne 0) {
        throw 'Host-key reset failed. No installation was performed.'
    }
}
$runner = Join-Path $PSScriptRoot 'pc-stage.sh'
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw 'Disk staging runner missing. Extract the complete package.' }
$stage = '/opt/usr/cc2-control-install-' + [guid]::NewGuid().ToString('N')
function Invoke-Remote([string]$Text) {
    $encoded = -join ([Text.Encoding]::UTF8.GetBytes($Text.Replace("`r`n", "`n")) | ForEach-Object { '\0' + [Convert]::ToString($_, 8).PadLeft(3, '0') })
    & ssh.exe $target "printf '%b' '$encoded' | sh"
    if ($LASTEXITCODE) { throw "Installation stopped. Persistent stage, if created: $stage" }
}
$prepare = @'
set -eu
umask 077
[ "$(id -u)" = 0 ]
[ -d /opt/usr ] && [ ! -L /opt/usr ]
awk '$2=="/opt/usr" && $3!="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
free=$(df -k /opt/usr | awk 'END {print $4}')
case "$free" in ''|*[!0-9]*) exit 1;;esac
[ "$free" -ge 65536 ] || { echo 'STOP: less than 64 MiB free persistent storage';exit 1; }
available=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
case "$available" in ''|*[!0-9]*) exit 1;;esac
[ "$available" -ge 24576 ] || { echo 'STOP: less than 24 MiB available RAM';exit 1; }
mkdir '__STAGE__'
'@
Invoke-Remote ($prepare.Replace('__STAGE__', $stage))
& scp.exe -O $archive $runner "${target}:$stage/"
if ($LASTEXITCODE) { throw "Payload upload failed. Disk stage retained: $stage" }
$runnerSha = (Get-FileHash -LiteralPath $runner -Algorithm SHA256).Hash.ToLowerInvariant()
$verify = @'
set -eu
printf '%s\n' '__SHA__  __STAGE__/pc-stage.sh' | sha256sum -c -
sh '__STAGE__/pc-stage.sh'
'@
Invoke-Remote ($verify.Replace('__SHA__', $runnerSha).Replace('__STAGE__', $stage))
Write-Host "CC2 Control installed: http://${PrinterIp}:8081" -ForegroundColor Green
