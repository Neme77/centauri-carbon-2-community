# The actual launcher, with inert transport functions and a fake child installer.
$ErrorActionPreference='Stop'
$source=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Run-Cumulative.ps1') -Raw
$fixture=Join-Path $env:TEMP ('cc2-cumulative-client-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory((Join-Path $fixture 'Native/floor'))
[void][IO.Directory]::CreateDirectory((Join-Path $fixture 'Application'))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Run-Cumulative.ps1') -Destination $fixture
[IO.File]::WriteAllText((Join-Path $fixture 'Application/Install-CC2-Control.ps1'),'param([string]$PrinterIp);$global:cc2Child=$PrinterIp;$global:LASTEXITCODE=0')
foreach($name in @('floor/fixture','floor-publisher','prepare-native.sh','retire_completed.sh')){[IO.File]::WriteAllText((Join-Path $fixture ('Native/'+$name)),'fixture')}
$rows=foreach($name in @('floor/fixture','floor-publisher','prepare-native.sh','retire_completed.sh')){(Get-FileHash -LiteralPath (Join-Path $fixture ('Native/'+$name)) -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$name}
[IO.File]::WriteAllLines((Join-Path $fixture 'Native/SHA256SUMS'),$rows)
$global:cc2Transport=@();$global:cc2Fail='';$global:cc2Child=''
function global:ssh.exe {$global:cc2Transport+=('ssh '+($args -join ' '));$global:LASTEXITCODE=if($global:cc2Fail -eq 'ssh'){1}else{0}}
function global:scp.exe {$global:cc2Transport+=('scp '+($args -join ' '));$global:LASTEXITCODE=if($global:cc2Fail -eq 'scp'){1}else{0}}
try {
    foreach($case in @('check','install','ssh','scp','invalid-ip','tamper')){
        $global:cc2Transport=@();$global:cc2Child='';$global:cc2Fail=$case;$global:LASTEXITCODE=0
        if($case -eq 'tamper'){[IO.File]::WriteAllText((Join-Path $fixture 'Native/floor-publisher'),'changed')}
        $ip=if($case -eq 'invalid-ip'){'1.2.3.4;bad'}else{'192.168.1.103'}
        $action=if($case -eq 'check'){'check'}else{'install'}
        $failed=$false
        try {& (Join-Path $fixture 'Run-Cumulative.ps1') -Action $action -PrinterIp $ip}catch{$failed=$true}
        $expected=$case -notin @('check','install')
        if($failed -ne $expected){throw "Unexpected launcher outcome: $case"}
        if(($case -eq 'install') -ne ($global:cc2Child -eq '192.168.1.103')){throw "Child installer order: $case"}
        if($case -in @('invalid-ip','tamper') -and $global:cc2Transport.Count){throw 'Transport ran before input validation.'}
        if($case -eq 'check' -and ($global:cc2Transport[-1] -notmatch '--check$')){throw 'Check invoked a mutation.'}
        if($case -eq 'install' -and ($global:cc2Transport[-1] -notmatch '--apply$')){throw 'Install omitted native preparation.'}
    }
    Write-Host 'PASS: six actual launcher cases; inert transport only, no printer accessed.'
} finally {
    Remove-Item Function:\ssh.exe,Function:\scp.exe
    # Preserve the small fixture as diagnostic evidence; no recursive deletion.
}
