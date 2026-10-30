# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$AppDirectory,[switch]$BenchmarkOnly)
$ErrorActionPreference='Stop'
$app=(Resolve-Path -LiteralPath $AppDirectory).Path
$out=Join-Path $PSScriptRoot ('build/fg-async-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$results=@()
foreach($provider in @('3.1.6','4.0.2')){
 $variants=if($BenchmarkOnly){@('sync-a','async','sync-b')}else{@('sync','async')}
 foreach($variant in $variants){
  $target=Join-Path $out ($provider+'-'+$variant)
  $options=@((Join-Path $app 'runtime'),$target,$provider,'--srgb','--inject-gaps')
  if($variant -eq 'async'){$options+='--async'}
  if($BenchmarkOnly){$options+='--benchmark'}
  & (Join-Path $app 'FrameGenCheck.exe') @options *> ($target+'.txt')
  if($LASTEXITCODE -ne 0){Get-Content -LiteralPath ($target+'.txt') -Tail 15;throw "FG async comparison failed: $provider / $variant"}
  if($BenchmarkOnly){$result=Get-Content -LiteralPath (Join-Path $target 'async-benchmark.json') -Raw | ConvertFrom-Json;$result|Add-Member -NotePropertyName provider -NotePropertyValue $provider;$result|Add-Member -NotePropertyName run -NotePropertyValue $variant;$results+=$result}
  Get-Content -LiteralPath ($target+'.txt') -Tail 4
 }
}
if($BenchmarkOnly){$results|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $out 'SUMMARY.json');$results|Select-Object provider,run,seconds,sourceThroughput,safeHolds|Format-Table}
Write-Output "Evidence: $out. Synthetic FIFO/upload fixture; no simulator FPS or scanout claim. App defaults stay synchronous."
