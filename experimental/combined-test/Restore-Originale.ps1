[CmdletBinding()]
param([string]$PrinterIp,[string]$BackupPath)
$ErrorActionPreference='Stop'
if (-not $PrinterIp) {$PrinterIp=Read-Host 'IP della stampante'}
if ($PrinterIp -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]*$') {throw 'Indirizzo non valido.'}
if (-not $BackupPath) {$BackupPath=Read-Host 'Percorso backup stampato dall installer (/opt/usr/cc2-combined-backup-...)'}
if ($BackupPath -notmatch '^/opt/usr/cc2-combined-backup-[0-9-]+$') {throw 'Percorso backup non valido.'}
& ssh.exe "root@$PrinterIp" "sh $BackupPath/restore.sh"
if ($LASTEXITCODE -ne 0) {throw 'Ripristino non completato: incolla il risultato.'}
