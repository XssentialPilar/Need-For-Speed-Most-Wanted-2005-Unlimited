# Game presentation and cutscene rendering

**Display mode** in the video settings offers **Fullscreen**, **Windowed**, and **Borderless**. The game applies the setting immediately and saves it for the next launch. Windowed mode preserves the aspect ratio and keeps the title bar accessible. Borderless mode fills the game monitor. [Window management](../nfs_window_mode.hpp)

**Presentation API** in the advanced video settings offers DirectX 9, DirectX 11, DirectX 12, and Vulkan. Changes take effect after restarting the game. **DirectX 9 is the default. DirectX 11, DirectX 12, and Vulkan are experimental.** The selector shows “DX9 output” when the game falls back to its original presenter. The setting controls the completed frame and cutscene passes. Cars, roads, particles, and most interface draw calls still use Direct3D 9. [Selector and Present integration](../vram_probe.cpp)

| API | Role |
| --- | --- |
| DirectX 9 | Presents the original game buffer and serves as the fallback if a native operation fails. |
| DirectX 11 | Reads the frame composed by 9On12 through shared GPU resources and presents it through a DXGI swap chain. Cutscenes use a dedicated D3D11 pass. |
| DirectX 12 | Copies or resolves the game render target on the GPU and presents it through DXGI. It renders cutscenes and can draw the supported shadow-mesh batch into the game's color and depth targets. |
| Vulkan | Transfers the frame through shared GPU memory and presents it through `vkQueuePresentKHR`. The Vulkan cutscene pass composites its output into the game render target. |

The `%TEMP%/NFSMW-graphics.log` log records the active presentation API in `present_api` and `native_frames`, and cutscene rendering in `last_movie_api` and `native_movie_draws`. The values 9, 11, 12, and 13 correspond to DirectX 9, DirectX 11, DirectX 12, and Vulkan. The last cutscene API can remain logged during a race after playback ends. [Frame presentation](frame_present.hpp) · [FEng cutscene rendering](../nfs_movies.hpp)

## Display mode changes and rearview mirror

For modern presentation, the engine draws to a private windowed 9On12 surface. The presenter applies the requested display mode. If Vulkan loses exclusive fullscreen, it can continue in borderless mode when the window covers the monitor at the requested mode. When graphics settings change, Unlimited releases its references to the targets and lets the engine rebuild them. [Vulkan presenter](frame_present_vk.hpp)

In the PC 1.3 executable with base address `0x400000`, routine `0x6DB0D0` calls `IDirect3DDevice9::Reset` at `0x6DB16A` and `0x6DB17A` with the same parameter block. It checks the second result. If reset fails, it retries up to three times at `0x6DB1DD` and then reduces the resolution index at `0x6DB242`. Unlimited intercepts all four sites so that retrying preserves the private 9On12 mode.

After a successful reset, `0x6DB265` calls the routine that rebuilds the targets. That routine creates shadows at `0x6BD7FC` and the rearview mirror at `0x6BD810`. The original game accessed the mirror texture immediately after `CreateTexture`. Unlimited publishes the texture, color surface, and depth surface only after all three allocations succeed. It selects 256, 512, or 1024 pixels per side based on the interface and device capabilities, and retries with a smaller size if allocation fails. [Rearview resource allocation](../nfs_visual.hpp)

## Test results and coverage

- **Presentation.** The DirectX 12 and Vulkan tests each verified 1,129,848 RGB swap-chain pixels in XRGB and ARGB, with MSAA off and at 4×. Vulkan completed 24 presentations. The game reached exclusive Vulkan presentation at 1920 × 1200 and 180 Hz on the RTX 5050. A separate test denied exclusive access and verified three borderless Vulkan presentations at the same resolution and refresh rate. The recovery suite ran 57 checks across all three modern APIs. A physical driver loss remains untested.
- **Cutscenes.** The tests compared the video pass against D3D9 at a 1920 × 1080 input resolution and with a 4× MSAA target. DirectX 11 and Vulkan differed by at most one level in an 8-bit channel. DirectX 12 matched exactly. [Cutscene passes](scene_movie_d3d12.hpp)
- **Materials.** The executable contains 31 effects, 76 techniques, and 87 passes, including 71 programmable pairs and 16 fixed-function passes that inherit state. The suite verified 1,952 register values and ran 352 sampler checks. Two GPUs created 206 pipelines per API in DirectX 12 and Vulkan, including two test states per fixed-function pass. Connecting and verifying the remaining materials' geometry, constants, textures, and states in a race is pending. [Inventory and shader conversion tests](shader-conversion.md)
- **DirectX 12 shadows.** All 18 off-screen comparisons of the two original effects matched pixel for pixel in D24S8, DF16, and INTZ. Tests included perspective, write masks, occlusion, two types of depth bias, and a concrete barrier from Rockport. A session on the high setting logged a 2048 × 2048 D24S8 shadow target. An in-race DirectX 12 shadow batch still needs verification. [Shadow pass](scene_shadow_d3d12.hpp) · [Comparison test](scene_test_shadow.cpp)

## Reproduce the GPU tests

Run the shadow comparison from `renderer-lab` and give the test a path to your PC 1.3 executable. It reads the effects from the executable and geometry from `TRACKS/STREAML2RA.BUN`, then renders to a 96 × 96 surface. If another application owns exclusive fullscreen, creating the windowed D3D9 test device can return `D3DERR_DEVICELOST`. [Lost devices in Direct3D 9](https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices)

```powershell
$gameExe = 'C:/Games/Need for Speed Most Wanted/speed.exe'
$shadowTest = Join-Path $env:TEMP 'unlimited-scene-shadow-test.exe'
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode scene_test_shadow.cpp -ld3dx9_43 -ld3d9 -ld3d12 -ld3dcompiler_47 -ldxgi -o $shadowTest
if ($LASTEXITCODE -ne 0) { throw 'Shadow test build failed' }
& $shadowTest $gameExe
if ($LASTEXITCODE -ne 0) { throw 'Shadow comparison failed' }
```

The standalone video presentation test checked seven complete frames and 24 consecutive submissions per API on both the RTX 5050 and Radeon 760M. It covers Full HD input, ultrawide and portrait surfaces, suspension, resumption, and vertical sync changes.

Run from `renderer-lab` with PowerShell, 32-bit LLVM-MinGW, and the Vulkan SDK configured in `VULKAN_SDK`.

```powershell
$presentScratch = Join-Path $env:TEMP 'unlimited-renderer-lab'
[void][IO.Directory]::CreateDirectory($presentScratch)
$presentExe = Join-Path $presentScratch 'test_native_present.exe'
glslangValidator -V movie.vert -o (Join-Path $presentScratch 'movie-vs.spv')
if ($LASTEXITCODE -ne 0) { throw 'Vertex shader build failed' }
glslangValidator -V movie.frag -o (Join-Path $presentScratch 'movie-fs.spv')
if ($LASTEXITCODE -ne 0) { throw 'Pixel shader build failed' }
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -I (Join-Path $env:VULKAN_SDK 'Include') test_native_present.cpp -ld3d11 -ld3d12 -ld3dcompiler_47 -ldxgi -o $presentExe
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
Push-Location -LiteralPath $presentScratch
try {
    & $presentExe
    if ($LASTEXITCODE -ne 0) { throw 'Presentation test failed' }
} finally {
    Pop-Location
}
```

To repeat synchronization validation, build for the architecture of the Vulkan SDK's Khronos validation layer. The test used `x86_64-w64-mingw32-clang++`. In a dedicated PowerShell session, set the following variables before running with `--isolate-windows-hooks`. This flag applies to the test process only. The trace with RTSS active showed an extra submission from its module before presentation. With the test isolated, the validation layer reported no errors on either GPU.

```powershell
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
$env:VK_LAYER_PATH = Join-Path $env:VULKAN_SDK 'Bin'
$env:VK_LAYER_VALIDATE_SYNC = '1'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
```

[DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model) · [DirectX 12 swap chains](https://learn.microsoft.com/en-us/windows/win32/direct3d12/swap-chains) · [Vulkan image acquisition](https://registry.khronos.org/vulkan/specs/latest/man/html/vkAcquireNextImageKHR.html)
