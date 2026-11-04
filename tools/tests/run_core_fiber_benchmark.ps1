param([string]$BinaryDirectory='build/tests/fiber-benchmark', [string]$OutputDirectory='build/tests/fiber-benchmark/results')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out=Join-Path $root $OutputDirectory
[IO.Directory]::CreateDirectory($out) | Out-Null
$rows=[Collections.Generic.List[object]]::new()
$cases=@(@('switch',2000000,1,'sequential'),@('switch',2000000,16,'sequential'),@('switch',2000000,128,'sequential'),@('switch',2000000,512,'sequential'),@('switch',2000000,16,'random'),@('switch',2000000,128,'random'),@('switch',2000000,512,'random'),@('create',1000,1,'sequential'),@('create',1000,128,'sequential'),@('cold_create',1,1,'sequential'))
for($round=0;$round -lt 12;$round++) {
 foreach($case in $cases) {
  $order=if($round%2){@('libco','boost')}else{@('boost','libco')}
  foreach($backend in $order) {
   $exe=Join-Path (Join-Path $root $BinaryDirectory) "fiber-workload-$backend.exe"
   $arguments=@([string]$case[0],[string]$case[1],[string]$case[2],[string]$case[3])
   $line=& $exe @arguments
   if($LASTEXITCODE -ne 0){throw "$backend $arguments failed: $LASTEXITCODE"}
   $p=([string]$line).Trim().Split(',')
   if($p.Length -ne 10){throw "Unexpected benchmark result: $line"}
   $rows.Add([pscustomobject]@{round=$round;backend=$backend;mode=$p[0];fibers=$p[1];order=$case[3];count=$p[2];seconds=$p[3];nanoseconds=$p[4];rss_before_kib=$p[5];rss_live_kib=$p[6];rss_after_kib=$p[7];commit_before_kib=$p[8];commit_live_kib=$p[9]})
  }
 }
 $rows | Export-Csv (Join-Path $out 'results.csv') -NoTypeInformation
}
$summary=foreach($group in ($rows | Group-Object backend,mode,fibers,order)) {
 $sample=$group.Group[0]
 $times=@($group.Group | ForEach-Object {[double]::Parse($_.nanoseconds,[Globalization.CultureInfo]::InvariantCulture)} | Sort-Object)
 [pscustomobject]@{backend=$sample.backend;mode=$sample.mode;fibers=$sample.fibers;order=$sample.order;samples=$times.Count;median_ns=($times[5]+$times[6])/2;min_ns=$times[0];max_ns=$times[-1]}
}
$summary | Export-Csv (Join-Path $out 'summary.csv') -NoTypeInformation
$summary | Format-Table -AutoSize