param([string]$Dxc = 'dxc.exe', [string]$Validator = 'spirv-val.exe')
$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot 'scene_movie_vulkan.hlsl'
$spv = [IO.Path]::GetTempFileName()
try {
    $output = [Text.StringBuilder]::new()
    [void]$output.AppendLine('// Generated from scene_movie_vulkan.hlsl by scene_build_movie_spirv.ps1.')
    foreach ($stage in @(@('MovieVertex','vs_6_0'), @('VulkanFilm','ps_6_0'))) {
        $options = @('-spirv','-fspv-target-env=vulkan1.2','-fvk-use-dx-layout','-fvk-b-shift',0,0,'-fvk-t-shift',1,0,'-fvk-s-shift',2,0)
        if ($stage[1] -eq 'vs_6_0') { $options += '-fvk-invert-y' }
        & $Dxc @options -E $stage[0] -T $stage[1] -O3 -Fo $spv $src
        if ($LASTEXITCODE) { throw "Failed to compile $($stage[0])." }
        & $Validator --target-env vulkan1.2 $spv
        if ($LASTEXITCODE) { throw "Invalid SPIR-V in $($stage[0])." }
        $bytes = [IO.File]::ReadAllBytes($spv)
        [void]$output.AppendLine("inline constexpr uint32_t $($stage[0])Spv[] = {")
        for ($i=0; $i -lt $bytes.Length; $i+=32) {
            $line = for ($j=$i; $j -lt [Math]::Min($i+32,$bytes.Length); $j+=4) {
                '0x{0:x8}' -f [BitConverter]::ToUInt32($bytes,$j)
            }
            [void]$output.AppendLine('    '+($line -join ', ')+',')
        }
        [void]$output.AppendLine('};')
    }
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'scene_movie_vulkan_shaders.inc'),$output.ToString().Replace("`r`n","`n"),[Text.UTF8Encoding]::new($false))
} finally { [IO.File]::Delete($spv) }
