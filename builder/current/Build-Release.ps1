[CmdletBinding()]
param([string]$ExistingBuilder,[string]$Distro='Ubuntu',
      [ValidateSet('stock','community')][string]$Mode='stock')
$ErrorActionPreference='Stop'
if (-not $ExistingBuilder) {
    $desktop=[Environment]::GetFolderPath('Desktop')
    $firmwareVersion=(Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\..\cc2-control\FIRMWARE_VERSION') -Raw).Trim()
    $previousPackage='CC2-Builder-V'+$firmwareVersion+'-Control-Panel-Canvas-Reset-Test-R2'
    foreach ($relative in @("$previousPackage\builder\current",
                            "$previousPackage\$previousPackage\builder\current",
                            'CC2-Builder-V4.3-Candidate\builder\current')) {
        $candidate=Join-Path $desktop $relative
        if (Test-Path -LiteralPath $candidate) {$ExistingBuilder=$candidate;break}
    }
    if (-not $ExistingBuilder) {$ExistingBuilder=Read-Host 'Cartella builder precedente (builder\current) con chiavi e strumenti'}
}
$old=(Resolve-Path -LiteralPath $ExistingBuilder).Path
if (Test-Path -LiteralPath (Join-Path $old 'builder\current')) {$old=Join-Path $old 'builder\current'}
if ($old -eq $PSScriptRoot) {throw 'Usa il builder precedente come sorgente degli input.'}
$inputs=@('keys\cc2_aes_key_v1.bin','keys\cc2_stock_private.pem','keys\cc2_community_release_private.pem',
    'components\ssh\sshd','tools\squashfs-tools-4.6.1\bin\mksquashfs','tools\squashfs-tools-4.6.1\bin\unsquashfs')
foreach ($relative in $inputs) {
    $source=Join-Path $old $relative
    $destination=Join-Path $PSScriptRoot $relative
    if (-not (Test-Path -LiteralPath $destination) -and (Test-Path -LiteralPath $source -PathType Leaf)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
    }
}
# Avoid Windows PowerShell 5 quoting differences in wslpath, including paths with spaces.
$full=(Resolve-Path -LiteralPath $PSScriptRoot).Path
if ($full.Length -lt 3 -or $full.Substring(1,2) -ne ':\') {throw 'Estrarre il pacchetto su un disco locale Windows.'}
$linuxRoot='/mnt/'+$full.Substring(0,1).ToLowerInvariant()+'/'+$full.Substring(3).Replace('\','/')
& wsl.exe -d $Distro -u root --exec python3 -u "$linuxRoot/build_release.py" --mode $Mode
if ($LASTEXITCODE -ne 0) {throw 'Build fallita. Non installare: incolla il primo errore.'}
