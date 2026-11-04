$ErrorActionPreference='Stop'
$expected=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'repair_staging.sh'),[Text.Encoding]::UTF8).Replace([string][char]13,'')
$global:calls=0
function global:ssh.exe {
    $global:calls++
    $octal=($args[-1] -split "'")[3]
    $bytes=[regex]::Matches($octal,'\\0([0-7]{3})') | ForEach-Object {[byte][Convert]::ToInt32($_.Groups[1].Value,8)}
    if([Text.Encoding]::UTF8.GetString([byte[]]$bytes) -cne $expected){throw 'Repair script changed during transport encoding'}
    $global:LASTEXITCODE=if($global:refuse){1}else{0}
}
try{
    $global:refuse=$false
    & (Join-Path $PSScriptRoot 'Repair-Staging.ps1') -PrinterIp '192.168.1.10'
    if($global:calls -ne 1){throw 'Expected one mock SSH call'}
    $failed=$false
    try{& (Join-Path $PSScriptRoot 'Repair-Staging.ps1') -PrinterIp '192.168.1.300'}catch{$failed=$true}
    if(!$failed -or $global:calls -ne 1){throw 'Invalid IPv4 reached mock SSH'}
    $global:refuse=$true;$failed=$false
    try{& (Join-Path $PSScriptRoot 'Repair-Staging.ps1') -PrinterIp '192.168.1.10'}catch{$failed=$true}
    if(!$failed -or $global:calls -ne 2){throw 'Remote repair refusal was ignored'}
}finally{Remove-Item -LiteralPath function:global:ssh.exe -ErrorAction SilentlyContinue}
Write-Host 'PASS: actual recovery controller, exact script transport, invalid-IP refusal and remote failure propagation; only mocked SSH used.'
