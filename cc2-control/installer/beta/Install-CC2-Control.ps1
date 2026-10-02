param([string]$PrinterIp, [switch]$ResetHostKey)
$ErrorActionPreference = 'Stop'
if (-not $PrinterIp) { $PrinterIp = Read-Host 'Printer IP address' }
if ($PrinterIp -notmatch '^[A-Za-z0-9.-]+$') { throw 'Invalid printer address.' }
$archive = Join-Path $PSScriptRoot 'cc2-control-beta-@BUILD_ID@-payload.tar.gz'
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
& scp.exe $archive "${target}:/tmp/cc2-control-beta-@BUILD_ID@-payload.tar.gz"
if ($LASTEXITCODE) { throw 'Payload upload failed.' }
& ssh.exe $target 'mkdir -p /tmp/cc2-control-beta-@BUILD_ID@ && tar -xzf /tmp/cc2-control-beta-@BUILD_ID@-payload.tar.gz -C /tmp/cc2-control-beta-@BUILD_ID@ && chmod 755 /tmp/cc2-control-beta-@BUILD_ID@/install-on-printer.sh /tmp/cc2-control-beta-@BUILD_ID@/cc2-control && sh /tmp/cc2-control-beta-@BUILD_ID@/install-on-printer.sh'
if ($LASTEXITCODE) { throw 'Installation failed; inspect the SSH output for restoration status.' }
Write-Host "CC2 Control BETA @BUILD_ID@ installed: http://${PrinterIp}:8081" -ForegroundColor Green
