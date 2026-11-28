# Building Unlimited

Use a Windows machine with Git for Windows, PowerShell, LLVM-MinGW (including `i686-w64-mingw32-clang++` and `i686-w64-mingw32-windres` on PATH), the Vulkan SDK (`VULKAN_SDK` set), the Windows SDK x86 UCRT redistributables, and Inno Setup 6. For optional asset builds, install Python 3.10 or newer with Pillow, and ensure `py -3` and `tar.exe` are available. The build validates and caches its FFmpeg source and GNU Make downloads. The complete build also needs several gigabytes of temporary space.

Run the commands from the repository root.

## Runtime-only development build

```powershell
./installer/build.ps1 -RuntimeOnly -OutputDirectory ./dist/v1.1.0-beta.1
Get-FileHash ./dist/v1.1.0-beta.1/NFSMW-Unlimited-Runtime-Setup.exe -Algorithm SHA256
```

The installer includes the x86 runtime, controller helpers, XenonEffects and the replaceable DISCX decoder. FFmpeg is linked into the decoder. The matching source archive, adapter, build scripts and notices are included under `UnlimitedCommunity\Discx`. [Decoder implementation](../third-party/discx-codec/README.md)

Use `./build.ps1 -OutputFile ./dist/d3d9.dll` for the core runtime alone. Use `-Compiler` and `-LegacyUcrt` with the installer recipe if your Inno Setup or Windows SDK installation is outside the default location.

## Complete local installer with optional assets

Start with a supported PC 1.3 installation whose HD resource files match the `HD` column in `installer/upscaled-hud-checksums.tsv`. The existing stable full installer can prepare those files. For console builds, the same source also needs the expected stock track, audio and vault inputs. The source is read by the build. Install and test the result on a separate copy of the game.

```powershell
./installer/build.ps1 -HudGameDirectory 'C:/Games/NFSMW Build Input'
```

The command outputs `NFSMW-Unlimited-Setup.exe`. Select compatible asset inputs as needed.

| Build argument | Input and output |
| --- | --- |
| `-TextureArchive` | Supplies the compatible 2.01 texture remaster as a 7z archive or an extracted directory. The output contains `GLOBAL/UnlimitedTextures.bin`. |
| `-XboxComplexDirectory` | Supplies the extracted Complex Installation folder from Xbox 360 Stuff Pack 4.2. The output packages verified console resources and registers the supported rendering profiles. |
| `-XboxGlobal` | Supplies the matching external Xbox 360 `GlobalB.lzc` so the build can embed compatible headlight flare and rain inputs. |
| `-XboxAttribulator` | Supplies the x86 Windows Attribulator CLI or its managed DLL to build the optional console visual vaults. |
| `-XboxDotnet` | Supplies an explicit x86 .NET host when using the managed CLI. |

```powershell
$consoleInputs = @{
    HudGameDirectory = 'C:/Games/NFSMW Build Input'
    XboxComplexDirectory = 'C:/Assets/Complex Installation'
    XboxGlobal = 'C:/Assets/Xbox360/GlobalB.lzc'
    XboxAttribulator = 'C:/Tools/Attribulator/Attribulator.CLI.dll'
    XboxDotnet = 'C:/Tools/dotnet-x86/dotnet.exe'
}
./installer/build.ps1 @consoleInputs -TextureArchive 'C:/Assets/8K Remade Textures V2.01.7z'
./installer/build.ps1 @consoleInputs -ConsoleUpdateOnly
```

The second command creates `NFSMW-Unlimited-Console-Update.exe` for an existing installation with the Unlimited console texture bank. It carries the runtime, decoder and updated console renderer data. The build reports the specific source record or checksum that prevented staging if an input is incompatible. [Xbox 360 asset inventory and runtime settings](../xbox-360-integration.md)

## Compact all-in-one installer

Use the same console inputs as the complete build.

```powershell
./installer/build.ps1 @consoleInputs -Compact -OutputDirectory ./dist/compact
```

The compact build creates **NFSMW-Unlimited-Compact-Setup.exe**. It works on a supported PC 1.3 game directory and combines the HD interface, controller and DISCX support, console engine sounds, and the console updater's renderer, shaders, garage assets and visual databases in one installer.

Setup generates the alternate track archive from the included UPS delta and the verified stock PC stream. It retains an existing alternate archive and logs when an incompatible or missing stock stream prevents generation.

The package omits the 8K remaster and the console soundtrack bank. It retains the texture quality selector. With Xbox 360 effects active, the profiles apply to the available console textures. Higher settings use the largest available mip level up to the existing console cap of 2048 pixels. The original PC soundtrack is used when a compatible console music bank is unavailable. Setup preserves previously installed optional banks.

The compact build stores each identical console mip chain once and keeps the full set of texture IDs. It includes the FFmpeg source and the applicable third-party notices.

## Verification by area

- Run the profile store test with a copy of a valid PC 1.3 profile and an output directory that does not yet exist. The test covers native size and checksum validation, interrupted writes, lock conflicts, history, and recovery paths.
- Install the built package into a separate supported game copy. Check the installer log, start the game, and verify the menu and a race before using a primary installation. Use `/LOG="C:\Temp\Unlimited-Setup.log"` to collect a setup log.
- For presentation or device changes, test menu, gameplay, reset, resolution changes, and the return to menus. Include a two-, three- and four-player race for changes to the split-screen renderer. [Renderer lab notes](../renderer-lab/README.md)

```powershell
i686-w64-mingw32-clang++ -std=c++17 -O2 -static -municode tests/profile_store.cpp -lbcrypt -o "$env:TEMP/profile-store-test.exe"
& "$env:TEMP/profile-store-test.exe" 'C:/QA/ValidProfileCopy' 'C:/QA/UnlimitedProfileTest-New'
```

The repository carries the source and build recipes. Prepare compatible retail data and asset packages locally.