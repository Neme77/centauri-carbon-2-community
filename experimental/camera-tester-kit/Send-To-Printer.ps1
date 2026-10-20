param([string]$PrinterIP)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $PrinterIP) { $PrinterIP = Read-Host 'Printer IPv4 address / Indirizzo IPv4 stampante' }
$parsedAddress = $null
if ($PrinterIP -notmatch '^(?:\d{1,3}\.){3}\d{1,3}$' -or
    -not [Net.IPAddress]::TryParse($PrinterIP, [ref]$parsedAddress) -or
    $parsedAddress.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'Invalid printer IPv4 address / Indirizzo IPv4 non valido.'
}
$PrinterIP = $parsedAddress.ToString()
$expected = @{
    'cc2-camera-recovery-r2.tar.gz' = 'c803ffcfe374064ef3b04d32d2dbc690adba924bcf0652ef1c6792682c196c04'
    'cc2-camera-tester-report.sh' = '__REPORT_SHA256__'
}
$files = @()
foreach ($name in $expected.Keys) {
    $path = Join-Path $PSScriptRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $name" }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected[$name]) {
        throw "Checksum mismatch: $name"
    }
    $files += $path
}
$scp = Get-Command scp.exe -ErrorAction Stop
$destination = 'root@' + $PrinterIP + ':/tmp/'
Write-Host 'Copy only: no service will be restarted / Solo copia: nessun servizio verra riavviato.'
& $scp.Source '-O' @files $destination
if ($LASTEXITCODE -ne 0) { throw 'Copy failed / Copia non riuscita.' }
Write-Host 'Copy complete. Follow README.en.md or README.it.md for SSH check and activation.'
Write-Host 'Copia completata. Segui README.it.md per controllo e attivazione tramite SSH.'
