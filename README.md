# Need for Speed Most Wanted 2005 Unlimited

Unlimited is an unofficial engine and compatibility patch for **Need for Speed: Most Wanted (2005), PC version 1.3**, developed and maintained by **XssentialPilar**. Install it over a supported copy of the game and start it from the usual shortcut. The patch works inside the PC game process and exposes its main features through the game's own menus.

[Latest release](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/latest) · [Changelog](CHANGELOG.md) · [Credits](CREDITS.md) · [MIT license](LICENSE) · [Report an issue](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/issues)

## What Unlimited adds

| Area | What changes in the game |
| --- | --- |
| Local multiplayer | Play on one PC with two, three or four players in supported Quick Race and Custom Race events. Each player gets their own camera, HUD, minimap, RPM needle and control bindings. |
| Music and sound | Play your own local music through **Options → DISCX**. It follows the game's music controls, transitions and volume, and uses the in-game song card. You can switch back to EA Trax. Sound settings also offer Windows speaker detection and **Unlimited Matrix** stereo output. |
| Graphics and display | Choose fullscreen, windowed or borderless mode, set an FPS cap or select **Unlimited**, and adjust scene resolution independently of the HUD. DirectX 9 supports the integrated Xbox 360 shader profiles, lighting, textures and garage resources. |
| Interface | The HD HUD adapts to the display and each local player's viewport. The patch groups and positions race standings, pursuit panels and song cards, and adds an in-game language and subtitle selector. |
| Saved profiles | Manual saves and autosaves validate a staged native PC profile before replacing the active save. Unlimited keeps validated revisions and can recover a compatible copy when the active file is damaged. |

## What makes Unlimited different

- **Your music follows the game's soundtrack rules.** The DISCX player routes imported music through the game's mixer, follows the menu and race volumes, and uses the game's song-change command and the native EA Trax scheduler for card timing and animation. The card shows embedded cover art and track details. The interactive score keeps priority when the game requests it, and the Sound menu controls the selected music source.
- **Local multiplayer and Xbox 360 graphics work together.** Abdullah374's restoration supplies the split-screen foundation, and Xbox 360 Stuff Pack 4.2 supplies the console renderer and shaders. Unlimited coordinates the two inside the PC engine. Each player gets separate scene passes, reflections, post-processing history and a correctly anchored HUD. Adaptive scene resolution reduces rendering work per view while the interface stays sharp at output resolution.
- **Graphics packs become managed game resources.** Unlimited's build tools convert compatible assets into banks and archives that the PC engine can load. The in-game settings select shader profiles and texture quality. The texture loader chooses mip levels based on memory availability and falls back to the installed game resources when needed. The compact installer bundles the console graphics and engine sounds, and the full installer adds the optional 8K bank and Xbox 360 soundtrack.
- **Saves remain native PC profiles with recovery built in.** Manual saves and autosaves validate the new file before replacing the current one. Unlimited retains good revisions, checks for external edits and can recover a compatible revision if the game rejects the active file.

DISCX, Unlimited Matrix, the save layer, the integration tooling and the combined runtime integration are developed by XssentialPilar. The credits document identifies the upstream work used by each integration. [Development credits and upstream sources](CREDITS.md) · [Beta verification and known limits](docs/verification-1.1.0-beta.1.md)

## Downloads and setup

| Download | When to use it |
| --- | --- |
| [Compact installer (v1.1.0-beta.1, 501.7 MB)](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/NFSMW-Unlimited-Compact-Setup.exe) | Recommended for most players. Includes the HD interface, DISCX, local split screen and Xbox 360 graphics and engine sounds. This edition omits the 8K remaster and Xbox 360 soundtrack. |
| [Full installer and console update on Google Drive](https://drive.google.com/drive/folders/1l_FDzXc2m_Aq71TXzJtKQNYlowg-eF3g?usp=sharing) | Choose the full installer (2.7 GB) for the most complete edition, including the 8K remaster and Xbox 360 soundtrack. Choose the console update (255 MB) if Unlimited is already installed with the Xbox 360 texture bank. |
| [Download guide ZIP, 12 languages](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/NFSMW-Unlimited-Download-Guide.zip) | Contains the Google Drive link and instructions for choosing and running either installer. |

1. Close Most Wanted and back up your saved profiles.
2. Download the installer for your installation. Use [SHA256SUMS.txt](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/SHA256SUMS.txt) to verify GitHub downloads. The compact installer is a single Windows executable.
3. Run Setup, select the folder containing the supported PC 1.3 `speed.exe`, and launch the game when Setup completes.
4. To use Xbox 360 graphics, select DirectX 9, turn on **Xbox 360 effects** in the Video menu, and restart the game.

Setup targets Windows 7 SP1 or later on systems that run x86 applications. It checks the installed game executable and keeps original copies of replaced files in `UnlimitedCommunity/BeforeUnlimited`. [Release notes](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/tag/v1.1.0-beta.1) · [Player guide](docs/player-guide.md) · [Build guide](docs/build.md)

On 64-bit Windows, Setup enables Large Address Aware for the verified PC 1.3 executable and saves the original in `UnlimitedCommunity/BeforeUnlimited/speed.exe`. This gives the 32-bit process up to 4 GB of virtual address space.

## Saved profiles

Manual saves and autosaves stage and validate the native PC profile before replacing it. Unlimited keeps recent valid revisions and known-good copies under `Documents/Unlimited Save Recovery`. It can recover a damaged file or offer a confirmed copy when a matching layout exists. [Save and recovery guide](docs/save-recovery.md)

## Display and controls

- The minimap, speedometer, pursuit panel, and music notice each move as a group. Their corner margins scale with the shorter side of the viewport. The song notice uses a smaller proportional size so it fits beside the race HUD.
- In the video settings, **Display mode** offers **Fullscreen**, **Windowed**, and **Borderless**.
- **Frame limit** offers **30**, **40**, **60**, **70**, **90**, **144**, **Unlimited**, and **Automatic** for menus and races, including on the Xbox 360 renderer. Vertical sync and driver settings can still limit presentation. Set `FrameStatistics=1` under `[Diagnostics]` in `NFSMW-Unlimited.ini` to log frame timing.
- When the game has up to two logical processors available, **Automatic** caps rendering at 60 FPS. On those systems Unlimited also uses the native 60 Hz simulation step, the original cubemap refresh pattern, and a default resolution scale of 0.8. Existing manual FPS and resolution scale preferences still apply. The shadow patch requests 1024-pixel maps at medium quality and 2048-pixel maps at high quality, with fallback for devices that cannot allocate the requested size.
- In the advanced video settings, **Presentation API** takes effect after restarting. **DirectX 9** is the default. **DirectX 11, DirectX 12, and Vulkan are experimental.** The renderer may fall back to DirectX 9, which the engine uses for much of the 3D scene. [Technical details](renderer-lab/presentation.md)
- From **Video → Advanced → Advanced effects**, switch game filters and motion blur independently, and adjust the motion blur intensity when enabled. **Resolution scale** runs from **0.4** to **2.0**, with **1.0** as the standard scene resolution. In split screen the selected value multiplies each view's adaptive resolution. The HUD keeps its output resolution.
- **Advanced Effects → EA Trax → Enable all** restores the 26 tracks in the original PC bank to both the menu and race playlists. New profiles start with all of them enabled. The **EA Trax** screen still lets you choose where specific songs play.
- **Xbox 360 effects** on DirectX 9 enables integrated console visuals. The four shader profiles are Standard, Lower-cost, Sharp Trees, and Soft Shadows. Setup also bundles XenonEffects for sparks and air trails. [Xbox 360 settings, assets and verification](xbox-360-integration.md)
- In the sound settings, **Output** offers **Automatic**, **Stereo**, **5.1**, and **7.1**. Changes take effect on restart and depend on the channels supported by the Windows playback device. The game applies a light per-channel tone adjustment.
- Open **Languages** from the title menu's **Options** to select an installed game language and set **Subtitles** on or off. Confirm to reload the game's language resources during the session. Subtitle preferences are saved for each language. Spoken cutscene audio comes from the installed movies.
- Setup installs the controller libraries and default analog mappings. Vibration is enabled by default for devices whose driver exposes compatible motors. Set `Vibration=0` under `[Controller]` in `NFSMW-Unlimited.ini` to turn it off. [Controller support and credits](third-party/controllers/README.md)

## Local split screen

- In **Quick Race**, choose a local split-screen mode for **2**, **3**, or **4 players**. In **Quick Race → Custom Race**, choose a track, then select **1**, **2**, **3**, or **4 players**. Tollbooth events support one player.
- Everyone selects a car and drives with a separate camera and HUD. Three-player split screen places the first two views on top and the third below, with one AI opponent. Four-player split screen uses four quadrants.
- Use up to four controllers or a shared keyboard. Press **Enter** to join on keyboard and **Tab** to add another keyboard player. The default driving layouts use the arrow keys, **WASD**, **IJKL**, and numpad **8/5/4/6**. Use **Local controls** to assign an available controller and save independent keyboard and controller bindings for each player.
- Each player gets the single-player race HUD, scaled and anchored inside their viewport. Each minimap follows its own driver, and each tachometer needle tracks that car's engine speed. The 3D scene renders at an adaptive per-viewport resolution while the HUD draws at output resolution.
- Local views use the graphics settings selected in the game. Each view has its own post-processing history and car reflections. The reduced scene targets preserve the active multisampling mode.
- The standings stay grouped at the upper right of each view and scale with its proportions. Local Quick Race and Custom Race disable police and Speedbreaker. The police heat display requires a race that enables pursuits.
- The startup screen preserves the original artwork proportions and fits the localized text inside the visible area.
- Split-screen restoration and the four-player engine hooks build on Abdullah374's MIT-licensed mod. [Upstream source](https://github.com/Abdullah374/NFS_MW_SplitScreenMOD/tree/v2.4.1) · [License](third-party/split-screen/LICENSE)

## DISCX

Copy your music into `UnlimitedMusic` inside the game folder. **Options → DISCX**, beside **EA Trax**, scans this folder and its subfolders. Use **Refresh** after adding or removing tracks. Browse by song, album, artist, or composer with the mouse wheel or directional controls. Select a track to start playback and toggle **Shuffle** to randomize the queue. The player includes pause, stop, previous and next track, seeking, and repeat. Adjust music and master volume in the game's sound settings.

The native **Sound** menu shows a Yes/No switch named after the active source, **EA Trax** or **DISCX**. Choose **No** to suspend the current DISCX queue, or open **EA Trax** in the Options carousel to return to the original soundtrack. During normal play, DISCX uses the game's licensed-music routing and next-song command. The interactive score retains priority when the game switches to it. The original music engine continues running silently to drive native transitions, and imported tracks share the same queue across menu and race contexts.

**Sound → Audio output** detects the Windows playback format in Automatic mode. Selecting **Stereo** reveals **Unlimited Matrix**. This option encodes the game's 5.1 mix as two-channel Lt/Rt for matrix surround decoding. Changes to the output mode or Unlimited Matrix take effect after restarting the game.

DISCX supports FLAC, WAV, MP3, Opus, Ogg Vorbis, AAC, M4A, AIFF, Monkey's Audio, and raw PCM. It reads embedded tags and uses the filename if the title tag is missing. **Back** returns to Options and playback continues. The native song card displays the current track's embedded front cover, title, artist, and album. Tracks without readable embedded art display the original EA logo. Decoded music joins the game's playback buffer before tone adjustment, follows the game's music and master levels, and uses the scene-specific cutoff from the native music mixer.

Raw `.pcm` files default to stereo 44.1 kHz signed 16-bit little-endian audio. For other raw formats or custom tags, add a sidecar such as `Track.pcm.ini` beside `Track.pcm`.

```ini
[PCM]
Format=s24le
SampleRate=48000
Channels=2
[Tags]
Title=My Track
Artist=My Artist
Album=My Album
Composer=My Composer
```

The installer includes the replaceable DISCX decoder, its adapter and build recipe, and the corresponding FFmpeg source and notices. [Decoder details](third-party/discx-codec/README.md)

## Remastered textures

In **Video → Advanced → Advanced effects → Textures**, select a profile and restart the game. The menu lists **Very Low** (256 px), **Low** (512 px), **Medium** (1024 px), **High** (2048 px), **Very High** (4096 px), and **Ultra** (8192 px), followed by **Original**. Original is the default. The caps apply to remastered textures and are shared across split-screen views. High, Very High, and Ultra prioritize large textures as the game binds their materials and release remastered copies once they are no longer in use. The loader chooses lower mip levels or the game's original texture as the available system memory, address space, and GPU memory change.

The full installer embeds the community texture remaster in `GLOBAL/UnlimitedTextures.bin`. Unlimited built the compiler and runtime loader that feed the remastered data into the game's DirectX 9 texture path. The installed game loads the texture bank directly and retains the original track, garage and global resources for fallback. The compact installer keeps all seven choices and applies them to available assets, including the bundled console textures when Xbox 360 effects are enabled. [Texture pack credits](CREDITS.md)

## HD interface and cutscenes

- The compact and complete installers embed the verified Unlimited HD profile and apply it offline. The profile contains LunicAura106's Upscaled HUD and supplemental resources prepared from the game installation. [Upscaled HUD by LunicAura106](https://nfsmods.xyz/mod/1096)
- To import replacement cutscenes you have prepared, choose the folder containing the HD VP6 files or pass `/HDMOVIES="C:\Games\HD Movies"`. Setup verifies the original filename, video dimensions, frame rate, and packet structure before copying to `MOVIES_HD`. The original movies stay in place.

## Credits and licensing

XssentialPilar maintains Unlimited and its original runtime, integration, installer and testing code. Community authors retain credit for the upstream code, tools and artwork that Unlimited uses or adapts. See the contribution and provenance notes for each component. [Credits and upstream sources](CREDITS.md)

The **MIT License** covers Unlimited's original code and documentation. Upstream code, third-party dependencies and game assets retain their own terms. [Project license](LICENSE) · [Third-party notices](CREDITS.md#licensing-scope)

## Reporting an issue

Include the Unlimited release tag, Windows version, selected presentation API, display resolution, and steps to reproduce the issue. For a split-screen problem, add the race type and player count. Remove personal paths and profile details from any logs you attach. [Open a GitHub issue](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/issues/new)

## Developer documentation

- [Build guide](docs/build.md)
- [Xbox 360 integration and asset inventory](xbox-360-integration.md)
- [Beta verification and performance measurements](docs/verification-1.1.0-beta.1.md)
- [Changelog](CHANGELOG.md)
- [Renderer lab](renderer-lab/README.md)
- [Bundled controller licenses](third-party/controllers/Licenses)
