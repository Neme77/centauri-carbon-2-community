param([switch]$Apply)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Archive-Transport.ps1')
$script = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'prepare-upgrade.sh') -Raw
$ip = (Read-Host 'Printer IPv4 address').Trim()
$parsed = $null
if ($ip -notmatch '^\d{1,3}(\.\d{1,3}){3}$' -or ![Net.IPAddress]::TryParse($ip,[ref]$parsed)) {
    throw 'Enter a valid IPv4 address.'
}
$folder = Join-Path ([Environment]::GetFolderPath('Desktop')) ('CC2-Upgrade-' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $folder | Out-Null
$probeSha = ''
$archiveSha = ''
$remoteProbe = '/tmp/cc2-upgrade-probe-' + [Guid]::NewGuid().ToString('N')
$remoteEvidence = '/tmp/cc2-upgrade-evidence-' + [Guid]::NewGuid().ToString('N')
$evidence = Join-Path $PSScriptRoot 'cc2-upgrade-evidence'
$evidenceSha = (Get-Content -LiteralPath "$evidence.sha256" -Raw).Trim()
if ($evidenceSha -cnotmatch '^[0-9a-f]{64}$' -or
    (Get-FileHash -LiteralPath $evidence -Algorithm SHA256).Hash.ToLowerInvariant() -cne $evidenceSha) {
    throw 'The read-only Canvas evidence probe checksum does not match.'
}
& scp.exe -O "$evidence" "root@${parsed}:$remoteEvidence"
if ($LASTEXITCODE) { throw 'Could not transfer the Canvas evidence probe.' }
$plan = @()
function Invoke-UpgradeStage([string]$Action,[string]$Destination) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = (Get-Command ssh.exe -ErrorAction Stop).Source
    $info.Arguments = "-T -o ConnectTimeout=15 -o ServerAliveInterval=15 -o ServerAliveCountMax=3 root@$parsed sh -s"
    $request = "action='$Action'; probe='$remoteProbe'; probe_sha='$probeSha'; archive_sha='$archiveSha'`n"
    $request += "evidence='$remoteEvidence'; evidence_sha='$evidenceSha'; evidence_keep='$([int][bool]$Apply)'`n"
    $request += 'set -- ' + ($plan -join ' ') + "`n" + $script
    [void](Read-CleanupArchive -Info $info -Request $request -Destination $Destination -MaxRequestBytes 16384)
}
$planFile = Join-Path $folder 'plan.txt'
Invoke-UpgradeStage discover $planFile
$lines = @(Get-Content -LiteralPath $planFile)
if (!$lines.Count -or $lines[-1] -cne 'PLAN COMPLETE') { throw "Invalid inventory: $planFile" }
$plan = @($lines | Select-Object -SkipLast 1)
if ($plan.Count -gt 128 -or @($plan | Sort-Object -Unique).Count -ne $plan.Count -or
    @($plan | Where-Object { $_ -cnotmatch '^(cc2-rfid|cc2-reactor-disabled|cc2-control-(backup-[0-9]{8}-[0-9]{6}-[0-9]+|install-[0-9a-f]{32}))$' }).Count) {
    throw "Unexpected inventory: $planFile"
}
$plan | Write-Host
if (!$plan.Count -or !$Apply) {
    Write-Host "PLAN ONLY. $($plan.Count) candidate paths. Report: $planFile"
    return
}
$probe = Join-Path $PSScriptRoot 'cc2-upgrade-probe'
$probeSha = (Get-Content -LiteralPath "$probe.sha256" -Raw).Trim()
if ($probeSha -cnotmatch '^[0-9a-f]{64}$' -or
    (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant() -cne $probeSha) {
    throw 'The ARM console probe checksum does not match.'
}
& scp.exe -O "$probe" "root@${parsed}:$remoteProbe"
if ($LASTEXITCODE) { throw 'Could not transfer the console probe.' }
$partial = Join-Path $folder 'community-stages.partial'
Invoke-UpgradeStage backup $partial
$entries = @(tar.exe -tf $partial)
if ($LASTEXITCODE) { throw "Cannot read the backup: $partial" }
$roots = @($entries | ForEach-Object { ($_ -replace '^\./','').Split('/')[0] } | Sort-Object -Unique)
if (Compare-Object -ReferenceObject $plan -DifferenceObject $roots -CaseSensitive) {
    throw "Backup contents differ from the plan: $partial"
}
$archive = Join-Path $folder 'community-stages.tar'
Move-Item -LiteralPath $partial -Destination $archive
$archiveSha = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveSha  community-stages.tar" | Set-Content -LiteralPath (Join-Path $folder 'SHA256SUMS.txt') -Encoding Ascii
Write-Host "BACKUP VERIFIED: $archive"
$report = Join-Path $folder 'quarantine.txt'
Invoke-UpgradeStage apply $report
$result = @(Get-Content -LiteralPath $report)
$result | Write-Host
if ($result[-1] -cne 'QUARANTINE COMPLETE' -or
    @($result | Where-Object { $_ -like 'MOVED=*' }).Count -ne $plan.Count) {
    throw "Quarantine incomplete. Keep the backup and report: $folder"
}
