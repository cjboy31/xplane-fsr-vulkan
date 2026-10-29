param(
 [Parameter(Mandatory)][string]$BaselineSingle,
 [Parameter(Mandatory)][string]$BaselineMsaa,
 [ValidateRange(3,15)][int]$Runs=5,
 [string]$OutputPath=(Join-Path $PSScriptRoot 'build\motion-performance.txt')
)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
$baseline=@{1=(Resolve-Path -LiteralPath $BaselineSingle).Path;4=(Resolve-Path -LiteralPath $BaselineMsaa).Path}
if(Test-Path -LiteralPath $OutputPath){throw 'Choose a fresh benchmark output file'}
$saved=@{}
foreach($key in @('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE')){
 $saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')
 [Environment]::SetEnvironmentVariable($key,$null,'Process')
}
try {
 foreach($samples in @(1,4)){
  foreach($size in @(@(2560,1440),@(3840,2160))){
   foreach($run in 0..($Runs-1)){
    # Alternate order to expose warm-up/order bias instead of always favoring one shader.
    $variants=if($run%2){@('optimized','baseline')}else{@('baseline','optimized')}
    foreach($variant in $variants){
     $current=if($samples -eq 1){'temporal-stability.spv'}else{'temporal-msaa.spv'}
     $shader=if($variant -eq 'baseline'){$baseline[$samples]}else{Join-Path $build $current}
     "$variant run=$run" | Add-Content -LiteralPath $OutputPath
     $args=@($shader)
     if($samples -gt 1){$args+=@($samples,(Join-Path $build 'msaa-input.spv'))}
     $args+=@($size[0],$size[1],'--benchmark')
     & (Join-Path $build 'temporal_tests.exe') @args >> $OutputPath
     if($LASTEXITCODE -ne 0){throw "Motion benchmark failed: $variant / $samples"}
    }
   }
  }
 }
}finally{foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}}
Write-Output 'Paired GPU timestamps recorded. Compare per-case medians; these are packing-pass costs, not live-flight FPS.'
