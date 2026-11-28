# Need for Speed Most Wanted 2005 Unlimited

**Unlimited is an unofficial engine patch for Need for Speed: Most Wanted (2005), PC 1.3, developed by XssentialPilar.** It integrates compatible Xbox 360 and remaster assets into the PC game's own loading and rendering systems, extends local split screen, and adds DISCX, new display controls and protection against save corruption.

[Latest release](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/latest) · [Player guide](docs/player-guide.md) · [Changelog](CHANGELOG.md) · [Credits](CREDITS.md) · [MIT license](LICENSE)

## What makes Unlimited different

### Native integration of external assets

Unlimited converts compatible files from external packs into resources the PC game loads directly. Its build tools map textures to PC resource IDs, compile the Xbox 360 and optional 8K texture banks, prepare garage archives and lighting databases, and validate console audio for the game's sound banks. Unlimited adapts the upstream renderer's startup and shader lookup so the game can load it in-process on DirectX 9. The original packs serve as build inputs. Setup installs the converted resources, and players launch the game through the usual `speed.exe` and control the effects from the Video menu. [Xbox 360 integration and asset inventory](xbox-360-integration.md) · [Build pipeline](docs/build.md)

### Local split screen

Unlimited builds on Abdullah374's restoration of the PC split-screen code and his four-player engine hooks. It adapts race setup for **two, three and four players** in supported Quick Race and Custom Race events, with **Local controls** for device assignment, independent bindings and shared keyboard play. Unlimited scales the race HUD to each viewport and updates each driver's minimap and tachometer independently. On DirectX 9 it runs the Xbox 360 world passes per view with separate reflection and post-processing state. Adaptive scene resolution reduces the rendering cost while each HUD draws at full output resolution. [Split-screen captures](renderer-lab/xbox360-split-screen.jpg) · [Development and upstream credits](CREDITS.md)

### DISCX, display and save recovery

- **DISCX and Unlimited Matrix.** Play your own music through the game's mixer, song controls and native song card with embedded cover art. The interactive score retains priority when requested by the game. Stereo output offers **Unlimited Matrix**, which encodes the 5.1 mix as stereo Lt/Rt for compatible matrix decoders. [Music and audio settings](docs/player-guide.md#discx-and-sound)
- **Display and interface.** Choose fullscreen, windowed or borderless mode, set an FPS cap or select **Unlimited**, and change the 3D scene resolution independently of the HUD. The HD interface scales with the display, and the in-game language selector can reload installed languages and save subtitle preferences. [Display and performance](docs/player-guide.md#display-and-performance)
- **Save protection.** Unlimited prevents failed or incomplete writes from corrupting your save and costing you career progress. It validates each manual save and autosave before replacing the active profile and checks for conflicting external edits. If the active file is damaged, Unlimited can recover progress from a verified, compatible revision. [Save recovery guide](docs/save-recovery.md)

## Downloads and setup

Current release **v1.1.0-beta.1**. Setup requires a supported PC 1.3 installation on Windows 7 SP1 or later with x86 application support.

**Simple, all-in-one setup.** The compact and full editions each use a single installer. Select your existing PC 1.3 game folder and Setup checks the executable, installs the patch and game-ready assets for your edition, and supplies the default configuration on a fresh install. The original community archives and conversion tools are used when building from source. After Setup finishes, launch Most Wanted from your usual shortcut.

| Download | Contents |
| --- | --- |
| [Compact installer · 501.7 MB](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/NFSMW-Unlimited-Compact-Setup.exe) | Single installer with the patch, HD interface, local split screen, DISCX, Xbox 360 graphics and engine sounds. It keeps all seven texture settings and uses the available textures. The full edition supplies the 8K remaster and Xbox 360 soundtrack. |
| [Full installer · 2.7 GB](https://drive.google.com/drive/folders/1l_FDzXc2m_Aq71TXzJtKQNYlowg-eF3g?usp=sharing) | Most complete edition. Includes everything in the compact edition plus the 8K remaster and Xbox 360 soundtrack. |
| [Console update · 255 MB](https://drive.google.com/drive/folders/1l_FDzXc2m_Aq71TXzJtKQNYlowg-eF3g?usp=sharing) | Updates an existing Unlimited installation that already has the Xbox 360 texture bank. |
| [Download guide ZIP · 12 languages](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/NFSMW-Unlimited-Download-Guide.zip) | Google Drive link and instructions for choosing and running the installers. |

1. Close the game and back up your saved profiles.
2. Download the installer for your installation. Verify GitHub downloads against [SHA256SUMS.txt](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/SHA256SUMS.txt).
3. Run Setup and select the folder containing the supported PC 1.3 `speed.exe`. Launch the game from your usual shortcut.
4. For the Xbox 360 world shaders, select **DirectX 9**, turn on **Xbox 360 effects** in the Video menu and restart.

Setup backs up replaced files under `UnlimitedCommunity/BeforeUnlimited`. DirectX 11, DirectX 12 and Vulkan presentation modes are experimental. [Player guide](docs/player-guide.md) · [Beta verification and known limits](docs/verification-1.1.0-beta.1.md)

## Credits and licensing

XssentialPilar develops Unlimited's original features, in-engine adaptations, asset conversion tools and installer. The credits list the upstream code and assets that support each area. The **MIT License** covers Unlimited's original code and documentation. Third-party components retain their own terms. [Credits and upstream sources](CREDITS.md) · [Project license](LICENSE)

## Help and development

- [Player guide](docs/player-guide.md) · [Save recovery](docs/save-recovery.md) · [Report an issue](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/issues/new)
- [Build guide](docs/build.md) · [Xbox 360 integration](xbox-360-integration.md) · [Renderer lab](renderer-lab/README.md) · [Changelog](CHANGELOG.md)
