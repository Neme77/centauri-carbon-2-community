function Read-CleanupArchive {
 param([Diagnostics.ProcessStartInfo]$Info,[string]$Request,[string]$Destination,[int]$MaxRequestBytes=4096)
 $Info.UseShellExecute=$false;$Info.CreateNoWindow=$false
 $Info.RedirectStandardInput=$true;$Info.RedirectStandardOutput=$true;$Info.RedirectStandardError=$false
 $process=New-Object Diagnostics.Process;$process.StartInfo=$Info
 $file=$null;$started=$false
 try {
  $data=[Text.Encoding]::UTF8.GetBytes($Request.Replace("`r",''))
  if($data.Length -gt $MaxRequestBytes){throw 'Archive request exceeds input bound.'}
  $started=$process.Start();if(-not $started){throw 'Cannot start archive reader.'}
  $process.StandardInput.BaseStream.Write($data,0,$data.Length)
  $process.StandardInput.BaseStream.Close()
  $file=[IO.File]::Open($Destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  $buffer=New-Object byte[] 65536
  [long]$total=0;$clock=[Diagnostics.Stopwatch]::StartNew();$next=5
  while($true){
   $task=$process.StandardOutput.BaseStream.ReadAsync($buffer,0,$buffer.Length)
   if(-not $task.Wait(60000)){throw 'No archive data for 60 seconds; transfer stopped without cleanup.'}
   $count=$task.GetAwaiter().GetResult();if($count -eq 0){break}
   $total+=$count;if($total -gt 17179869184){throw 'Archive exceeds 16 GiB bound.'}
   $file.Write($buffer,0,$count)
   if($clock.Elapsed.TotalSeconds -ge $next){Write-Host ('Copied {0:N1} MiB...' -f ($total/1MB));$next=$clock.Elapsed.TotalSeconds+5}
  }
  $file.Flush();$file.Dispose();$file=$null
  $process.WaitForExit();if($process.ExitCode){throw ('Archive SSH failed: '+$process.ExitCode)}
  return $total
 } finally {
  if($file){$file.Dispose()}
  if($started -and -not $process.HasExited){$process.Kill();$process.WaitForExit()}
  $process.Dispose()
 }
}
