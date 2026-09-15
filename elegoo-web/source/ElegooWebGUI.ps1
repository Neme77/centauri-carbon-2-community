Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$base = Split-Path -Parent $MyInvocation.MyCommand.Path
$configPath = Join-Path $base "config.json"
$serverPath = Join-Path $base "ElegooWebServer.exe"

try {Add-Type -Path (Join-Path $base "ElegooWebAsync.cs") -ErrorAction Stop}
catch {[void][Windows.Forms.MessageBox]::Show("Impossibile caricare il modulo di rete. Estrai tutti i file del pacchetto e avvia Diagnostica_GUI.cmd per i dettagli.","ELEGOO Web");throw}
$script:configWarning=""
$script:probeTask=$null
$script:lastProbe=[DateTime]::MinValue
$script:restartPending=$false
$script:restartDeadline=[DateTime]::MinValue
$script:serverReady=$false
$script:qrProcess=$null
$script:qrRequestedUrl=""
$script:qrStarted=[DateTime]::MinValue
function Load-Config {
  if(Test-Path -LiteralPath $configPath){
    try {
      $cfg=Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
      if($null -eq $cfg -or $null -eq $cfg.printer_ip){throw 'Configurazione incompleta'}
      return $cfg
    } catch {$script:configWarning="Configurazione non leggibile. Reinserisci i dati e salva."}
  }
  return [pscustomobject]@{printer_ip="";username="elegoo";access_code="";serial="";language="it";port=8888}
}
function Save-Config($ip,$user,$code,$sn,$lang){
  $cfg=[ordered]@{printer_ip=$ip;username=$user;access_code=$code;serial=$sn;language=$lang;port=8888}
  $temp=$configPath+"."+[Guid]::NewGuid().ToString('N')+".tmp"
  try {
    [IO.File]::WriteAllText($temp,($cfg | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
    if([IO.File]::Exists($configPath)){[IO.File]::Replace($temp,$configPath,$null)}
    else {[IO.File]::Move($temp,$configPath)}
  } finally {if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
  $script:c=[pscustomobject]$cfg
}
function Get-OwnServer {
  foreach($process in @(Get-Process -Name ElegooWebServer -ErrorAction SilentlyContinue)){
    try {if([string]::Equals($process.MainModule.FileName,$serverPath,[StringComparison]::OrdinalIgnoreCase)){return $process}} catch {}
  }
  return $null
}
function Server-Running { return $null -ne (Get-OwnServer) }
function Start-Server {
  if(Server-Running){return}
  if(-not(Test-Path -LiteralPath $configPath)){throw 'Configura prima la stampante.'}
  # Do not start a second copy on an already occupied port.
  $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Any,8888)
  try {$listener.Start()} catch {throw 'Porta 8888 occupata. Arresta il server della precedente copia prima di avviare questa.'}
  finally {$listener.Stop()}
  Start-Process -FilePath $serverPath -WorkingDirectory $base -WindowStyle Hidden -ErrorAction Stop
  $script:lastProbe=[DateTime]::MinValue
}
function Stop-Server {
  $process=Get-OwnServer
  if($null -ne $process){$process.Kill()}
  $script:serverReady=$false
  $script:lastProbe=[DateTime]::MinValue
}
function Show-Error($message){[void][Windows.Forms.MessageBox]::Show($message,"ELEGOO Web",'OK','Warning')}

$c=Load-Config

# ---------- ELEGOO Web v4.1 Polished GUI ----------
$form=New-Object Windows.Forms.Form
$form.Text="ELEGOO Web 4.1.1"
$form.Icon=[Drawing.Icon]::new((Join-Path $base "favicon.ico"))
$form.Size=[Drawing.Size]::new(720,625)
$form.StartPosition="CenterScreen"
$form.FormBorderStyle="FixedSingle"
$form.MaximizeBox=$false
$form.Font=[Drawing.Font]::new("Segoe UI",10)
$form.BackColor=[Drawing.Color]::FromArgb(242,244,247)

# Header
$header=New-Object Windows.Forms.Panel
$header.Location=[Drawing.Point]::new(0,0);$header.Size=[Drawing.Size]::new(720,96)
$header.BackColor=[Drawing.Color]::FromArgb(31,35,42);$form.Controls.Add($header)

$title=New-Object Windows.Forms.Label
$title.Text="ELEGOO WEB";$title.ForeColor=[Drawing.Color]::White
$title.Font=[Drawing.Font]::new("Segoe UI Semibold",22);$title.AutoSize=$true
$title.Location=[Drawing.Point]::new(28,16);$header.Controls.Add($title)

$subtitle=New-Object Windows.Forms.Label
$subtitle.Text="Centauri Carbon 2  •  Local Web Control"
$subtitle.ForeColor=[Drawing.Color]::FromArgb(190,195,202)
$subtitle.BackColor=[Drawing.Color]::FromArgb(31,35,42)
$subtitle.Font=[Drawing.Font]::new("Segoe UI",9.5,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Point)
$subtitle.AutoSize=$false
$subtitle.Location=[Drawing.Point]::new(31,48)
$subtitle.Size=[Drawing.Size]::new(440,40)
$subtitle.TextAlign=[Drawing.ContentAlignment]::MiddleLeft
$subtitle.UseCompatibleTextRendering=$false
$header.Controls.Add($subtitle)

$version=New-Object Windows.Forms.Label
$version.Text="v4.1.1";$version.ForeColor=[Drawing.Color]::FromArgb(190,195,202)
$version.AutoSize=$true;$version.Location=[Drawing.Point]::new(650,27);$header.Controls.Add($version)

# Status card
$card=New-Object Windows.Forms.Panel
$card.Location=[Drawing.Point]::new(25,116);$card.Size=[Drawing.Size]::new(655,190)
$card.BackColor=[Drawing.Color]::White;$card.BorderStyle="FixedSingle";$form.Controls.Add($card)

$cardTitle=New-Object Windows.Forms.Label
$cardTitle.Text="STATO";$cardTitle.Font=[Drawing.Font]::new("Segoe UI Semibold",11)
$cardTitle.Location=[Drawing.Point]::new(20,15);$cardTitle.AutoSize=$true;$cardTitle.UseCompatibleTextRendering=$true;$card.Controls.Add($cardTitle)

$printerName=New-Object Windows.Forms.Label
$printerName.Text="Centauri Carbon 2";$printerName.Font=[Drawing.Font]::new("Segoe UI Semibold",14)
$printerName.Location=[Drawing.Point]::new(20,48);$printerName.AutoSize=$true;$printerName.UseCompatibleTextRendering=$true;$card.Controls.Add($printerName)

$printerIP=New-Object Windows.Forms.Label
$printerIP.Location=[Drawing.Point]::new(22,80);$printerIP.Size=[Drawing.Size]::new(300,24)
$printerIP.ForeColor=[Drawing.Color]::DimGray;$card.Controls.Add($printerIP)

$serverDot=New-Object Windows.Forms.Label
$serverDot.Font=[Drawing.Font]::new("Segoe UI",13);$serverDot.Location=[Drawing.Point]::new(285,45)
$serverDot.Size=[Drawing.Size]::new(200,28);$card.Controls.Add($serverDot)

$printerDot=New-Object Windows.Forms.Label
$printerDot.Font=[Drawing.Font]::new("Segoe UI",8.5);$printerDot.Location=[Drawing.Point]::new(330,78)
$printerDot.Size=[Drawing.Size]::new(155,28);$printerDot.TextAlign=[Drawing.ContentAlignment]::MiddleLeft;$card.Controls.Add($printerDot)

$mobileLbl=New-Object Windows.Forms.Label
$mobileLbl.Text="ACCESSO MOBILE";$mobileLbl.Font=[Drawing.Font]::new("Segoe UI Semibold",9)
$mobileLbl.Location=[Drawing.Point]::new(20,136);$mobileLbl.AutoSize=$true;$card.Controls.Add($mobileLbl)

$link=New-Object Windows.Forms.TextBox
$link.Location=[Drawing.Point]::new(145,132);$link.Size=[Drawing.Size]::new(235,27)
$link.ReadOnly=$true;$link.BackColor=[Drawing.Color]::White;$card.Controls.Add($link)

$copy=New-Object Windows.Forms.Button
$copy.Text="COPIA";$copy.Location=[Drawing.Point]::new(390,130);$copy.Size=[Drawing.Size]::new(90,31)
$card.Controls.Add($copy)

$qrTitle=New-Object Windows.Forms.Label
$qrTitle.Text="QR MOBILE";$qrTitle.Font=[Drawing.Font]::new("Segoe UI Semibold",9)
$qrTitle.Location=[Drawing.Point]::new(525,12);$qrTitle.AutoSize=$true;$qrTitle.UseCompatibleTextRendering=$true;$card.Controls.Add($qrTitle)

$qrBox=New-Object Windows.Forms.PictureBox
$qrBox.Location=[Drawing.Point]::new(505,35);$qrBox.Size=[Drawing.Size]::new(125,125)
$qrBox.SizeMode="Zoom";$qrBox.BackColor=[Drawing.Color]::White
$qrBox.BorderStyle="FixedSingle";$card.Controls.Add($qrBox)

$qrHint=New-Object Windows.Forms.Label
$qrHint.Text="Scansiona dal telefono";$qrHint.Font=[Drawing.Font]::new("Segoe UI",8)
$qrHint.ForeColor=[Drawing.Color]::DimGray;$qrHint.Location=[Drawing.Point]::new(500,164)
$qrHint.Size=[Drawing.Size]::new(140,18);$qrHint.TextAlign="MiddleCenter";$card.Controls.Add($qrHint)

$qrHelper=Join-Path $base "QRHelper.exe"
$qrPath=Join-Path ([IO.Path]::GetTempPath()) ("ElegooWeb_"+[Guid]::NewGuid().ToString('N')+".png")
$script:lastQrUrl=""
function Update-QR([string]$url){
 if([string]::IsNullOrWhiteSpace($url) -or $url -notlike "http*"){return}
 if($script:lastQrUrl -eq $url -and $qrBox.Image){return}
 if($null -ne $script:qrProcess){return}
 if(-not(Test-Path -LiteralPath $qrHelper)){return}
 try {
   $script:qrRequestedUrl=$url
   $script:qrStarted=[DateTime]::UtcNow
   $script:qrProcess=Start-Process -FilePath $qrHelper -ArgumentList ('"'+$url+'" "'+$qrPath+'"') -WindowStyle Hidden -PassThru -ErrorAction Stop
 } catch {$qrHint.Text="QR non disponibile"}
}
function Poll-QR {
 if($null -eq $script:qrProcess){return}
 if(-not $script:qrProcess.HasExited){
   if(([DateTime]::UtcNow-$script:qrStarted).TotalSeconds -gt 5){try {$script:qrProcess.Kill()}catch{};$qrHint.Text="QR: tempo scaduto"}
   return
 }
 try {
   if($script:qrProcess.ExitCode -eq 0 -and (Test-Path -LiteralPath $qrPath)){
     $bytes=[IO.File]::ReadAllBytes($qrPath)
     $ms=New-Object IO.MemoryStream(,$bytes)
     try {$img=[Drawing.Image]::FromStream($ms);try {$clone=New-Object Drawing.Bitmap($img)} finally {$img.Dispose()}}
     finally {$ms.Dispose()}
     if($qrBox.Image){$qrBox.Image.Dispose()}
     $qrBox.Image=$clone
     $script:lastQrUrl=$script:qrRequestedUrl
     $qrHint.Text="Scansiona dal telefono"
   } else {$qrHint.Text="QR non disponibile"}
 } catch {$qrHint.Text="QR non disponibile"}
 finally {
   if($script:qrProcess.HasExited){$script:qrProcess.Dispose();$script:qrProcess=$null}
 }
}

# Main action buttons
$open=New-Object Windows.Forms.Button
$open.Text="APRI WEB UI";$open.Font=[Drawing.Font]::new("Segoe UI Semibold",11)
$open.Location=[Drawing.Point]::new(25,326);$open.Size=[Drawing.Size]::new(210,48);$form.Controls.Add($open)

$toggle=New-Object Windows.Forms.Button
$toggle.Location=[Drawing.Point]::new(250,326);$toggle.Size=[Drawing.Size]::new(205,48);$form.Controls.Add($toggle)

$configBtn=New-Object Windows.Forms.Button
$configBtn.Text="CONFIGURA";$configBtn.Location=[Drawing.Point]::new(470,326)
$configBtn.Size=[Drawing.Size]::new(210,48);$form.Controls.Add($configBtn)

# Configuration panel - hidden during normal use
$configPanel=New-Object Windows.Forms.Panel
$configPanel.Location=[Drawing.Point]::new(25,393);$configPanel.Size=[Drawing.Size]::new(655,150)
$configPanel.BackColor=[Drawing.Color]::White;$configPanel.BorderStyle="FixedSingle";$form.Controls.Add($configPanel)

function Add-CfgField($label,$x,$y,$width,$text,$password=$false){
 $l=New-Object Windows.Forms.Label;$l.Text=$label;$l.Location=[Drawing.Point]::new($x,$y)
 $l.Size=[Drawing.Size]::new($width,22);$configPanel.Controls.Add($l)
 $t=New-Object Windows.Forms.TextBox;$t.Location=[Drawing.Point]::new($x,($y+24))
 $t.Size=[Drawing.Size]::new($width,27);$t.Text=$text
 if($password){$t.UseSystemPasswordChar=$true};$configPanel.Controls.Add($t);return $t
}
$ip=Add-CfgField "IP stampante" 18 14 140 $c.printer_ip
$user=Add-CfgField "Username" 170 14 130 $c.username
$code=Add-CfgField "Access Code" 312 14 145 $c.access_code $true
$sn=Add-CfgField "Serial Number" 469 14 165 $c.serial

$langLabel=New-Object Windows.Forms.Label
$langLabel.Text="Lingua";$langLabel.Location=[Drawing.Point]::new(18,76);$langLabel.Size=[Drawing.Size]::new(140,20)
$langLabel.BackColor=[Drawing.Color]::White
$configPanel.Controls.Add($langLabel)
$lang=New-Object Windows.Forms.ComboBox
$lang.Location=[Drawing.Point]::new(18,98);$lang.Size=[Drawing.Size]::new(140,28);$lang.DropDownStyle="DropDownList"
$lang.FlatStyle="Standard"
[void]$lang.Items.Add("Italiano");[void]$lang.Items.Add("English")
$lang.SelectedIndex=if($c.language -eq "en"){1}else{0};$configPanel.Controls.Add($lang)

$showCode=New-Object Windows.Forms.CheckBox
$showCode.Text="Mostra Access Code";$showCode.Location=[Drawing.Point]::new(180,97)
$showCode.Size=[Drawing.Size]::new(175,28);$showCode.BackColor=[Drawing.Color]::White
$configPanel.Controls.Add($showCode)

$save=New-Object Windows.Forms.Button
$save.Text="SALVA E AVVIA";$save.Location=[Drawing.Point]::new(468,88)
$save.Size=[Drawing.Size]::new(166,38);$configPanel.Controls.Add($save)

$hint=New-Object Windows.Forms.Label
$hint.Text="Configurazione salvata localmente sul PC."
$hint.ForeColor=[Drawing.Color]::DimGray;$hint.Location=[Drawing.Point]::new(180,126)
$hint.Size=[Drawing.Size]::new(275,18);$hint.BackColor=[Drawing.Color]::White
$configPanel.Controls.Add($hint)

$footer=New-Object Windows.Forms.Label
$footer.Text="Server locale • Nessun cloud richiesto per l'accesso LAN"
$footer.ForeColor=[Drawing.Color]::Gray;$footer.AutoSize=$true
$footer.Location=[Drawing.Point]::new(26,558);$form.Controls.Add($footer)

function Refresh-PolishedStatus {
 Poll-QR
 if($script:restartPending){
   if(-not(Server-Running)){
     $script:restartPending=$false
     try {Start-Server} catch {Show-Error $_.Exception.Message}
   } elseif([DateTime]::UtcNow -gt $script:restartDeadline){
     $script:restartPending=$false;Show-Error "Il server non si e' arrestato. Riprova."
   }
   $save.Enabled=-not $script:restartPending
   $toggle.Enabled=-not $script:restartPending
   $open.Enabled=-not $script:restartPending
 }
 if($null -ne $script:probeTask -and $script:probeTask.IsCompleted){
   if($script:probeTask.Status -eq 'RanToCompletion'){
     $result=$script:probeTask.Result
     $script:serverReady=$result.HttpReady -and (Server-Running)
     if($result.LanIP){
       $link.Text="http://"+$result.LanIP+":8888/"
       Update-QR $link.Text
     } else {
       $link.Text="Rete LAN non rilevata"
       if($qrBox.Image){$qrBox.Image.Dispose();$qrBox.Image=$null}
       $script:lastQrUrl=""
     }
     if($result.PrinterReachable){$printerDot.Text="● PORTA 9001 OK";$printerDot.ForeColor=[Drawing.Color]::ForestGreen}
     else {$printerDot.Text="● NON RAGGIUNGIBILE";$printerDot.ForeColor=[Drawing.Color]::DarkOrange}
   }
   $script:probeTask=$null
 }
 if($null -eq $script:probeTask -and ([DateTime]::UtcNow-$script:lastProbe).TotalSeconds -ge 5){
   $script:lastProbe=[DateTime]::UtcNow
   $script:probeTask=[ElegooWebAsync]::Probe([string]$c.printer_ip)
 }
 $printerIP.Text=if([string]::IsNullOrWhiteSpace($c.printer_ip)){"Stampante non configurata"}else{"IP: "+$c.printer_ip}
 $running=Server-Running
 if($running -and $script:serverReady){$serverDot.Text="● SERVER ATTIVO";$serverDot.ForeColor=[Drawing.Color]::ForestGreen}
 elseif($running){$serverDot.Text="● SERVER IN AVVIO";$serverDot.ForeColor=[Drawing.Color]::DarkOrange}
 else {$serverDot.Text="● SERVER ARRESTATO";$serverDot.ForeColor=[Drawing.Color]::Firebrick}
 $toggle.Text=if($running){"ARRESTA SERVER"}else{"AVVIA SERVER"}
}
function Set-ConfigVisible($visible){
 $configPanel.Visible=$visible
 if($visible){$configBtn.Text="CHIUDI CONFIGURAZIONE"}else{$configBtn.Text="CONFIGURA"}
}
$showCode.Add_CheckedChanged({$code.UseSystemPasswordChar=-not $showCode.Checked})
$configBtn.Add_Click({Set-ConfigVisible(-not $configPanel.Visible)})
$copy.Add_Click({if($link.Text -like "http*"){[Windows.Forms.Clipboard]::SetText($link.Text)}})

$save.Add_Click({
 $address=$null
 if(-not [Net.IPAddress]::TryParse($ip.Text.Trim(),[ref]$address) -or $address.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork){Show-Error "Inserisci un indirizzo IPv4 valido.";return}
 if([string]::IsNullOrWhiteSpace($user.Text) -or [string]::IsNullOrWhiteSpace($code.Text) -or [string]::IsNullOrWhiteSpace($sn.Text)){Show-Error "Completa Username, Access Code e Serial Number.";return}
 $lg=if($lang.SelectedIndex -eq 1){"en"}else{"it"}
 try {
   Save-Config $ip.Text.Trim() $user.Text.Trim() $code.Text $sn.Text.Trim() $lg
   Stop-Server
   $script:restartPending=$true
   $script:restartDeadline=[DateTime]::UtcNow.AddSeconds(5)
   $save.Enabled=$false;$toggle.Enabled=$false;$open.Enabled=$false
   Set-ConfigVisible $false
 } catch {Show-Error $_.Exception.Message}
})
$open.Add_Click({
 try {
   $wasRunning=Server-Running
   Start-Server
   # The original server opens the browser itself on first launch.
   if($wasRunning){Start-Process "http://localhost:8888/"}
 } catch {Show-Error $_.Exception.Message}
})
$toggle.Add_Click({
 try {if(Server-Running){Stop-Server}else{Start-Server}} catch {Show-Error $_.Exception.Message}
})
# Fast polling consumes completed work only. Network and QR waits run outside UI.
$timer=New-Object Windows.Forms.Timer
$timer.Interval=250
$timer.Add_Tick({Refresh-PolishedStatus})
$form.Add_Shown({
 $first=[string]::IsNullOrWhiteSpace($c.printer_ip) -or [string]::IsNullOrWhiteSpace($c.serial) -or [string]::IsNullOrWhiteSpace($c.access_code)
 Set-ConfigVisible $first
 Refresh-PolishedStatus
 $timer.Start()
 if($first){$ip.Focus()}
 if($script:configWarning){Show-Error $script:configWarning}
})
$form.Add_FormClosed({
 $timer.Stop();$timer.Dispose()
 if($qrBox.Image){$qrBox.Image.Dispose()}
 if($null -ne $script:qrProcess){try {if(-not $script:qrProcess.HasExited){$script:qrProcess.Kill()}}catch{};$script:qrProcess.Dispose()}
 try {if([IO.File]::Exists($qrPath)){[IO.File]::Delete($qrPath)}}catch{}
 # Keep server available to mobile clients, as in v4.1.
})
[void]$form.ShowDialog()
