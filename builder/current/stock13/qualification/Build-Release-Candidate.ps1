param([string]$Builder = '/mnt/c/Users/User/Desktop/Test/cc2-firmware-test/builder/current')
$ErrorActionPreference = 'Stop'
$port = (Resolve-Path (Join-Path $PSScriptRoot '../../work/cc2-13-port')).Path
$name = 'CC2_4.4.4_RC4_02.01.01.13.zip.sig'
$target = Join-Path $PSScriptRoot $name
if (Test-Path -LiteralPath $target) {
    throw 'The RC4 output already exists. Keep it and review the build log before starting another build.'
}
$log = Join-Path $PSScriptRoot ('build-rc4-' + (Get-Date -Format yyyyMMdd-HHmmss) + '.log')
Start-Transcript -LiteralPath $log | Out-Null
try {
    wsl.exe -u root --cd "$PSScriptRoot" -- python3 -u -B Check-Active-RFID-Upgrade.py
    if ($LASTEXITCODE) { throw "Active RFID helper update qualification failed. Log: $log" }
    wsl.exe -u root --cd "$PSScriptRoot" -- python3 -u -B Check-RFID-Integrity.py
    if ($LASTEXITCODE) { throw "RFID integrity optimization checks failed. Log: $log" }
    wsl.exe --cd "$PSScriptRoot" -- python3 -u -B Check-Preview-Observations.py
    if ($LASTEXITCODE) { throw "Preview migration regression checks failed. Log: $log" }
    wsl.exe --cd "$PSScriptRoot" -- python3 -u -B Check-Integrated-Release.py
    if ($LASTEXITCODE) { throw "Integrated regression tests failed. Log: $log" }
    $squash = "$Builder/tools/squashfs-tools-4.6.1/bin"
    wsl.exe -u root --cd "$port" -- python3 -u -B Check-Combined13.py `
        --package "../../outputs/CC2-4.4.4-Preview/$name" --signing-mode stock `
        --rfid-preserve-profile '32354677decfa0bf435e1d3c5d4b1d514d793e387a277cb3b3fe0449b1e2bbdf' `
        --rfid-active-from `
            '112c1a84842de6170c4713ddc3ec249aa143749fa42e0fa43673a091ee9fc9f2' `
            '61185ad5edbb8918e4a3c41961d21ef7a2653f9172eb1bd2d468b332d10bc32f' `
            'dc421aa79c56397149c531261c2aef2671ac2e1e5ae22cb64f175fc60e1eb96a' `
            '790dbfce523439dc53ad75ad738613d3116b6f1880cd4ca324d7cb531bfbffbb' `
            '2e14d845ad9f7e93a5cfa9c419068a1ed32778974ff2596f9a4ec8704906eba0' `
            '6c10f97ecdc5c0fedecba458674996e32c1548ad5cf754f07f3d1779bef78d37' `
        --private-key "$Builder/keys/cc2_stock_private.pem" `
        --aes-key "$Builder/keys/cc2_aes_key_v1.bin" `
        --mksquashfs "$squash/mksquashfs" --unsquashfs "$squash/unsquashfs"
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "RC4 firmware build did not complete. Log: $log"
    }
    $sha = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    "$sha  $name" | Set-Content -LiteralPath "$target.sha256" -Encoding Ascii
    Write-Host "VERIFIED CC2 4.4.4 RC4: $target"
} finally {
    Stop-Transcript | Out-Null
}
