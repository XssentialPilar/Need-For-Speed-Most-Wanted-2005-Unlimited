# Player guide

Unlimited is a community patch for the original Windows PC 1.3 release of Need for Speed Most Wanted (2005). Setup validates the executable and looks for the game's track and global resource directories. The game is launched through its normal shortcut or `speed.exe`.

## Choose a download

| Situation | Download |
| --- | --- |
| You want one installer with the HD interface, DISCX, split screen and Xbox 360 graphics | [Compact installer (501.7 MB)](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/NFSMW-Unlimited-Compact-Setup.exe) |
| You want the most complete edition, with the 8K texture remaster and Xbox 360 soundtrack | [Full installer (2.7 GB) on Google Drive](https://drive.google.com/drive/folders/1l_FDzXc2m_Aq71TXzJtKQNYlowg-eF3g?usp=sharing) |
| Your Unlimited installation already has the Xbox 360 texture bank | [Console update (255 MB) on Google Drive](https://drive.google.com/drive/folders/1l_FDzXc2m_Aq71TXzJtKQNYlowg-eF3g?usp=sharing) |
| You want the Google Drive links and instructions in another language | [Download guide ZIP, 12 languages](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/NFSMW-Unlimited-Download-Guide.zip) |

The compact installer includes the same console graphics integration and engine sounds as the complete edition. It omits the 8K remaster and Xbox 360 soundtrack. It keeps all seven texture quality choices and uses the installed texture data. The game's original PC soundtrack is used when a compatible console soundtrack bank is unavailable.

## Install or update

1. Close Most Wanted. Copy `Documents\NFS Most Wanted` and, if present, `Documents\Unlimited Save Recovery` somewhere safe.
2. Download one installer. To verify the compact installer, run `Get-FileHash .\NFSMW-Unlimited-Compact-Setup.exe -Algorithm SHA256` in PowerShell and compare the result with [SHA256SUMS.txt](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/SHA256SUMS.txt).
3. Run Setup and select the folder containing the supported PC 1.3 `speed.exe`. If you downloaded the Drive guide ZIP, extract it and open the README for your language before downloading an installer.
4. Launch the game. Select DirectX 9 and enable **Xbox 360 effects** in Video to use the console renderer. Restart after changing the render API, console effects, texture quality or audio output.

Setup retains an existing `NFSMW-Unlimited.ini` and user music. It keeps original copies of replaced files in `UnlimitedCommunity\BeforeUnlimited`. On supported 64-bit Windows, Setup enables Large Address Aware on the verified x86 executable and backs up the original. This allows the 32-bit process up to 4 GB of virtual address space.

## Local split screen

Open **Quick Race → Split Screen** and choose two, three or four players. In supported Custom Race events, the player count is available during race setup. Connect controllers before joining or share a keyboard. Press **Enter** to join on keyboard and **Tab** to add another keyboard player. Use **Local controls** to assign devices and save independent bindings. Default driving layouts use the arrow keys, **WASD**, **IJKL**, and numpad **8/5/4/6**. Each pane has its own camera, position table, minimap and tachometer. The tachometer needle measures engine RPM. The numeric value displays vehicle speed.

With three players, two views appear on top and one below, with one AI opponent. Four players use four quadrants. Tollbooth events support one player. Split-screen Quick Race and Custom Race disable police and Speedbreaker.

The resolution scale in Advanced Effects applies to each pane's scene. The HUD uses the display resolution. Both installers include the Xbox 360 world renderer and its DirectX 9 shader profiles. [Xbox 360 settings](../xbox-360-integration.md)

## DISCX and sound

Place your audio files in `UnlimitedMusic`. Open **Options → DISCX**, use **Refresh** after changing the library, and select a track. You can browse by title, album, artist or composer. The card uses the current track's tags and embedded front cover. Tracks without readable artwork use the EA logo.

The **Sound** menu shows the toggle for the selected music source. To return to the original soundtrack, open **EA Trax** in the Options carousel. The game's licensed-music controls, volume and scene-specific filtering apply to DISCX. The interactive score retains priority in scenes that request it.

**Automatic** chooses a speaker configuration based on the Windows playback device. **Stereo** exposes **Unlimited Matrix**, which encodes the surround mix as two-channel Lt/Rt for a compatible matrix decoder. **Output** also offers **5.1** and **7.1** when supported by the Windows playback device. Restart after changing the output mode or Unlimited Matrix.

The player supports FLAC, WAV, MP3, Opus, Ogg Vorbis, AAC, M4A, AIFF, Monkey's Audio, and raw PCM. Use the playback controls to pause, stop, seek, skip or repeat, and turn on **Shuffle** to randomize the queue. Under **Advanced Effects → EA Trax**, **Enable all** adds all 26 tracks in the original PC bank to the menu and race playlists. New profiles start with all of them enabled.

Raw `.pcm` files default to stereo 44.1 kHz signed 16-bit little-endian audio. Put a sidecar such as `Track.pcm.ini` next to the audio file to specify another format or custom tags.

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

[DISCX decoder and FFmpeg source](../third-party/discx-codec/README.md)

## Display and performance

- **Video → Frame limit** offers **30**, **40**, **60**, **70**, **90**, **144**, **Unlimited**, and **Automatic** for menus and races. Vertical sync and driver limits still apply. On systems with up to two logical processors available to the game, **Automatic** uses a 60 FPS cap. Those systems also use the original 60 Hz simulation step and cubemap refresh pattern and default to a scene scale of 0.8. Manual FPS and scene scale settings still apply.
- Use **Video → Advanced → Advanced effects** to adjust motion blur, game filters, mirror effects and **Resolution scale**. Scene scale ranges from **0.4** to **2.0**, with **1.0** as standard. Lowering it reduces rendering work while keeping the HUD at output resolution. Split screen applies the chosen scale to each view's adaptive scene resolution.
- Medium and high shadow quality request 1024 px and 2048 px maps, respectively. The patch can use smaller maps when the device cannot allocate the requested size.
- **Video → Display mode** offers **Fullscreen**, **Windowed**, and **Borderless**. Changes to **Presentation API** take effect after restarting. DirectX 9 supports the Xbox 360 world shaders, and the DirectX 11, DirectX 12 and Vulkan presentation modes are experimental. [Technical details](../renderer-lab/presentation.md)
- Optional timing logs are written to `%TEMP%\NFSMW-graphics.log` after enabling `FrameStatistics=1` under `[Diagnostics]` in `NFSMW-Unlimited.ini`. Disable the option after collecting a trace.

## Textures, language and cutscenes

- In **Video → Advanced → Advanced effects → Textures**, choose **Very Low** (256 px), **Low** (512 px), **Medium** (1024 px), **High** (2048 px), **Very High** (4096 px), **Ultra** (8192 px), or **Original** (default), then restart. The pixel limits apply to the remaster in the full edition. The compact edition applies the same menu selections to the assets it has installed. Unlimited chooses smaller mip levels or uses the original resource when memory runs low. [Asset details](../xbox-360-integration.md)
- Open **Options → Languages** to select an installed language and toggle subtitles. Confirm to reload the interface language in the current session. The patch saves subtitle preferences per language. Spoken cutscene audio comes from the installed movies.
- The compact and full installers apply the verified HD interface offline. To import compatible 1920 × 1080 VP6 cutscenes, supply their folder to Setup or use `/HDMOVIES="C:\Games\HD Movies"`. Setup verifies the filenames, frame rates and packet structure before copying to `MOVIES_HD`. The cutscene loader can fall back to the original movie. [Cutscene requirements](../xbox-360-integration.md#full-hd-cutscenes)
- Controller vibration defaults to on when the driver exposes compatible motors. Set `Vibration=0` under `[Controller]` in `NFSMW-Unlimited.ini` to disable it. [Controller support](../third-party/controllers/README.md)

## Save recovery and problem reports

If a profile is rejected, use the recovery steps before restoring or replacing files manually. [Save recovery guide](save-recovery.md)

When reporting a problem, include the release tag, Windows version, resolution, presentation API, graphics options, reproduction steps, and a description of any optional asset packs. For races, include the track, mode and player count. Share only relevant log lines and remove personal paths and profile information. [Report an issue](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/issues/new)