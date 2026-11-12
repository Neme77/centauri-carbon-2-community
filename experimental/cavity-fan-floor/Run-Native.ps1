param([ValidateSet('check','enable','disable','status')][string]$Action='check',[string]$PrinterIp)
$ErrorActionPreference='Stop'
if(!$PrinterIp){$PrinterIp=Read-Host 'Printer IPv4 address for SSH'}
if($PrinterIp -notmatch '^([0-9]{1,3}\.){3}[0-9]{1,3}$' -or @($PrinterIp.Split('.') | Where-Object {[int]$_ -gt 255}).Count){throw 'Invalid printer IPv4 address.'}
foreach($tool in 'ssh.exe','scp.exe'){if(!(Get-Command $tool -ErrorAction SilentlyContinue)){throw "$tool is required."}}
$target="root@$PrinterIp"
$component=Join-Path $PSScriptRoot 'cc2-cavity-floor'
$base='/opt/usr/cc2-cavity-floor'
function Invoke-NativeRemote([string]$Text){
    $encoded=-join ([Text.Encoding]::UTF8.GetBytes($Text.Replace([string][char]13,'')) | ForEach-Object {'\0'+[Convert]::ToString($_,8).PadLeft(3,'0')})
    $result=& ssh.exe -o ConnectTimeout=15 $target "printf '%b' '$encoded' | sh"
    if($LASTEXITCODE){throw 'Native trial stopped. Retain output and backup; no automatic reboot requested.'}
    $result
}
if($Action -in 'check','enable'){
    $digest=(Get-FileHash -LiteralPath (Join-Path $component 'SHA256SUMS') -Algorithm SHA256).Hash.ToLowerInvariant()
    $prepare=@'
# CC2-FLOOR-PREPARE
set -eu
umask 077
[ "$(id -u)" = 0 ]
[ -d /opt/usr ] && [ ! -L /opt/usr ]
awk '$2=="/opt/usr" && $3!="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
if [ ! -e /opt/usr/cc2-cavity-floor ] && [ ! -L /opt/usr/cc2-cavity-floor ];then
    mkdir /opt/usr/cc2-cavity-floor
    echo CC2_NEW
else
    [ -d /opt/usr/cc2-cavity-floor ] && [ ! -L /opt/usr/cc2-cavity-floor ]
    [ -f /opt/usr/cc2-cavity-floor/SHA256SUMS ] && [ ! -L /opt/usr/cc2-cavity-floor/SHA256SUMS ]
    printf '%s\n' '__SHA__  /opt/usr/cc2-cavity-floor/SHA256SUMS' | sha256sum -c -
    (cd /opt/usr/cc2-cavity-floor && sha256sum -c SHA256SUMS)
    echo CC2_EXISTING
fi
'@
    $probe=@(Invoke-NativeRemote ($prepare.Replace('__SHA__',$digest)))
    $state=$probe | Select-Object -Last 1
    if($state -eq 'CC2_NEW'){
        $files=@(Get-ChildItem -LiteralPath $component -File | Sort-Object Name | ForEach-Object {$_.FullName})
        $destination=$target+':'+$base+'/'
        & scp.exe -O @files $destination
        if($LASTEXITCODE){throw 'Native transfer failed. Incomplete staging retained; no activation requested.'}
    }elseif($state -ne 'CC2_EXISTING'){throw 'Unexpected staging response.'}
    $verify=@'
# CC2-FLOOR-VERIFY
set -eu
printf '%s\n' '__SHA__  /opt/usr/cc2-cavity-floor/SHA256SUMS' | sha256sum -c -
(cd /opt/usr/cc2-cavity-floor && sha256sum -c SHA256SUMS)
sh /opt/usr/cc2-cavity-floor/manage.sh __ACTION__
'@
    Invoke-NativeRemote ($verify.Replace('__SHA__',$digest).Replace('__ACTION__',$Action)) | Out-Host
}else{
    $checks=foreach($name in 'manage.sh','thermal.awk'){
        $sha=(Get-FileHash -LiteralPath (Join-Path $component $name) -Algorithm SHA256).Hash.ToLowerInvariant()
        "$sha  $base/$name"
    }
    $script="set -eu"+[char]10+"printf '%s\n' '"+($checks -join "' '")+"' | sha256sum -c -"+[char]10+"sh $base/manage.sh $Action"+[char]10
    Invoke-NativeRemote $script | Out-Host
}
