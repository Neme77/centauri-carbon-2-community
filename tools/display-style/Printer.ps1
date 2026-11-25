param([ValidateSet('Check','Install','Restore','Status')][string]$Action='Check')
$ErrorActionPreference='Stop'
$archive=Join-Path $PSScriptRoot 'display-kit.tar.gz'
$bundleHash='034f420bfff43632367d2b5ca7cc919f730486675f9bd7bc8611ecc87d19f697'
$log=Join-Path $PSScriptRoot ($Action+'-'+(Get-Date -Format yyyyMMdd-HHmmss)+'.log')
$record=Join-Path $PSScriptRoot 'printer-stage.json'
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $bundleHash){throw 'Kit archive differs; preserve files and stop.'}
Write-Host "REPORT: $log"
function Invoke-Recorded {
    param([string]$Program,[string[]]$Arguments)
    $saved=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $Program @Arguments 2>&1 | Tee-Object -FilePath $log -Append
        $code=$LASTEXITCODE
    } finally {$ErrorActionPreference=$saved}
    if($code -ne 0){throw "Command stopped ($code). Keep $log and printer-stage.json; do not repeat automatically."}
}
if($Action -eq 'Check') {
    $ip=Read-Host 'Printer IPv4 [192.168.1.103]'
    if([string]::IsNullOrWhiteSpace($ip)){$ip='192.168.1.103'}
    $address=$null
    if(-not [Net.IPAddress]::TryParse($ip,[ref]$address) -or $address.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork){throw 'A valid IPv4 address is required.'}
    $ip=$address.ToString()
    if(Test-Path -LiteralPath $record){
        $prior=Get-Content -Raw -LiteralPath $record | ConvertFrom-Json
        if($prior.ip -ne $ip -or $prior.bundle_sha256 -ne $bundleHash){throw 'Existing stage belongs to another printer or kit. Preserve it; do not overwrite the record.'}
        $stage=$prior.stage
    } else {
        $id=[Guid]::NewGuid().ToString('N')
        $stage='/opt/usr/cc2-stock-style-'+$id
        $remoteArchive=$stage+'.tar.gz'
        Invoke-Recorded -Program 'scp.exe' -Arguments @('-O',$archive,"root@${ip}:$remoteArchive")
        $bootstrap="set -eu; umask 077; [ ! -L '$remoteArchive' ]; chmod 600 '$remoteArchive'; printf '%s  %s\n' '$bundleHash' '$remoteArchive' | sha256sum -c -; [ ! -e '$stage' ] && [ ! -L '$stage' ]; mkdir -m 700 '$stage'; tar -xzf '$remoteArchive' -C '$stage'; printf 'STAGE=%s\n' '$stage'"
        # Retain even a failed staging attempt for diagnosis; it never grants Apply.
        @{ip=$ip;stage=$stage;bundle_sha256=$bundleHash;read_only_passed=$false} | ConvertTo-Json | Set-Content -LiteralPath $record -Encoding UTF8
        Invoke-Recorded -Program 'ssh.exe' -Arguments @("root@$ip",$bootstrap)
    }
} else {
    if(-not (Test-Path -LiteralPath $record)){throw 'Run Check.cmd first; no verified private stage is recorded.'}
    $prior=Get-Content -Raw -LiteralPath $record | ConvertFrom-Json
    if($prior.bundle_sha256 -ne $bundleHash -or -not $prior.read_only_passed){throw 'Read-only admission is missing or belongs to another kit. Preserve the report.'}
    $ip=$prior.ip;$stage=$prior.stage
}
if($stage -notmatch '^/opt/usr/cc2-stock-style-[0-9a-f]{32}$'){throw 'Invalid stage record.'}
$address=$null
if(-not [Net.IPAddress]::TryParse($ip,[ref]$address) -or $address.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork){throw 'Invalid printer record.'}
$mode=@{Check='--check';Install='--apply';Restore='--restore';Status='--status'}[$Action]
$remote="set -eu; [ ! -L '$stage' ] && [ -d '$stage' ] && [ ! -L '$stage/install.sh' ]; printf '%s  %s\n' 'de1eff32d25c7739d6e77d378910ad30447b12c1c2be6ba1acefe110f4597d16' '$stage/install.sh' | sha256sum -c -; sh '$stage/install.sh' '$mode'"
if($Action -eq 'Check'){Write-Host 'Read-only display admission. Private stage/audit only; no active file replaced or service restarted.'}
else {Write-Host 'Cold idle printer, zero heater targets. No automatic reboot. Retain kit and backup.'}
Invoke-Recorded -Program 'ssh.exe' -Arguments @("root@$ip",$remote)
if($Action -eq 'Check'){
    @{ip=$ip;stage=$stage;bundle_sha256=$bundleHash;read_only_passed=$true} | ConvertTo-Json | Set-Content -LiteralPath $record -Encoding UTF8
    Write-Host "VERIFIED_STAGE=$stage"
    Write-Host 'STOCK_STYLE_DISPLAY_READ_ONLY_PASSED. Send the report before Install. No active display file changed.'
}
if($Action -eq 'Install'){Write-Host 'DISPLAY_STYLE_INSTALL_COMPLETE. Send report before the single manual idle-printer reboot.'}
if($Action -eq 'Restore'){Write-Host 'COMMUNITY_DISPLAY_RESTORE_COMPLETE. Send report before the single manual idle-printer reboot.'}
