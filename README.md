# Need for Speed Most Wanted 2005 Unlimited

A patch for the 2005 PC release. The installer supports Windows 7 SP1 and later on systems that can run x86 applications. Unlimited requires a compatible installation of the original game with the 1.3 executable.

## Installation

Download **NFSMW-Unlimited-Setup.exe**, choose a setup language, and select the original game folder containing `speed.exe`. Setup validates the executable, installs the patch and controller libraries, and preserves existing settings. Backups of replaced files are written under `UnlimitedCommunity/BeforeUnlimited` inside the game folder. [Releases](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases)

Setup is available in English, Spanish, French, German, Italian, Dutch, Portuguese, Polish, Russian, Japanese, Swedish, and Danish.

## Display and controls

- The minimap, speedometer, pursuit panel, and EA Trax notice each move as a group. Their corner margins scale with the shorter side of the viewport, and their artwork retains its proportions.
- In the video settings, **Display mode** offers **Fullscreen**, **Windowed**, and **Borderless**.
- In the advanced video settings, **Presentation API** takes effect after restarting. **DirectX 9** is the default. **DirectX 11, DirectX 12, and Vulkan are experimental.** The renderer may fall back to DirectX 9, which the engine uses for much of the 3D scene. [Technical details](renderer-lab/presentation.md)
- Setup includes the frame-rate, graphics, audio, language, window, and HUD patches, plus XenonEffects. [Xbox 360 integration](xbox-360-integration.md)
- Setup installs the controller libraries and default analog mappings. Vibration is enabled by default for devices whose driver exposes compatible motors. Set `Vibration=0` under `[Controller]` in `NFSMW-Unlimited.ini` to turn it off. [Controller support and credits](third-party/controllers/README.md)

## HD interface and cutscenes

- To transfer a previously prepared Unlimited HD interface, select its game folder on the **HD game assets** page. The importer verifies the 295 replacement files and the target game archives before replacing any asset. It can also be run with `/HDHUD="C:\Games\Unlimited HD Source"`.
- For a first-time installation of the community interface, follow the author's setup instructions for the original Upscaled HUD and then run Unlimited Setup with the HD HUD source field blank. Setup preserves the installed art and adds the responsive layout at runtime. The full Unlimited HD profile also contains supplemental resources from the local build. [Upscaled HUD by LunicAura106](https://nfsmods.xyz/mod/1096)
- To import replacement cutscenes you have prepared, choose the folder containing the HD VP6 files or pass `/HDMOVIES="C:\Games\HD Movies"`. Setup verifies the original filename, video dimensions, frame rate, and packet structure before copying to `MOVIES_HD`. The original movies stay in place.

## Build

Use LLVM-MinGW for x86, the Windows and Vulkan SDKs, and Inno Setup 6. Run `installer/build.ps1` from PowerShell. Use `-Compiler` to specify the Inno Setup path and `-LegacyUcrt` to specify the location of the x86 SDK redistributables.

To build an offline installer for your own copy, run `installer/build.ps1 -HudGameDirectory 'C:\Games\Unlimited HD Source' -OutputDirectory 'dist/local'`. This build embeds your verified HD interface assets and applies them automatically. Keep the resulting **NFSMW-Unlimited-Local-HD-Setup.exe** with your local files. The public release contains the patch and the HD import tools.

Bundled controller libraries retain [their original licenses](third-party/controllers/Licenses).
