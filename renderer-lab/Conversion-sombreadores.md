# Conversión de los efectos de PC 1.3

El diagnóstico recorre los **31 efectos, 76 técnicas y 87 pases** que contiene la copia local de `speed.exe`. Abre el ejecutable como recurso de solo lectura. Las instrucciones originales, el HLSL generado y los programas compilados permanecen en la memoria del diagnóstico.

**71 pases** contienen programas explícitos de vértices y de píxeles. Direct3D 11 creó los **30 programas de vértices y 65 de píxeles** distintos. El enlace por semántica conserva el registro, el tipo y los canales que consume cada entrada del programa de píxeles. La reflexión del DXBC verificó los 71 pares. El emisor de Unlimited interpreta los dos perfiles de píxeles 1.x. MojoShader traduce los demás perfiles. [Emisor HLSL de MojoShader](https://github.com/icculus/mojoshader/blob/ad5dff84830c2863c841f4b1f4e3df78c705b383/profiles/mojoshader_profile_hlsl.c).

| Perfil | Programas creados en D3D11 |
| --- | ---: |
| `vs_1_1` | 29 de 29 |
| `vs_3_0` | 1 de 1 |
| `ps_1_1` | 28 de 28 |
| `ps_1_4` | 3 de 3 |
| `ps_2_0` | 15 de 15 |
| `ps_2_1` | 17 de 17 |
| `ps_3_0` | 2 de 2 |

Los 31 programas de píxeles 1.x pasaron **62 comparaciones de imagen** entre D3D9 y D3D11. El fotograma de prueba contiene texturas 2D de patrones distintos, colores de vértices interpolados y dos juegos de constantes. Los 15.872 píxeles coincidieron exactamente en sus cuatro canales. La prueba cubre las lecturas dependientes, la coemisión de instrucciones, los límites de los registros de constantes y las dos fases de PS 1.4. [Registros PS 1.x](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-1-x) y [muestreo PS 1.4](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texld---ps-1-4).

## Los 16 pases de función fija

El diagnóstico carga un par de sombreadores en el dispositivo antes de cada pase. Los 16 pases desactivaron ambos durante `BeginPass`. El código de Unlimited lee el estado efectivo justo después, incluidos los valores que el juego había dejado en el dispositivo. El emisor de vértices usa la declaración activa, matrices, material, fuentes de color, luces y generación de coordenadas. El de píxeles combina las etapas activas de textura y reproduce el factor de textura, el registro temporal, el brillo especular y la prueba alfa. [Procesamiento de vértices](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fixed-function-vertex-processing), [operaciones de textura](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtextureop) y [cascada de etapas](https://learn.microsoft.com/en-us/windows/win32/direct3d9/texture-blending).

Se probaron dos conjuntos de estados de entrada por pase, variando las matrices, el factor de textura y una etapa de mezcla heredada. El combinador se comparó con interpolantes controlados en 32 imágenes. En otras 32 se renderizó un triángulo con las dos etapas originales de función fija y se comparó con las dos etapas nuevas de D3D11. **Las 64 imágenes coincidieron píxel a píxel**. Estas pruebas aíslan el material con muestreo puntual, escritura RGBA y rasterización comunes a ambas APIs.

| Efectos | Estado aportado por el motor que debe conservar la integración |
| --- | --- |
| `WORLDMIN` | La técnica `world_min` hereda toda la cascada de texturas del llamador. |
| `FE`, `FE_MASK`, `PARTICLES`, `TREE`, `SHADOW_CG`, `SKYBOX_CG`, `WORLD`, `WORLDNORMALMAP`, `WORLDNORMALMAPNOFOG`, `WORLDPRELIT`, `WORLDREFLECT` | El pase hereda la declaración de vértices. Varias técnicas heredan también matrices, iluminación, índices de coordenadas o etapas superiores. |
| `CAR` (dos técnicas), `RUNWAYLIGHT`, `WORLDNOFOG` | El pase fija el FVF. La geometría, matrices, texturas y los parámetros de material proceden del llamador. En `CAR` se heredan las luces activas. |

El seguimiento del motor debe proporcionar a `CaptureVertex` los identificadores de todas las luces que pudo habilitar. Direct3D 9 admite índices dispersos. La captura rechaza combinaciones pendientes como niebla heredada, planos de recorte del usuario o mezcla de vértices. Las pruebas de este diagnóstico cubren dos configuraciones reproducibles por pase. La integración con una carrera exige comprobar los estados que realmente se solicitan, el contenido de los búferes, los recursos y los destinos de cada dibujo.

## Direct3D 12 y Vulkan

DXC compiló los 71 pares explícitos y 32 variantes de los pases fijos a DXIL 6.0 y SPIR-V para Vulkan 1.1. Se crearon **206 pipelines Direct3D 12 y 206 pipelines Vulkan** entre la RTX 5050 Laptop y la Radeon 760M. Cada GPU creó los 103 de cada API. La capa de validación Khronos completó la prueba de Vulkan sin emitir errores. Se verificaron los descriptores y las ubicaciones de entrada y salida. Las pruebas de imagen descritas arriba comparan D3D9 con D3D11. [Asignación de registros y descriptores en DXC](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst).

## Repetir las pruebas

Ejecuta desde `renderer-lab` con LLVM-MinGW de 32 bits, CMake, Ninja y el runtime DirectX heredado que usa el juego. El informe incorpora un identificador por programa y la lista de pases afectados.

```powershell
$coverageScratch = Join-Path $env:TEMP 'unlimited-shader-coverage'
[void][IO.Directory]::CreateDirectory($coverageScratch)
$mojoPath = Join-Path $coverageScratch 'mojoshader'
$mojoBuild = Join-Path $coverageScratch 'mojoshader-build'
if (!(Test-Path -LiteralPath (Join-Path $mojoPath '.git'))) {
    git clone https://github.com/icculus/mojoshader.git $mojoPath
    if ($LASTEXITCODE -ne 0) { throw 'Falló la descarga de MojoShader' }
}
git -C $mojoPath checkout --detach ad5dff84830c2863c841f4b1f4e3df78c705b383
if ($LASTEXITCODE -ne 0) { throw 'Falló la selección de versión de MojoShader' }
cmake -S $mojoPath -B $mojoBuild -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_COMPILER=i686-w64-mingw32-clang.exe' -DPROFILE_HLSL=ON -DEFFECT_SUPPORT=OFF
if ($LASTEXITCODE -ne 0) { throw 'Falló la configuración de MojoShader' }
cmake --build $mojoBuild --target mojoshader
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de MojoShader' }
$coverageExe = Join-Path $coverageScratch 'check_legacy_shader_translation.exe'
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode -I $mojoPath check_legacy_shader_translation.cpp (Join-Path $mojoBuild 'libmojoshader.a') -ldxguid -ld3d9 -ld3dx9_43 -ld3d11 -ld3dcompiler_47 -o $coverageExe
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación del diagnóstico' }
& $coverageExe (Resolve-Path -LiteralPath '../Juego/speed.exe').Path
if ($LASTEXITCODE -ne 0) { throw 'Falló la lectura de efectos' }
```

La comprobación adicional de pipelines usa la compilación de 64 bits de Vulkan SDK con SPIRV-Reflect y el DXC de Windows SDK. Ajusta la ruta de Windows SDK a la versión instalada.

```powershell
$mojo64 = Join-Path $coverageScratch 'mojoshader-build-x64'
cmake -S $mojoPath -B $mojo64 -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang.exe' -DPROFILE_HLSL=ON -DEFFECT_SUPPORT=OFF
if ($LASTEXITCODE -ne 0) { throw 'Falló CMake x64' }
cmake --build $mojo64 --target mojoshader
if ($LASTEXITCODE -ne 0) { throw 'Falló MojoShader x64' }
$reflectDir = Join-Path $env:VULKAN_SDK 'Source/SPIRV-Reflect'
$reflectObj = Join-Path $coverageScratch 'spirv_reflect_x64.o'
x86_64-w64-mingw32-clang -O2 -DSPIRV_REFLECT_USE_SYSTEM_SPIRV_H -I "$env:VULKAN_SDK/Include" -c "$reflectDir/spirv_reflect.c" -o $reflectObj
if ($LASTEXITCODE -ne 0) { throw 'Falló SPIRV-Reflect' }
$check64 = Join-Path $coverageScratch 'check_pipelines_x64.exe'
x86_64-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode -DUNLIMITED_NATIVE_PIPELINE_TEST -I $mojoPath -I "$env:VULKAN_SDK/Include" -I $reflectDir check_legacy_shader_translation.cpp "$mojo64/libmojoshader.a" $reflectObj "$env:VULKAN_SDK/Lib/vulkan-1.lib" -ld3d12 -ldxgi -ldxguid -ld3d9 -ld3dx9_43 -ld3d11 -ld3dcompiler_47 -o $check64
if ($LASTEXITCODE -ne 0) { throw 'Falló el diagnóstico nativo x64' }
$dxCompiler = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxcompiler.dll'
& $check64 (Resolve-Path '../Juego/speed.exe').Path $dxCompiler "$env:VULKAN_SDK/Bin/dxcompiler.dll"
if ($LASTEXITCODE -ne 0) { throw 'Falló la validación de pipelines' }
```
