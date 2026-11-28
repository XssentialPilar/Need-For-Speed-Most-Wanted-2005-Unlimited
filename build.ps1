param([string]$OutputFile = (Join-Path $PSScriptRoot 'd3d9.dll'), [string]$XboxGlobal,
      [string]$XboxMaterials, [string]$XboxBarriers, [string]$XboxPlatformLights, [string]$XboxWorldGlobal)
$ErrorActionPreference = 'Stop'
$toolDir = Split-Path (Get-Command i686-w64-mingw32-windres.exe).Source
$resource = Join-Path ([IO.Path]::GetTempPath()) ("unlimited-" + [guid]::NewGuid() + ".o")
$script = Join-Path $PSScriptRoot 'Unlimited.rc'
$generated = [IO.Path]::ChangeExtension($resource, '.rc')
$effects = [IO.Path]::ChangeExtension($resource, '.bin')
try {
    $extra = @()
    if ($XboxGlobal -or $XboxBarriers) {
        $effectSource = if ($XboxGlobal) { $XboxGlobal } else { $XboxBarriers }
        $effectArgs = @()
        if ($XboxGlobal) { $effectArgs += '--xbox-effects' }
        if ($XboxBarriers) { $effectArgs += @('--xbox-barriers', $XboxBarriers) }
        if ($XboxWorldGlobal) { $effectArgs += @('--world-global', $XboxWorldGlobal) }
        & py.exe -3 (Join-Path $PSScriptRoot 'installer/compile_textures.py') $effectSource $effects @effectArgs
        if ($LASTEXITCODE) { throw "Console texture compilation failed ($LASTEXITCODE)" }
        $extra += '201 RCDATA "{0}"' -f $effects.Replace('\','/')
    }
    if ($XboxMaterials) {
        $materialPath = (Resolve-Path -LiteralPath $XboxMaterials).Path.Replace('\','/')
        $extra += '202 RCDATA "{0}"' -f $materialPath
    }
    if ($XboxPlatformLights) {
        $platformLightPath = (Resolve-Path -LiteralPath $XboxPlatformLights).Path.Replace('\','/')
        $extra += '203 RCDATA "{0}"' -f $platformLightPath
    }
    if ($extra.Count) {
        [IO.File]::WriteAllText($generated, ([IO.File]::ReadAllText($script) + "`r`n" + ($extra -join "`r`n") + "`r`n"))
        $script = $generated
    }
    & (Join-Path $toolDir 'i686-w64-mingw32-windres.exe') -i $script -O coff -o $resource
    if ($LASTEXITCODE) { throw "Resource compilation failed ($LASTEXITCODE)" }
    $vkInclude = Join-Path $env:VULKAN_SDK 'Include'
    $splitScreen = Join-Path $PSScriptRoot 'third-party/split-screen/main.cpp'
    & (Join-Path $toolDir 'i686-w64-mingw32-clang++.exe') -std=c++17 -Wall -Wextra -Werror -O2 -static -shared -idirafter $vkInclude (Join-Path $PSScriptRoot 'vram_probe.cpp') (Join-Path $PSScriptRoot 'nfs_discx.cpp') $splitScreen (Join-Path $PSScriptRoot 'd3d9.def') $resource -lgdi32 -lcomctl32 -lole32 -lwindowscodecs -luuid -o $OutputFile
    if ($LASTEXITCODE) { throw "DLL compilation failed ($LASTEXITCODE)" }
} finally {
    foreach ($file in @($resource, $generated, $effects)) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
}
