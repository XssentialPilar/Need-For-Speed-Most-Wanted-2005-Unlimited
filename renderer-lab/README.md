# Prueba del renderizador nativo

Este prototipo dibuja un fotograma BGRA decodificado con shaders propios en DirectX 11, DirectX 12 y Vulkan. Conserva las proporciones, trata el espacio sobrante como bandas negras y compara cada píxel RGBA devuelto por la GPU con el patrón esperado.

Pasaron 24 pruebas en un proceso de 32 bits. Cada API ejecutó cuatro casos en la RTX 5050 y en la Radeon 760M. Los casos incluyen 4:3, ultrapanorámico, vertical y la carga de un fotograma de 1920 × 1080 con relleno entre filas hacia un destino de 1920 × 1200. La prueba también rechaza un fotograma truncado.

## Reproducir la prueba

Ejecuta estos comandos desde `renderer-lab` con PowerShell, LLVM-MinGW de 32 bits y el SDK de Vulkan en `VULKAN_SDK`.

```powershell
$renderSources = (Get-Location).Path
$renderScratch = Join-Path $env:TEMP 'unlimited-renderer-lab'
[void][IO.Directory]::CreateDirectory($renderScratch)
glslangValidator -V (Join-Path $renderSources 'movie.vert') -o (Join-Path $renderScratch 'movie-vs.spv')
if ($LASTEXITCODE -ne 0) { throw 'Falló el shader de vértices' }
glslangValidator -V (Join-Path $renderSources 'movie.frag') -o (Join-Path $renderScratch 'movie-fs.spv')
if ($LASTEXITCODE -ne 0) { throw 'Falló el shader de píxeles' }
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -I (Join-Path $env:VULKAN_SDK 'Include') (Join-Path $renderSources 'test_native_movie.cpp') -ld3d11 -ld3d12 -ld3dcompiler_47 -ldxgi -o (Join-Path $renderScratch 'test_native_movie.exe')
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de la prueba' }
Push-Location -LiteralPath $renderScratch
try {
    .\test_native_movie.exe
    if ($LASTEXITCODE -ne 0) { throw 'Falló la prueba de GPU' }
} finally {
    Pop-Location
}
```

## Integración pendiente en el juego

El prototipo termina en un destino fuera de pantalla. El paso de cinemáticas de PC 1.3 recibe una textura de Direct3D 9 en el dibujo de FEng, en el desplazamiento `0x191460` del ejecutable. Para llevar este paso a producción se necesita una ruta de presentación compartida con el HUD, los menús y la escena 3D, además de reutilizar la textura, el búfer de carga y los objetos de renderizado entre fotogramas.

El ejecutable importa `Direct3DCreate9`, `D3DXCreateEffectFromResourceA` y `D3DXCreateEffectPool`. Contiene 31 efectos compilados, entre ellos materiales de coches, mapas de sombras, máscaras de interfaz, filtros de pantalla, partículas, lluvia, cielo y reflejos. El portado necesita conservar sus técnicas, datos y estados de renderizado en cada API. El árbol de PC del proyecto de decompilación disponible contiene cuatro encabezados y carece de una implementación de renderizado de PC que se pueda compilar. [Proyecto de decompilación](https://github.com/dbalatoni13/nfsmw).

La prueba de [presentación nativa](Presentacion.md) ya dibuja en las cadenas de intercambio de las tres API. La conexión con la escena 3D, el HUD y los menús de PC sigue pendiente.

## Portado de efectos de sombra

El inventario refleja los recursos del ejecutable local de PC 1.3 en modo de solo lectura. Detecta 31 efectos, 76 técnicas y 87 pases con los parámetros, perfiles y estados que aplica cada pase con los valores iniciales. Los valores que envía el motor en tiempo de ejecución requieren verificación adicional. [Conversión de programas de vértices y píxeles](Conversion-sombreadores.md).

El portado de `IDI_SHADOW_MAP_MESH_DEPTH_FX` e `IDI_SHADOW_MAP_MESH_FX` comparte la transformación de vértices. La prueba de DirectX 11 comprueba los dos resultados frente a los efectos originales, incluida la profundidad con perspectiva, la oclusión y la máscara de escritura. Los tres puntos de entrada HLSL también compilan a DXIL y a SPIR-V validado para Vulkan 1.1.

Desde `renderer-lab`, con LLVM-MinGW de 32 bits y los runtimes de DirectX instalados, ejecuta las comprobaciones de inventario y de paridad.

```powershell
$effectScratch = Join-Path $env:TEMP 'unlimited-effects'
[void][IO.Directory]::CreateDirectory($effectScratch)
$inventory = Join-Path $effectScratch 'inspect_legacy_effects.exe'
$shadowTest = Join-Path $effectScratch 'test_shadow_mesh_d3d11.exe'
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode inspect_legacy_effects.cpp -ld3dx9_43 -ld3d9 -o $inventory
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación del inventario' }
& $inventory ../Juego/speed.exe
if ($LASTEXITCODE -ne 0) { throw 'Falló la lectura de los efectos' }
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode test_shadow_mesh_d3d11.cpp -ld3d9 -ld3dx9_43 -ld3d11 -ld3dcompiler_47 -ldxgi -o $shadowTest
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de la prueba de sombras' }
& $shadowTest ../Juego/speed.exe shadow_mesh.hlsl
if ($LASTEXITCODE -ne 0) { throw 'Falló la comparación de las sombras' }
```
