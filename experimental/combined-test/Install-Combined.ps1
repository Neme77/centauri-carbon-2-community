[CmdletBinding()]
param([string]$PrinterIp,[switch]$ResetHostKey)
$ErrorActionPreference='Stop'
if (-not $PrinterIp) {$PrinterIp=Read-Host 'IP della stampante'}
if ($PrinterIp -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]*$') {throw 'Indirizzo non valido.'}
foreach ($tool in @('ssh.exe','scp.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {throw "Manca $tool"}
}
$archive=Join-Path $PSScriptRoot 'combined-payload.tar.gz'
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {throw 'Manca il payload. Esegui prima Build-ARM.ps1 e usa lo ZIP generato in output.'}
$sums=[IO.File]::ReadAllLines((Join-Path $PSScriptRoot 'SHA256SUMS'))
$entry=@($sums | Where-Object {$_ -match '  combined-payload\.tar\.gz$'})
if ($entry.Count -ne 1 -or (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant() -cne ($entry[0] -split '  ')[0]) {throw 'Checksum del payload non valido.'}
if ($ResetHostKey) {
    Write-Host 'Rimuovo soltanto la chiave salvata per questo indirizzo. Verifica la nuova impronta richiesta da OpenSSH.'
    $confirm=Read-Host "Digita $PrinterIp per confermare il cambio di chiave"
    if ($confirm -cne $PrinterIp) {throw 'Operazione annullata.'}
    & ssh-keygen.exe -R $PrinterIp
    if ($LASTEXITCODE -ne 0) {throw 'Ripristino chiave SSH fallito.'}
}
$target="root@$PrinterIp"
$stage='/opt/usr/cc2-combined-staging/'+[guid]::NewGuid().ToString('N')
Write-Host 'Test sperimentale persistente: solo a macchina inattiva e fredda. Se il test temporaneo e attivo, prima riavvia.'
Write-Host 'L installazione riavvia i servizi vendor; attendi INSTALLAZIONE COMBINATA PASS prima di impartire comandi.'
& ssh.exe $target "umask 077; mkdir -p $stage"
if ($LASTEXITCODE -ne 0) {throw 'Preparazione staging fallita.'}
& scp.exe $archive "${target}:$stage/payload.tar.gz"
if ($LASTEXITCODE -ne 0) {throw 'Trasferimento fallito. Nessun servizio modificato.'}
& ssh.exe $target "tar -xzf $stage/payload.tar.gz -C $stage && sh $stage/install-combined.sh"
if ($LASTEXITCODE -ne 0) {throw "Installazione non completata: leggi il risultato del ripristino. Staging: $stage"}
Write-Host "Installato: http://${PrinterIp}:8081. Esegui Ctrl+F5. Dopo il riavvio verifica che il modulo sia presente." -ForegroundColor Green
