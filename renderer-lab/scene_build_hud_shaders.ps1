param([Parameter(Mandatory=$true)][string]$Fxc, [ValidateSet('Hud','Movie','Composite','Shadow','Frame')][string]$Pass = 'Hud')
$ErrorActionPreference = 'Stop'
$stem = if ($Pass -eq 'Composite') { 'scene_movie_composite' } else { 'scene_'+$Pass.ToLowerInvariant() }
$shaderPath = Join-Path $PSScriptRoot ($stem+'.hlsl')
$headerPath = Join-Path $PSScriptRoot ($stem+'_shaders.inc')
$stageOutput = [IO.Path]::GetTempFileName()
try {
    $source = [Text.StringBuilder]::new()
    [void]$source.AppendLine("// Generated from $stem.hlsl by scene_build_hud_shaders.ps1.")
    $stages = @(@(($Pass + 'Root'), 'rootsig_1_0'), @(($Pass + 'Vertex'), 'vs_5_0'), @(($Pass + 'Pixel'), 'ps_5_0'))
    if ($Pass -eq 'Movie') { $stages += ,@('MovieLayer','ps_5_0') }
    if ($Pass -eq 'Shadow') { $stages += ,@('ShadowMarker','ps_5_0') }
    if ($Pass -eq 'Composite') {
        $stages += ,@('CompositePixel','ps_5_0','MS')
        $stages += ,@('ExportSamples','ps_5_0','','EXPORT_SAMPLES=1')
        $stages += ,@('CompositeFilm','ps_5_0','','UNORM16_LAYER=1')
        $stages += ,@('CompositeFilm','ps_5_0','MS','UNORM16_LAYER=1','MULTISAMPLE=1')
    }
    foreach ($stage in $stages) {
        $defines = @()
        if ($stage.Count -gt 3) { for ($parameter=3; $parameter -lt $stage.Count; ++$parameter) { $defines += @('/D',$stage[$parameter]) } }
        elseif ($stage.Count -gt 2) { $defines = @('/D','MULTISAMPLE=1') }
        & $Fxc /nologo /WX /O3 /Qstrip_reflect /Qstrip_debug @defines /T $stage[1] /E $stage[0] /Fo $stageOutput $shaderPath
        if ($LASTEXITCODE) { throw "Could not compile $($stage[0])." }
        $bytes = [IO.File]::ReadAllBytes($stageOutput)
        [void]$source.AppendLine("alignas(4) inline constexpr unsigned char $($stage[0])$($stage[2])[] = {")
        for ($i = 0; $i -lt $bytes.Length; $i += 16) {
            $end = [Math]::Min($i + 15, $bytes.Length - 1)
            $values = ($bytes[$i..$end] | ForEach-Object { '0x{0:x2}' -f $_ }) -join ', '
            [void]$source.AppendLine("    $values,")
        }
        [void]$source.AppendLine('};')
    }
    [IO.File]::WriteAllText($headerPath, $source.ToString().Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
} finally {
    [IO.File]::Delete($stageOutput)
}
