param([ValidateSet('check','install')][string]$Action='check',[string]$PrinterIp)
$ErrorActionPreference='Stop'
if(-not $PrinterIp){$PrinterIp=Read-Host 'Printer IPv4 address'}
$address=$null
if($PrinterIp -notmatch '^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$' -or -not [Net.IPAddress]::TryParse($PrinterIp,[ref]$address)){throw 'Valid printer IPv4 required.'}
$target='root@'+$address.ToString()
$stage='/opt/usr/cc2-cumulative-stage-'+[Guid]::NewGuid().ToString('N')
$native=Join-Path $PSScriptRoot 'Native'
$manifest=Get-Content -LiteralPath (Join-Path $native 'SHA256SUMS')
foreach($line in $manifest){
    if($line -notmatch '^([0-9a-f]{64})  ([A-Za-z0-9./_-]+)$'){throw 'Invalid native manifest.'}
    $digest=$Matches[1];$name=$Matches[2]
    if($name.Contains('..') -or $name.StartsWith('/')){throw 'Unsafe native member.'}
    if((Get-FileHash -LiteralPath (Join-Path $native $name) -Algorithm SHA256).Hash.ToLowerInvariant() -cne $digest){throw ('Changed native member '+$name)}
}
function Remote([string]$Command){
    $prior=$ErrorActionPreference
    try {$ErrorActionPreference='Continue';& ssh.exe -o ConnectTimeout=15 -o ServerAliveInterval=15 -o ServerAliveCountMax=2 $target $Command;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$prior}
    if($code){throw ('Cumulative action stopped. Retain '+$stage+' and all backups; no automatic reboot.')}
}
Remote "umask 077; mkdir '$stage'"
foreach($path in @('floor','floor-publisher','prepare-native.sh','retire_completed.sh','SHA256SUMS')){
    $prior=$ErrorActionPreference
    try {$ErrorActionPreference='Continue';& scp.exe -O -r -o ConnectTimeout=15 (Join-Path $native $path) ($target+':'+$stage+'/');$code=$LASTEXITCODE}finally{$ErrorActionPreference=$prior}
    if($code){throw ('Native staging stopped; retain '+$stage)}
}
$mode=if($Action -eq 'install'){'--apply'}else{'--check'}
Remote "cd '$stage' && sh prepare-native.sh $mode"
if($Action -eq 'install'){
    & (Join-Path $PSScriptRoot 'Application/Install-CC2-Control.ps1') -PrinterIp $address.ToString()
    if($LASTEXITCODE){throw 'Application installation stopped; retain native staging and application backups. Do not reboot until reviewed.'}
    Write-Host 'CUMULATIVE TRIAL INSTALLED: EXHAUST and RFID prepared. Share the result before full reboot. Keep power connected during later Canvas transfer.'
}else{
    Write-Host 'CUMULATIVE CHECK COMPLETE. No active startup or Canvas changed. Share result before installation.'
}
