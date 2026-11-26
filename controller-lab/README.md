# Diagnóstico de mandos

Esta prueba usa la misma biblioteca SDL3 de 32 bits que Unlimited. Por defecto solo enumera los dispositivos reconocidos por SDL y muestra su identificador de fabricante y producto. Con `--pulse VID:PID` envía un pulso de 125 ms a cada motor por separado, o un pulso por la ruta háptica si el controlador ofrece esa interfaz. Comprueba que el controlador acepte el pulso y la parada. La sensación física se comprueba en el mando.

Ejecuta desde `controller-lab` en PowerShell con LLVM-MinGW y los encabezados de SDL3. En este equipo los encabezados usados en la verificación están en la carpeta temporal del proyecto.

```powershell
$sdlHeaders = Join-Path $env:TEMP 'nfsmw-controller/sdl3-source/include'
$probe = Join-Path $env:TEMP 'nfsmw-controller/check_physical_pad.exe'
i686-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wextra -Werror -static -municode -I $sdlHeaders check_physical_pad.cpp -o $probe
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación del diagnóstico.' }
$gameSdl = (Resolve-Path -LiteralPath '../Juego/SDL3.dll').Path
& $probe $gameSdl
```

Para probar un mando, cierra el juego y añade `--pulse` seguido del VID:PID mostrado por la enumeración. El diagnóstico exige una coincidencia única y omite los mandos virtuales de SDL.
