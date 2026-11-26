# Presentación del juego y renderizado de cinemáticas

En **Gráficos → Modo de pantalla** se puede elegir **Pantalla completa**, **Ventana** o **Sin bordes**. El juego aplica la elección durante la sesión y la recuerda al arrancar. Ventana conserva la proporción del juego y deja accesible la barra de título. Sin bordes ocupa el monitor del juego. [Gestión de la ventana](../nfs_window_mode.hpp).

En **Gráficos → Avanzado → API de salida** se puede elegir DirectX 9, DirectX 11, DirectX 12 o Vulkan. El cambio de API se aplica al reiniciar. El selector indica «salida DX9» cuando el fotograma vuelve al presentador original. La opción controla la salida del fotograma completo y el pase de cinemáticas. El motor sigue enviando los dibujos de coches, carretera, partículas y la mayor parte de la interfaz por Direct3D 9. [Integración del selector y de Present](../vram_probe.cpp).

| Ruta | Trabajo que realiza en el juego |
| --- | --- |
| DirectX 9 | Presenta el búfer original del juego. También sirve de respaldo cuando falla una operación nativa. |
| DirectX 11 | Accede al fotograma compuesto por 9On12 mediante recursos compartidos en la GPU y lo presenta en una cadena DXGI. El pase de vídeo propio usa D3D11. |
| DirectX 12 | Copia o resuelve el destino del juego en la GPU y presenta mediante DXGI. Dibuja el vídeo y puede ejecutar el lote de mallas de sombra compatible en los destinos reales de color y profundidad. |
| Vulkan | Transfiere el fotograma por memoria compartida en la GPU e intercambia la imagen con `vkQueuePresentKHR`. El pase de vídeo usa Vulkan y compone su resultado en el destino del juego. |

El registro `%TEMP%/NFSMW-graphics.log` distingue la salida efectiva (`present_api`, `native_frames`) de la última cinemática (`last_movie_api`, `native_movie_draws`). Los valores 9, 11, 12 y 13 identifican DirectX 9, DirectX 11, DirectX 12 y Vulkan respectivamente. Un valor de vídeo puede conservarse durante la carrera, cuando ya terminó la reproducción. [Presentación del fotograma](frame_present.hpp) · [Dibujo de vídeo en FEng](../nfs_movies.hpp).

## Cambio de modos y retrovisor

Para la salida moderna, el motor dibuja en una superficie privada de 9On12 en modo ventana. El presentador aplica el modo de pantalla solicitado. Si Vulkan pierde la exclusiva, puede continuar sin bordes cuando la ventana cubre el monitor con el modo solicitado. Al aplicar cambios gráficos, Unlimited libera sus referencias a los destinos y devuelve el control al ciclo de reconstrucción del motor. [Presentador Vulkan](frame_present_vk.hpp).

En el ejecutable PC 1.3 con base `0x400000`, la rutina `0x6DB0D0` llama dos veces seguidas a `IDirect3DDevice9::Reset` en `0x6DB16A` y `0x6DB17A`, con el mismo bloque de parámetros. Comprueba el resultado de la segunda llamada. Si falla, reintenta hasta tres veces en `0x6DB1DD` y después reduce el índice de resolución en `0x6DB242`. Unlimited intercepta los cuatro puntos para que un segundo intento del juego conserve el modo privado de 9On12.

Tras un reinicio correcto, `0x6DB265` llama a la rutina que reconstruye los destinos. Esta crea las sombras en `0x6BD7FC` y el retrovisor en `0x6BD810`. El original accedía a la textura del espejo inmediatamente después de `CreateTexture` sin comprobar el resultado. Unlimited publica el conjunto de textura, superficie de color y profundidad solo si se crean los tres. Selecciona un lado de 256, 512 o 1024 píxeles según el tamaño de la interfaz y las capacidades del dispositivo, y prueba el tamaño inferior si falla la reserva. [Asignación de recursos del retrovisor](../nfs_visual.hpp).

## Resultados y alcance

- **Presentación.** DirectX 12 y Vulkan verificaron, cada uno, 1.129.848 píxeles RGB de la cadena de intercambio con formatos XRGB y ARGB, sin multimuestreo y con MSAA 4×. La prueba de Vulkan realizó 24 presentaciones. El juego de PC presentó en Vulkan exclusivo a 1920 × 1200 y 180 Hz en la RTX 5050. Una prueba independiente denegó la exclusiva y verificó tres presentaciones Vulkan sin bordes en ese modo. El banco de recuperación completó 57 comprobaciones entre las tres API modernas. Queda pendiente reproducir una pérdida física del controlador.
- **Cinemáticas.** El banco comparó el pase de vídeo con la salida D3D9 para entrada de 1920 × 1080 y destino con MSAA 4×. La diferencia máxima fue de una unidad en un canal de ocho bits en DirectX 11 y Vulkan. DirectX 12 coincidió exactamente. [Pases de vídeo](scene_movie_d3d12.hpp).
- **Materiales.** El ejecutable contiene 31 efectos, 76 técnicas y 87 pases. Hay 71 pares programables por pase y 16 pases de función fija que dependen del estado heredado. El banco verificó 1.952 valores de registros y 352 comprobaciones de muestreadores. En dos GPU se crearon 206 pipelines por API en DirectX 12 y Vulkan, incluidos dos estados de prueba por pase fijo. Aún falta conectar y verificar en carrera geometría, constantes, texturas y estados de los demás materiales. [Inventario y pruebas de conversión](Conversion-sombreadores.md).
- **Sombras DirectX 12.** Las 18 comparaciones fuera de pantalla de los dos efectos originales coincidieron píxel a píxel, con D24S8, DF16 e INTZ. Las pruebas cubren perspectiva, máscara de escritura, oclusión, dos clases de sesgo y una barrera de hormigón extraída de Rockport. La sesión de juego con calidad alta registró una sombra D24S8 de 2048 × 2048. La ejecución de un lote de sombras en DirectX 12 durante una carrera queda pendiente de confirmación. [Pase de sombras](scene_shadow_d3d12.hpp) · [Prueba comparativa](scene_test_shadow.cpp).

## Reproducir las pruebas de GPU

Para comparar las sombras, ejecuta los siguientes comandos desde `renderer-lab` con el adaptador disponible para un dispositivo D3D9 en ventana. La prueba lee los efectos del ejecutable PC 1.3 y la geometría de `TRACKS/STREAML2RA.BUN`, y renderiza en una superficie de 96 × 96. Si otra aplicación tiene la exclusiva, la creación del dispositivo de prueba puede devolver `D3DERR_DEVICELOST`. [Dispositivos perdidos en Direct3D 9](https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices).

```powershell
$shadowTest = Join-Path $env:TEMP 'unlimited-scene-shadow-test.exe'
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -municode scene_test_shadow.cpp -ld3dx9_43 -ld3d9 -ld3d12 -ld3dcompiler_47 -ldxgi -o $shadowTest
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de la prueba de sombras' }
& $shadowTest (Resolve-Path '../Juego/speed.exe').Path
if ($LASTEXITCODE -ne 0) { throw 'Falló la comparación de sombras' }
```

La prueba de presentación de vídeo autónoma verificó, en cada API y en cada una de las GPU RTX 5050 y Radeon 760M, siete fotogramas completos y 24 envíos consecutivos con entrada Full HD, superficies panorámicas y verticales, suspensión, reanudación y cambios de sincronización vertical.

Ejecuta desde `renderer-lab` con PowerShell, LLVM-MinGW de 32 bits y el SDK de Vulkan en `VULKAN_SDK`.

```powershell
$presentScratch = Join-Path $env:TEMP 'unlimited-renderer-lab'
[void][IO.Directory]::CreateDirectory($presentScratch)
$presentExe = Join-Path $presentScratch 'test_native_present.exe'
glslangValidator -V movie.vert -o (Join-Path $presentScratch 'movie-vs.spv')
if ($LASTEXITCODE -ne 0) { throw 'Falló el shader de vértices' }
glslangValidator -V movie.frag -o (Join-Path $presentScratch 'movie-fs.spv')
if ($LASTEXITCODE -ne 0) { throw 'Falló el shader de píxeles' }
i686-w64-mingw32-clang++ -std=c++17 -Wall -Wextra -Werror -O2 -static -I (Join-Path $env:VULKAN_SDK 'Include') test_native_present.cpp -ld3d11 -ld3d12 -ld3dcompiler_47 -ldxgi -o $presentExe
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de la prueba' }
Push-Location -LiteralPath $presentScratch
try {
    & $presentExe
    if ($LASTEXITCODE -ne 0) { throw 'Falló la prueba de presentación' }
} finally {
    Pop-Location
}
```

Para repetir la validación de sincronización, compila con la arquitectura de la capa Khronos del SDK. En esta máquina se usó `x86_64-w64-mingw32-clang++`. En una sesión de PowerShell dedicada activa estas variables antes de ejecutar la prueba con `--isolate-windows-hooks`. La opción se limita al proceso de prueba. La traza con RTSS activo identificó un envío adicional de su módulo antes de presentar. Con la prueba aislada, la capa de validación terminó sin errores en las dos GPU.

```powershell
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
$env:VK_LAYER_PATH = Join-Path $env:VULKAN_SDK 'Bin'
$env:VK_LAYER_VALIDATE_SYNC = '1'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
```

[DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model) · [Cadenas de intercambio en DirectX 12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/swap-chains) · [Adquisición de imágenes en Vulkan](https://registry.khronos.org/vulkan/specs/latest/man/html/vkAcquireNextImageKHR.html).
