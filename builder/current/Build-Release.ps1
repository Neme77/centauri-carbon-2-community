[CmdletBinding()]
param([string]$ExistingBuilder,[string]$PrinterIp,[string]$Distro='Ubuntu',
      [ValidateSet('stock','community')][string]$Mode='stock',[switch]$UpdaterOnly)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $UpdaterOnly) {
    if (-not $ExistingBuilder) {$ExistingBuilder=Read-Host 'Cartella del builder precedente con input firmware e chiavi locali'}
    $old=(Resolve-Path -LiteralPath $ExistingBuilder).Path
    if ($old -eq $PSScriptRoot) {throw 'Usa la cartella del builder precedente, non questa.'}
    $inputs=@('original_firmware\cc2_eeb001_02.01.00.00_20260707170825.zip.sig',
        'keys\cc2_aes_key_v1.bin','keys\cc2_stock_private.pem','keys\cc2_community_release_private.pem',
        'components\printer\elegoo_printer_v3_8','components\ssh\sshd','dualtrust\dual_verify.bin',
        'tools\squashfs-tools-4.6.1\bin\mksquashfs','tools\squashfs-tools-4.6.1\bin\unsquashfs')
    foreach ($relative in $inputs) {
        $source=Join-Path $old $relative
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $destination=Join-Path $PSScriptRoot $relative
            if (-not (Test-Path -LiteralPath $destination)) {
                New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
                Copy-Item -LiteralPath $source -Destination $destination
            }
        }
    }
}
$vendor=Join-Path $repo 'experimental\combined-test\reactor\vendor-link'
$lib=Join-Path $vendor 'libcolib.so'
$expected='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403'
if (-not (Test-Path -LiteralPath $lib -PathType Leaf)) {
    if (-not $PrinterIp) {$PrinterIp=Read-Host 'IP stampante qualificata: recupero libco in sola lettura'}
    if ($PrinterIp -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]*$') {throw 'Indirizzo non valido.'}
    New-Item -ItemType Directory -Force -Path $vendor | Out-Null
    $incoming=Join-Path $vendor 'libcolib.download'
    & scp.exe "root@${PrinterIp}:/opt/lib/libcolib.so" $incoming
    if ($LASTEXITCODE -ne 0) {throw 'Recupero libco fallito.'}
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $incoming).Hash.ToLowerInvariant() -cne $expected) {
        Remove-Item -LiteralPath $incoming
        throw 'Libco non qualificata. Build interrotta.'
    }
    Move-Item -LiteralPath $incoming -Destination $lib
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $lib).Hash.ToLowerInvariant() -cne $expected) {throw 'Libco locale non qualificata.'}
$pathOutput=& wsl.exe -d $Distro -u root --exec wslpath -a -u $PSScriptRoot
if ($LASTEXITCODE -ne 0) {throw 'Conversione percorso WSL fallita.'}
$linuxRoot=($pathOutput -join "`n").Trim()
$arguments=@('-d',$Distro,'-u','root','--exec','python3','-u',"$linuxRoot/build_release.py",'--mode',$Mode)
if ($UpdaterOnly) {$arguments+='--updater-only'}
& wsl.exe @arguments
if ($LASTEXITCODE -ne 0) {throw 'Build fallita. Non pubblicare o installare: incolla il primo errore.'}
