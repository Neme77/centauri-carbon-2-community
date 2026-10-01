$ErrorActionPreference = 'Stop'
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('cc2-ssh-test-' + [guid]::NewGuid())
$installer = Join-Path $temporary 'Install-CC2-Control.ps1'

function Read-Host {
    param([string]$Prompt)
    return $global:CC2TestAnswer
}
function ssh-keygen.exe {
    $global:CC2TestCalls.Add('ssh-keygen ' + ($args -join ' '))
    $global:LASTEXITCODE = $global:CC2TestKeygenExit
}
function scp.exe {
    $global:CC2TestCalls.Add('scp')
    $global:LASTEXITCODE = 0
}
function ssh.exe {
    $global:CC2TestCalls.Add('ssh')
    $global:LASTEXITCODE = 0
}

try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    Copy-Item (Join-Path $PSScriptRoot '..\installer\Install-CC2-Control.ps1') $installer
    New-Item -ItemType File -Path (Join-Path $temporary 'cc2-control-1.1.31-payload.tar.gz') | Out-Null

    $cases = @(
        @{ Name='normal'; Reset=$false; Answer=''; KeyExit=0; Fails=$false; Calls='scp|ssh' },
        @{ Name='confirmed'; Reset=$true; Answer='192.0.2.10'; KeyExit=0; Fails=$false; Calls='ssh-keygen -R 192.0.2.10|scp|ssh' },
        @{ Name='cancelled'; Reset=$true; Answer='no'; KeyExit=0; Fails=$true; Calls='' },
        @{ Name='empty confirmation'; Reset=$true; Answer=''; KeyExit=0; Fails=$true; Calls='' },
        @{ Name='reset failure'; Reset=$true; Answer='192.0.2.10'; KeyExit=1; Fails=$true; Calls='ssh-keygen -R 192.0.2.10' }
    )
    foreach ($case in $cases) {
        $global:CC2TestCalls = New-Object 'System.Collections.Generic.List[string]'
        $global:CC2TestAnswer = $case.Answer
        $global:CC2TestKeygenExit = $case.KeyExit
        $failed = $false
        try {
            & $installer -PrinterIp '192.0.2.10' -ResetHostKey:$case.Reset
        } catch {
            $failed = $true
            Write-Output ("Captured error: " + $_.Exception.Message)
        }
        if ($failed -ne $case.Fails -or ($global:CC2TestCalls -join '|') -ne $case.Calls) {
            throw "FAIL: $($case.Name); failed=$failed; calls=$($global:CC2TestCalls -join '|')"
        }
        Write-Output "PASS: $($case.Name)"
    }
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
