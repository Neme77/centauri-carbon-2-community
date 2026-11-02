[CmdletBinding()]
param([string]$ExistingBuilder,[string]$Distro='Ubuntu',
      [ValidateSet('stock','community')][string]$Mode='stock',
      [string]$ConnectionsRuntime)
$ErrorActionPreference='Stop'
if (-not $ExistingBuilder) {
    $desktop=[Environment]::GetFolderPath('Desktop')
    $firmwareVersion=(Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\..\cc2-control\FIRMWARE_VERSION') -Raw).Trim()
    $previousPackage='CC2-Builder-V'+$firmwareVersion+'-Updated-Control-Panel'
    foreach ($relative in @("$previousPackage\builder\current",
                            "$previousPackage\$previousPackage\builder\current",
                            'CC2-Builder-V4.3-Candidate\builder\current')) {
        $candidate=Join-Path $desktop $relative
        if (Test-Path -LiteralPath $candidate) {$ExistingBuilder=$candidate;break}
    }
    if (-not $ExistingBuilder) {
        foreach ($package in (Get-ChildItem -LiteralPath $desktop -Directory -Filter 'CC2-Builder-V*' | Sort-Object LastWriteTime -Descending)) {
            foreach ($relative in @('builder\current', ($package.Name+'\builder\current'))) {
                $candidate=Join-Path $package.FullName $relative
                if ((Test-Path -LiteralPath (Join-Path $candidate 'core\firmware_builder.py') -PathType Leaf) -and $candidate -ne $PSScriptRoot) {
                    $ExistingBuilder=$candidate
                    break
                }
            }
            if ($ExistingBuilder) {break}
        }
    }
    if (-not $ExistingBuilder) {$ExistingBuilder=Read-Host 'Previous builder/current directory with local keys and tools'}
}
$old=(Resolve-Path -LiteralPath $ExistingBuilder).Path
if (Test-Path -LiteralPath (Join-Path $old 'builder\current')) {$old=Join-Path $old 'builder\current'}
if ($old -eq $PSScriptRoot) {throw 'Use the previous builder as the local input source.'}
$runtimeTarget=Join-Path $PSScriptRoot 'components\connections\runtime.tar.gz'
if ($ConnectionsRuntime) {
    $runtimeSource=(Resolve-Path -LiteralPath $ConnectionsRuntime).Path
    $pin=(Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\..\experimental\network-connections\runtime.json') -Raw | ConvertFrom-Json).sha256
    if ((Get-FileHash -LiteralPath $runtimeSource -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin) {throw 'Connections runtime checksum mismatch.'}
    if (Test-Path -LiteralPath $runtimeTarget) {
        if ((Get-FileHash -LiteralPath $runtimeTarget -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin) {throw 'Existing Connections input differs; retain it for diagnosis. Nothing overwritten.'}
    } else {
        [void](New-Item -ItemType Directory -Force -Path (Split-Path $runtimeTarget))
        Copy-Item -LiteralPath $runtimeSource -Destination $runtimeTarget
    }
}
if (-not (Test-Path -LiteralPath $runtimeTarget -PathType Leaf)) {throw 'Supply the reviewed local runtime with -ConnectionsRuntime. No unqualified replacement is downloaded.'}
$inputs=@('keys\cc2_aes_key_v1.bin','keys\cc2_stock_private.pem','keys\cc2_community_release_private.pem',
    'components\ssh\sshd','tools\squashfs-tools-4.6.1\bin\mksquashfs','tools\squashfs-tools-4.6.1\bin\unsquashfs',
    'original_firmware\cc2_eeb001_02.01.01.07_20260922210235.zip.sig')
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
if ($full.Length -lt 3 -or $full.Substring(1,2) -ne ':\') {throw 'Extract the builder onto a local Windows drive.'}
$linuxRoot='/mnt/'+$full.Substring(0,1).ToLowerInvariant()+'/'+$full.Substring(3).Replace('\','/')
& wsl.exe -d $Distro -u root --exec python3 -u "$linuxRoot/build_release.py" --mode $Mode
if ($LASTEXITCODE -ne 0) {throw 'Build failed. Do not install; share the first error and retained log.'}
