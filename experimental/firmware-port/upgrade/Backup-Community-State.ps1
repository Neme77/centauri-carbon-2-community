$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Archive-Transport.ps1')

$ip = (Read-Host 'Printer IPv4 address').Trim()
$parsed = $null
if ($ip -notmatch '^\d{1,3}(\.\d{1,3}){3}$' -or ![Net.IPAddress]::TryParse($ip,[ref]$parsed)) {
    throw 'Enter a valid IPv4 address.'
}
$ip = $parsed.ToString()
$desktop = [Environment]::GetFolderPath('Desktop')
$folder = Join-Path $desktop ('CC2-Before-4.4.4-' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $folder | Out-Null
$partial = Join-Path $folder 'community-state.partial'
$archive = Join-Path $folder 'community-state.tar'
$info = New-Object Diagnostics.ProcessStartInfo
$info.FileName = (Get-Command ssh.exe -ErrorAction Stop).Source
$info.Arguments = "-T -o ConnectTimeout=15 -o ServerAliveInterval=15 -o ServerAliveCountMax=3 root@$ip sh -s"
$request = @'
set -eu
cd /opt/usr
[ "$(pwd -P)" = /opt/usr ]
set --
for path in .cc2-* cc2-*; do
    [ -e "$path" ] || [ -L "$path" ] || continue
    set -- "$@" "$path"
done
[ "$#" -gt 0 ] || { echo 'No persistent Community files found.' >&2; exit 1; }
exec tar -cf - "$@"
'@
$bytes = Read-CleanupArchive -Info $info -Request $request -Destination $partial
$entries = @(tar.exe -tf $partial)
if ($LASTEXITCODE -ne 0 -or $bytes -lt 1024 -or !$entries.Count) {
    throw 'Archive verification failed. The partial file was kept.'
}
Move-Item -LiteralPath $partial -Destination $archive
$entries | Set-Content -LiteralPath (Join-Path $folder 'archive-contents.txt') -Encoding UTF8
$sha = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$sha  community-state.tar" | Set-Content -LiteralPath (Join-Path $folder 'SHA256SUMS.txt') -Encoding Ascii
Write-Host "BACKUP VERIFIED: $archive"
