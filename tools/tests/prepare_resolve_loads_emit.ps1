# Extract the same seven production helpers as run_spv_output_tests.sh.
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('#include "xenia/gpu/spirv_shader_translator.h"')
$lines.Add('#include "xenia/gpu/spirv_compatibility.h"')
$lines.Add('#include "xenia/gpu/spirv/GLSL.std.450.h"')
$lines.Add('namespace xe { namespace gpu {')
$count = 0
foreach ($source in @('spirv_shader_translator.cc', 'spirv_shader_translator_rb.cc')) {
  $emit = $false
  foreach ($line in [IO.File]::ReadAllLines((Join-Path $root "src/xenia/gpu/$source"))) {
    if ($line -match '^spv::Id SpirvShaderTranslator::(PreClampedDepthTo20e4|UnclampedFloat32To7e3|PreClampedFloat32To7e3|Float7e3To32|Depth20e4To32|PWLGammaToLinear|LinearToPWLGamma)\(') {
      $emit = $true; ++$count
    }
    if ($emit) { $lines.Add($line) }
    if ($emit -and $line -match '^}') { $emit = $false }
  }
  if ($emit) { throw 'Incomplete production helper' }
}
if ($count -ne 7) { throw "Expected seven production helpers, found $count" }
$lines.Add('} }')
[IO.File]::WriteAllLines((Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) 'static_emitters.cc'), $lines)
