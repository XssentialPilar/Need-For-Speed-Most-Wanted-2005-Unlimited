# Shader conversion for PC 1.3

The diagnostic enumerates the **31 effects, 76 techniques, and 87 passes** in a local copy of `speed.exe`. It opens the executable read-only and keeps the original instructions, generated HLSL, and compiled programs in memory.

**71 passes** contain explicit vertex and pixel programs. Direct3D 11 created **30 distinct vertex programs and 65 distinct pixel programs**. Semantic linkage preserves the register, type, and channels each pixel shader input consumes. DXBC reflection verified all 71 pairs. Unlimited handles the two 1.x pixel shader profiles. MojoShader translates the other profiles. [MojoShader HLSL emitter](https://github.com/icculus/mojoshader/blob/ad5dff84830c2863c841f4b1f4e3df78c705b383/profiles/mojoshader_profile_hlsl.c)

| Profile | Programs created in D3D11 |
| --- | ---: |
| `vs_1_1` | 29 of 29 |
| `vs_3_0` | 1 of 1 |
| `ps_1_1` | 28 of 28 |
| `ps_1_4` | 3 of 3 |
| `ps_2_0` | 15 of 15 |
| `ps_2_1` | 17 of 17 |
| `ps_3_0` | 2 of 2 |

All 31 pixel shader 1.x programs passed **62 image comparisons** between D3D9 and D3D11. The test uses distinct 2D texture patterns, interpolated vertex colors, and two sets of constants. All 15,872 pixels matched exactly in all four channels. The test covers dependent reads, instruction co-issuing, constant register limits, and both phases of PS 1.4. [PS 1.x registers](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-1-x) · [PS 1.4 sampling](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texld---ps-1-4)

## The 16 fixed-function passes

The diagnostic binds a pair of shaders before each pass. All 16 passes disable both shaders during `BeginPass`. Unlimited captures the resulting device state, including values inherited from the game. The vertex emitter uses the active declaration, matrices, material, color sources, lights, and texture coordinate generation. The pixel emitter combines enabled texture stages and reproduces the texture factor, temporary register, specular highlights, and alpha testing. [Fixed-function vertex processing](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fixed-function-vertex-processing) · [Texture operations](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtextureop) · [Texture stage cascades](https://learn.microsoft.com/en-us/windows/win32/direct3d9/texture-blending)

Tests used two input states per pass and varied the matrices, texture factor, and an inherited blend stage. The combiner was compared with controlled interpolants in 32 images. Another 32 comparisons rendered a triangle using both original fixed-function stages and both D3D11 replacements. **All 64 images matched pixel for pixel.** The tests use point sampling, RGBA writes, and the same rasterization settings on both APIs.

| Effects | Engine-provided state to preserve |
| --- | --- |
| `WORLDMIN` | The `world_min` technique inherits the entire texture stage cascade from the caller. |
| `FE`, `FE_MASK`, `PARTICLES`, `TREE`, `SHADOW_CG`, `SKYBOX_CG`, `WORLD`, `WORLDNORMALMAP`, `WORLDNORMALMAPNOFOG`, `WORLDPRELIT`, `WORLDREFLECT` | Passes inherit the vertex declaration. Several techniques also inherit matrices, lighting, coordinate indices, or higher texture stages. |
| `CAR` (two techniques), `RUNWAYLIGHT`, `WORLDNOFOG` | Passes set the FVF. The caller provides geometry, matrices, textures, and material parameters. `CAR` inherits active lights. |

Engine state tracking must pass every potentially enabled light ID to `CaptureVertex`. Direct3D 9 supports sparse light indices. The capture rejects unsupported combinations such as inherited fog, user clip planes, and vertex blending. Tests cover two reproducible states per pass. Race integration still requires checking the states, buffer contents, resources, and targets for each draw.

## Direct3D 12 and Vulkan

DXC compiled all 71 explicit pairs and 32 fixed-function variants to DXIL 6.0 and SPIR-V for Vulkan 1.1. The RTX 5050 Laptop and the Radeon 760M created **206 Direct3D 12 pipelines and 206 Vulkan pipelines** combined, with 103 per API on each GPU. The Khronos validation layer reported no Vulkan errors. Tests checked descriptors and input and output locations. The image comparisons above cover D3D9 and D3D11. [DXC register and descriptor mapping](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst)

## Reproduce the tests

Run from `renderer-lab` with 32-bit LLVM-MinGW, CMake, Ninja, and the game's legacy DirectX runtime. Set `$gameExe` to your installed PC 1.3 executable. The report identifies each program and its passes.

```powershell
$gameExe = 'C:/Games/Need for Speed Most Wanted/speed.exe'
$coverageScratch = Join-Path $env:TEMP 'unlimited-shader-coverage'
[void][IO.Directory]::CreateDirectory($coverageScratch)
$mojoPath = Join-Path $coverageScratch 'mojoshader'
$mojoBuild = Join-Path $coverageScratch 'mojoshader-build'
if (!(Test-Path -LiteralPath (Join-Path $mojoPath '.git'))) {
    git clone https://github.com/icculus/mojoshader.git $mojoPath
    if ($LASTEXITCODE -ne 0) { throw 'MojoShader download failed' }
}
git -C $mojoPath checkout --detach ad5dff84830c2863c841f4b1f4e3df78c705b383
if ($LASTEXITCODE -ne 0) { throw 'Could not check out the required MojoShader version' }
cmake -S $mojoPath -B $mojoBuild -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_COMPILER=i686-w64-mingw32-clang.exe' -DPROFILE_HLSL=ON -DEFFECT_SUPPORT=OFF
if ($LASTEXITCODE -ne 0) { throw 'MojoShader configuration failed' }
cmake --build $mojoBuild --target mojoshader
if ($LASTEXITCODE -ne 0) { throw 'MojoShader build failed' }
$coverageExe = Join-Path $coverageScratch 'check_legacy_shader_translation.exe'
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode -I $mojoPath check_legacy_shader_translation.cpp (Join-Path $mojoBuild 'libmojoshader.a') -ldxguid -ld3d9 -ld3dx9_43 -ld3d11 -ld3dcompiler_47 -o $coverageExe
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic build failed' }
& $coverageExe $gameExe
if ($LASTEXITCODE -ne 0) { throw 'Could not read the effects' }
```

The 64-bit pipeline test uses SPIRV-Reflect from the Vulkan SDK and DXC from the Windows SDK. Set the Windows SDK path to the version installed on your machine.

```powershell
$mojo64 = Join-Path $coverageScratch 'mojoshader-build-x64'
cmake -S $mojoPath -B $mojo64 -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang.exe' -DPROFILE_HLSL=ON -DEFFECT_SUPPORT=OFF
if ($LASTEXITCODE -ne 0) { throw '64-bit CMake configuration failed' }
cmake --build $mojo64 --target mojoshader
if ($LASTEXITCODE -ne 0) { throw '64-bit MojoShader build failed' }
$reflectDir = Join-Path $env:VULKAN_SDK 'Source/SPIRV-Reflect'
$reflectObj = Join-Path $coverageScratch 'spirv_reflect_x64.o'
x86_64-w64-mingw32-clang -O2 -DSPIRV_REFLECT_USE_SYSTEM_SPIRV_H -I "$env:VULKAN_SDK/Include" -c "$reflectDir/spirv_reflect.c" -o $reflectObj
if ($LASTEXITCODE -ne 0) { throw 'SPIRV-Reflect build failed' }
$check64 = Join-Path $coverageScratch 'check_pipelines_x64.exe'
x86_64-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode -DUNLIMITED_NATIVE_PIPELINE_TEST -I $mojoPath -I "$env:VULKAN_SDK/Include" -I $reflectDir check_legacy_shader_translation.cpp "$mojo64/libmojoshader.a" $reflectObj "$env:VULKAN_SDK/Lib/vulkan-1.lib" -ld3d12 -ldxgi -ldxguid -ld3d9 -ld3dx9_43 -ld3d11 -ld3dcompiler_47 -o $check64
if ($LASTEXITCODE -ne 0) { throw '64-bit native diagnostic build failed' }
$dxCompiler = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxcompiler.dll'
& $check64 $gameExe $dxCompiler "$env:VULKAN_SDK/Bin/dxcompiler.dll"
if ($LASTEXITCODE -ne 0) { throw 'Pipeline validation failed' }
```
