# Native renderer tests

The standalone test renders a decoded BGRA frame with custom shaders in DirectX 11, DirectX 12, and Vulkan. It preserves the aspect ratio, fills unused space with black, and compares each RGBA pixel returned by the GPU with the expected result.

All 24 tests passed in a 32-bit process. Each API ran four cases on the RTX 5050 and four on the Radeon 760M. Cases include 4:3, ultrawide, portrait, and a padded 1920 × 1080 input frame rendered to a 1920 × 1200 target. The test also rejects truncated frames.

## Reproduce the test

Run these commands from `renderer-lab` with PowerShell, 32-bit LLVM-MinGW, and `VULKAN_SDK` set to the SDK directory.

```powershell
$renderSources = (Get-Location).Path
$renderScratch = Join-Path $env:TEMP 'unlimited-renderer-lab'
[void][IO.Directory]::CreateDirectory($renderScratch)
glslangValidator -V (Join-Path $renderSources 'movie.vert') -o (Join-Path $renderScratch 'movie-vs.spv')
if ($LASTEXITCODE -ne 0) { throw 'Vertex shader build failed' }
glslangValidator -V (Join-Path $renderSources 'movie.frag') -o (Join-Path $renderScratch 'movie-fs.spv')
if ($LASTEXITCODE -ne 0) { throw 'Pixel shader build failed' }
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -I (Join-Path $env:VULKAN_SDK 'Include') (Join-Path $renderSources 'test_native_movie.cpp') -ld3d11 -ld3d12 -ld3dcompiler_47 -ldxgi -o (Join-Path $renderScratch 'test_native_movie.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
Push-Location -LiteralPath $renderScratch
try {
    .\test_native_movie.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPU test failed' }
} finally {
    Pop-Location
}
```

## Integration with the game

The standalone cutscene test renders off-screen. In PC 1.3, the FEng cutscene draw call receives a Direct3D 9 texture at executable offset `0x191460`. The game integration reuses textures, upload buffers, and rendering objects between frames.

The executable imports `Direct3DCreate9`, `D3DXCreateEffectFromResourceA`, and `D3DXCreateEffectPool`. Its 31 compiled effects cover areas including car materials, shadows, interface masks, screen filters, particles, rain, sky, and reflections. A complete port needs to preserve their techniques, data, and render state for each API. The available decompilation project's PC tree has four headers and lacks a buildable PC renderer. [Decompilation project](https://github.com/dbalatoni13/nfsmw)

The experimental [presentation backends](presentation.md) present the composed game frame and render cutscenes. The engine still uses Direct3D 9 for much of the scene, HUD, and menus.

## Porting shadow effects

The inventory reads the local PC 1.3 executable's resources. It identifies 31 effects, 76 techniques, and 87 passes with the parameters, profiles, and render states applied to each pass at its initial values. Runtime values still require verification. [Vertex and pixel shader conversion](shader-conversion.md)

The `IDI_SHADOW_MAP_MESH_DEPTH_FX` and `IDI_SHADOW_MAP_MESH_FX` ports share a vertex transformation. The DirectX 11 test compares both outputs with the original effects, including perspective depth, occlusion, and write masks. All three HLSL entry points also compile to DXIL and validated SPIR-V for Vulkan 1.1.

Run the inventory and comparison tests from `renderer-lab` with 32-bit LLVM-MinGW and the DirectX runtimes installed. Set `$gameExe` to your installed PC 1.3 executable.

```powershell
$gameExe = 'C:/Games/Need for Speed Most Wanted/speed.exe'
$effectScratch = Join-Path $env:TEMP 'unlimited-effects'
[void][IO.Directory]::CreateDirectory($effectScratch)
$inventory = Join-Path $effectScratch 'inspect_legacy_effects.exe'
$shadowTest = Join-Path $effectScratch 'test_shadow_mesh_d3d11.exe'
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode inspect_legacy_effects.cpp -ld3dx9_43 -ld3d9 -o $inventory
if ($LASTEXITCODE -ne 0) { throw 'Inventory tool build failed' }
& $inventory $gameExe
if ($LASTEXITCODE -ne 0) { throw 'Could not read the effects' }
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode test_shadow_mesh_d3d11.cpp -ld3d9 -ld3dx9_43 -ld3d11 -ld3dcompiler_47 -ldxgi -o $shadowTest
if ($LASTEXITCODE -ne 0) { throw 'Shadow test build failed' }
& $shadowTest $gameExe shadow_mesh.hlsl
if ($LASTEXITCODE -ne 0) { throw 'Shadow comparison failed' }
```
