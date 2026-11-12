$ErrorActionPreference='Stop'
$source=$PSScriptRoot
$root=Join-Path ([IO.Path]::GetTempPath()) ('cc2-cavity-pc-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $root)
foreach($name in 'Run-Native.ps1','Install-Trial.ps1'){Copy-Item -LiteralPath (Join-Path $source $name) -Destination $root}
[void](New-Item -ItemType Directory -Path (Join-Path $root 'app'))
[void](New-Item -ItemType Directory -Path (Join-Path $root 'cc2-cavity-floor'))
foreach($name in 'SHA256SUMS','manage.sh','thermal.awk'){Set-Content -LiteralPath (Join-Path $root "cc2-cavity-floor/$name") -Value 'fixture' -Encoding ASCII}
Set-Content -LiteralPath (Join-Path $root 'app/Install-CC2-Control.ps1') -Encoding ASCII -Value @'
param([string]$PrinterIp)
$global:steps.Add('APP')
if($global:scenario -eq 'app-failure'){throw 'dummy app failure'}
'@
$global:steps=New-Object 'System.Collections.Generic.List[string]'
function global:ssh.exe {
    $text=$args[-1];$octal=($text -split "'")[3]
    $bytes=[regex]::Matches($octal,'\\0([0-7]{3})') | ForEach-Object {[byte][Convert]::ToInt32($_.Groups[1].Value,8)}
    $script=[Text.Encoding]::UTF8.GetString([byte[]]$bytes)
    $global:LASTEXITCODE=0
    if($script.Contains('CC2-FLOOR-PREPARE')){
        $global:steps.Add('PREPARE')
        if($global:scenario -eq 'foreign'){$global:LASTEXITCODE=1;return}
        if($global:remoteExists){'CC2_EXISTING'}else{$global:remoteExists=$true;'CC2_NEW'}
    }elseif($script -match 'manage\.sh (check|enable|disable|status)'){
        $action=$Matches[1].ToUpperInvariant();$global:steps.Add($action)
        if($global:scenario -eq 'check-failure' -and $action -eq 'CHECK'){$global:LASTEXITCODE=1;return}
        'dummy remote result'
    }else{throw 'Unexpected SSH command in PC fixture'}
}
function global:scp.exe {
    $global:steps.Add('TRANSFER');$global:LASTEXITCODE=0
    if($global:scenario -eq 'transfer-failure'){$global:LASTEXITCODE=1}
}
try{
    foreach($global:scenario in 'new','existing','foreign','transfer-failure','check-failure','app-failure','invalid-ip','disable'){
        $global:steps.Clear();$global:remoteExists=$global:scenario -eq 'existing';$failed=$false
        $ip=if($global:scenario -eq 'invalid-ip'){'192.168.1.300'}else{'192.168.1.10'}
        try{
            if($global:scenario -eq 'disable'){& (Join-Path $root 'Run-Native.ps1') -Action disable -PrinterIp $ip}
            else{& (Join-Path $root 'Install-Trial.ps1') -PrinterIp $ip}
        }catch{$failed=$true}
        $expected=switch($global:scenario){
            new {'PREPARE,TRANSFER,CHECK,APP,PREPARE,ENABLE'}
            existing {'PREPARE,CHECK,APP,PREPARE,ENABLE'}
            foreign {'PREPARE'}
            transfer-failure {'PREPARE,TRANSFER'}
            check-failure {'PREPARE,TRANSFER,CHECK'}
            app-failure {'PREPARE,TRANSFER,CHECK,APP'}
            invalid-ip {''}
            disable {'DISABLE'}
        }
        if(($global:steps -join ',') -cne $expected){throw "Wrong PC order: $global:scenario : $($global:steps -join ',')"}
        if($failed -ne ($global:scenario -notin 'new','existing','disable')){throw "Wrong PC result: $global:scenario"}
        Write-Host "PASS PC fixture: $global:scenario"
    }
}finally{
    Remove-Item -LiteralPath function:global:ssh.exe -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath function:global:scp.exe -ErrorAction SilentlyContinue
}
Write-Host 'PASS: Windows trial controller with mocked SSH/SCP/app; no overwrite of existing components, refusals and rollback entry. No printer accessed.'
