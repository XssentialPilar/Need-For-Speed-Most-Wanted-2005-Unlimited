# Need for Speed Most Wanted 2005 Unlimited

A patch for the 2005 PC release. The installer supports Windows 7 SP1 and later on systems that can run x86 applications. Unlimited requires a compatible installation of the original game with the 1.3 executable.

## Installation

Run the complete **NFSMW-Unlimited-Setup.exe**, choose a setup language, and select the original game folder containing `speed.exe`. Setup validates the executable, applies all 295 bundled HD interface files, and installs the patch and controller libraries. It preserves existing settings and backs up replaced files under `UnlimitedCommunity/BeforeUnlimited` inside the game folder.

Setup is available in English, Spanish, French, German, Italian, Dutch, Portuguese, Polish, Russian, Japanese, Swedish, and Danish.

## Display and controls

- The minimap, speedometer, pursuit panel, and EA Trax notice each move as a group. Their corner margins scale with the shorter side of the viewport, and their artwork retains its proportions.
- In the video settings, **Display mode** offers **Fullscreen**, **Windowed**, and **Borderless**.
- In the advanced video settings, **Presentation API** takes effect after restarting. **DirectX 9** is the default. **DirectX 11, DirectX 12, and Vulkan are experimental.** The renderer may fall back to DirectX 9, which the engine uses for much of the 3D scene. [Technical details](renderer-lab/presentation.md)
- Setup includes the frame-rate, graphics, audio, language, window, and HUD patches, plus XenonEffects. [Xbox 360 integration](xbox-360-integration.md)
- Setup installs the controller libraries and default analog mappings. Vibration is enabled by default for devices whose driver exposes compatible motors. Set `Vibration=0` under `[Controller]` in `NFSMW-Unlimited.ini` to turn it off. [Controller support and credits](third-party/controllers/README.md)

## HD interface and cutscenes

- The complete installer embeds the verified Unlimited HD profile and applies it offline. The profile contains LunicAura106's Upscaled HUD and supplemental resources prepared from the game installation. [Upscaled HUD by LunicAura106](https://nfsmods.xyz/mod/1096)
- The historical GitHub release installers are runtime-only previews. Their optional HUD importer accepts a previously prepared Unlimited HD game folder on the **HD game assets** page or via `/HDHUD="C:\Games\Unlimited HD Source"`. [GitHub releases](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases)
- To import replacement cutscenes you have prepared, choose the folder containing the HD VP6 files or pass `/HDMOVIES="C:\Games\HD Movies"`. Setup verifies the original filename, video dimensions, frame rate, and packet structure before copying to `MOVIES_HD`. The original movies stay in place.

## Build

Use LLVM-MinGW for x86, the Windows and Vulkan SDKs, and Inno Setup 6. Run `installer/build.ps1` from PowerShell. The default build reads the verified HD profile from `Juego` and writes the complete `dist/NFSMW-Unlimited-Setup.exe`. Use `-HudGameDirectory` to specify another folder containing that profile. A missing file or checksum mismatch stops the build.

The complete executable contains archives from the local game installation. `-RuntimeOnly` builds the separately named `NFSMW-Unlimited-Runtime-Setup.exe` for distribution alongside the source. Use `-Compiler` or `-LegacyUcrt` to specify the Inno Setup compiler or the x86 SDK redistributables.

Bundled controller libraries retain [their original licenses](third-party/controllers/Licenses).
