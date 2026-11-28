# Need for Speed Most Wanted 2005 Unlimited

Unlimited is a community patch for the 2005 PC release of Need for Speed Most Wanted. It adds an HD interface, local split screen for up to four players, graphics and display controls, and an in-game player for your own music.

[Stable downloads](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/latest) · [Changelog](CHANGELOG.md) · [Report an issue](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/issues)

## Downloads and setup

| Channel | Use it for |
| --- | --- |
| [v1.1.0-beta.1](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/tag/v1.1.0-beta.1) | The latest runtime changes, save recovery, DISCX, and split-screen testing |
| [Stable release](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/latest) | The stable complete HD installer |

1. Close the game and back up your profiles.
2. Download the installer and `SHA256SUMS.txt` from your chosen release. In PowerShell, run `Get-FileHash` on the installer and compare its SHA-256 with the published value.
3. Run Setup and select the existing PC 1.3 installation folder containing `speed.exe`.
4. Launch the game normally. Restart after changing the graphics API, texture profile or audio output.

Setup targets Windows 7 SP1 or later on systems that run x86 applications. It checks the executable and keeps original copies of replaced files in `UnlimitedCommunity/BeforeUnlimited`. The public beta runtime includes an optional HD importer. Build the complete asset integration locally using the matching input directories. [Player guide](docs/player-guide.md) · [Build guide](docs/build.md)

## Saved profiles

Manual saves and autosaves stage and validate the native PC profile before replacing it. Unlimited keeps recent valid revisions and known-good copies under `Documents/Unlimited Save Recovery`. It can recover a damaged file or offer a confirmed copy when a matching layout exists. [Save and recovery guide](docs/save-recovery.md)

## Display and controls

- The minimap, speedometer, pursuit panel, and music notice each move as a group. Their corner margins scale with the shorter side of the viewport. The song notice uses a smaller proportional size so it fits beside the race HUD.
- In the video settings, **Display mode** offers **Fullscreen**, **Windowed**, and **Borderless**.
- **Frame limit** controls menus and races, including the Xbox 360 renderer. Vertical sync and driver settings can still limit presentation. For frame timing logs, set `FrameStatistics=1` under `[Diagnostics]` in `NFSMW-Unlimited.ini`.
- **Frame limit** offers **30**, **40**, **60**, **70**, **90**, **144**, **Unlimited**, and **Automatic**. On systems with up to two logical processors available to the game, Automatic caps rendering at 60 FPS. On those systems Unlimited also uses the native 60 Hz simulation step, the original cubemap refresh pattern, and a default resolution scale of 0.8. Existing manual FPS and resolution scale preferences still apply. The shadow patch requests 1024-pixel maps at medium quality and 2048-pixel maps at high quality, with fallback for devices that cannot allocate the requested size.
- In the advanced video settings, **Presentation API** takes effect after restarting. **DirectX 9** is the default. **DirectX 11, DirectX 12, and Vulkan are experimental.** The renderer may fall back to DirectX 9, which the engine uses for much of the 3D scene. [Technical details](renderer-lab/presentation.md)
- From **Video → Advanced → Advanced effects**, switch game filters and motion blur independently, and adjust the motion blur intensity when enabled. **Resolution scale** runs from **0.4** to **2.0**, with **1.0** as the standard scene resolution. In split screen the selected value multiplies each view's adaptive resolution. The HUD keeps its output resolution.
- **Advanced Effects → EA Trax → Enable all** restores the 26 tracks in the original PC bank to both the menu and race playlists. New profiles start with all of them enabled. The **EA Trax** screen still lets you choose where specific songs play.
- Setup includes the frame-rate, graphics, audio, language, window, and HUD patches, plus XenonEffects. [Xbox 360 integration](xbox-360-integration.md)
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

The native **Sound** menu shows a Yes/No switch named after the active source, **EA Trax** or **DISCX**. Choose **No** to suspend the current Discx queue, or open **EA Trax** in the Options carousel to return to the original soundtrack. During normal play, DISCX uses the game's licensed-music routing and next-song command. The interactive score retains priority when the game switches to it. The original music engine continues running silently to drive native transitions, and imported tracks share the same queue across menu and race contexts.

**Sound → Audio output** detects the Windows playback format in Automatic mode. Selecting **Stereo** reveals **Unlimited Matrix**. This option encodes the game's 5.1 mix as two-channel Lt/Rt for matrix surround decoding. Changes to the output mode or Unlimited Matrix take effect after restarting the game.

Discx supports FLAC, WAV, MP3, Opus, Ogg Vorbis, AAC, M4A, AIFF, Monkey's Audio, and raw PCM. It reads embedded tags and uses the filename if the title tag is missing. **Back** returns to Options and playback continues. The native song card displays the current track's embedded front cover, title, artist, and album. Tracks without readable embedded art display the original EA logo. Decoded music joins the game's playback buffer before tone adjustment, follows the game's music and master levels, and uses the scene-specific cutoff from the native music mixer.

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

The installer includes the [private Discx decoder and its FFmpeg source](third-party/discx-codec/README.md).

On 64-bit Windows, setup enables Large Address Aware for the verified PC 1.3 executable and saves the original in `UnlimitedCommunity/BeforeUnlimited/speed.exe`. This gives the 32-bit process up to 4 GB of virtual address space.

## Remastered textures

In **Video → Advanced → Advanced effects → Textures**, select a profile and restart the game. The menu lists **Very Low** (256 px), **Low** (512 px), **Medium** (1024 px), **High** (2048 px), **Very High** (4096 px), and **Ultra** (8192 px), followed by **Original**. Original is the default. The caps apply to remastered textures and are shared across split-screen views. High, Very High, and Ultra prioritize large textures as the game binds their materials and release remastered copies once they are no longer in use. The loader chooses lower mip levels or the game's original texture as the available system memory, address space, and GPU memory change.

A setup built with the remaster embeds the texture data in `GLOBAL/UnlimitedTextures.bin`. The patch feeds the remastered data into the game's DirectX 9 texture loader. The original track, garage and global resource files remain intact. The installed game reads this resource directly without the source archive, loose DDS files, or NextGenGraphics.

## HD interface and cutscenes

- The stable complete installer and complete local builds embed the verified Unlimited HD profile and apply it offline. The profile contains LunicAura106's Upscaled HUD and supplemental resources prepared from the game installation. [Upscaled HUD by LunicAura106](https://nfsmods.xyz/mod/1096)
- To import replacement cutscenes you have prepared, choose the folder containing the HD VP6 files or pass `/HDMOVIES="C:\Games\HD Movies"`. Setup verifies the original filename, video dimensions, frame rate, and packet structure before copying to `MOVIES_HD`. The original movies stay in place.

## Reporting an issue

Include the Unlimited release tag, Windows version, selected presentation API, display resolution, and steps to reproduce the issue. For a split-screen problem, add the race type and player count. Remove personal paths and profile details from any logs you attach. [Open a GitHub issue](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/issues/new)

## Developer documentation

- [Build guide](docs/build.md)
- [Xbox 360 integration and asset inventory](xbox-360-integration.md)
- [Beta verification and performance measurements](docs/verification-1.1.0-beta.1.md)
- [Changelog](CHANGELOG.md)
- [Renderer lab](renderer-lab/README.md)
- [Bundled controller licenses](third-party/controllers/Licenses)
