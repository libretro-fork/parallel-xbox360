param(
   [Parameter(Mandatory=$true)][string]$ParentExe,
   [Parameter(Mandatory=$true)][string]$CurrentExe,
   [Parameter(Mandatory=$true)][string]$Cache,
   [ValidateRange(1,100)][int]$Samples=5
)
# Optional Windows-only profiling of already built offline cache fixtures.
# No game/frontend is started; output bytes go to the Windows null device.
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
   throw 'This process CPU-time comparison requires Windows.'
}
$parentPath=(Resolve-Path -LiteralPath $ParentExe).Path
$currentPath=(Resolve-Path -LiteralPath $CurrentExe).Path
$cachePath=(Resolve-Path -LiteralPath $Cache).Path
$expected=0
for($sample=-1;$sample -lt $Samples;++$sample) {
   $order=if($sample % 2 -eq 0){@('parent','current')}else{@('current','parent')}
   foreach($revision in $order) {
      $info=[Diagnostics.ProcessStartInfo]::new()
      $info.FileName=if($revision -eq 'parent'){$parentPath}else{$currentPath}
      $info.WorkingDirectory=(Get-Location).Path
      $info.UseShellExecute=$false
      $info.CreateNoWindow=$true
      $info.RedirectStandardOutput=$true
      $info.RedirectStandardError=$true
      $info.EnvironmentVariables['SPV_GUEST_CACHE']=$cachePath
      $info.EnvironmentVariables['SPV_GUEST_OUTPUT']='NUL'
      $process=[Diagnostics.Process]::new()
      $process.StartInfo=$info
      try {
         $watch=[Diagnostics.Stopwatch]::StartNew()
         [void]$process.Start()
         $stdout=$process.StandardOutput.ReadToEndAsync()
         $stderr=$process.StandardError.ReadToEndAsync()
         if(!$process.WaitForExit(30000)) {
            throw "Offline test timed out; PID $($process.Id) remains live for inspection."
         }
         $watch.Stop()
         $output=$stdout.GetAwaiter().GetResult()
         $errorOutput=$stderr.GetAwaiter().GetResult()
         if($process.ExitCode -or $output -notmatch 'PASS: (\d+) cached guest translations') {
            throw "Offline test failed: $output $errorOutput"
         }
         $count=[int]$Matches[1]
         if(!$expected){$expected=$count}
         if(!$count -or $count -ne $expected){throw 'Translation counts differ.'}
         if($sample -ge 0) {
            [pscustomobject]@{
               revision=$revision
               sample=$sample
               translations=$count
               cpu_ms=$process.TotalProcessorTime.TotalMilliseconds
               wall_ms=$watch.Elapsed.TotalMilliseconds
            }
         }
      } finally {
         $process.Dispose()
      }
   }
}
