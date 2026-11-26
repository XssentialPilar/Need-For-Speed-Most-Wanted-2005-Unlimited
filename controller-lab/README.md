# Controller diagnostics

This diagnostic uses the same 32-bit SDL3 library as Unlimited. By default it lists SDL devices and their vendor and product IDs. With `--pulse VID:PID` it sends a 125 ms pulse to each motor separately, or a single pulse through the haptic interface if the driver exposes one. It checks whether the driver accepts both the pulse and stop commands. Check the controller itself to verify the physical response.

Run the following in PowerShell from `controller-lab` with LLVM-MinGW and the SDL3 headers. Set `$sdlHeaders` to your SDL3 include directory.

```powershell
$sdlHeaders = 'C:/Libraries/SDL3/include'
$probe = Join-Path $env:TEMP 'check_physical_pad.exe'
i686-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wextra -Werror -static -municode -I $sdlHeaders check_physical_pad.cpp -o $probe
if ($LASTEXITCODE -ne 0) { throw 'Controller diagnostic build failed.' }
$gameSdl = (Resolve-Path -LiteralPath '../third-party/controllers/SDL3.dll').Path
& $probe $gameSdl
```

To test a physical controller, close the game and pass `--pulse` followed by the VID:PID shown in the device list. The diagnostic requires exactly one matching controller and skips virtual SDL controllers.
