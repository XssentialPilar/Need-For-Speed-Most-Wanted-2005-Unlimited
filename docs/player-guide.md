# Player guide

Unlimited is a community patch for the original Windows PC 1.3 release of Need for Speed Most Wanted (2005). Setup validates the executable and looks for the game's track and global resource directories. The game is launched through its normal shortcut or `speed.exe`.

## Choose a download

| Situation | Download |
| --- | --- |
| You want one installer with the HD interface, DISCX, split screen and Xbox 360 graphics | [Compact installer (501.7 MB)](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/NFSMW-Unlimited-Compact-Setup.exe) |
| You also want the 8K texture remaster and Xbox 360 soundtrack | [Full installer (2.7 GB) on Google Drive](https://drive.google.com/drive/folders/1l_FDzXc2m_Aq71TXzJtKQNYlowg-eF3g?usp=sharing) |
| Your Unlimited installation already has the Xbox 360 texture bank | [Console update (255 MB) on Google Drive](https://drive.google.com/drive/folders/1l_FDzXc2m_Aq71TXzJtKQNYlowg-eF3g?usp=sharing) |
| You want the Google Drive links and instructions in another language | [Download guide ZIP, 12 languages](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/NFSMW-Unlimited-Download-Guide.zip) |

The compact installer includes the same console graphics integration and engine sounds as the complete edition. It keeps the seven texture quality choices and uses the installed texture data. The game's original PC soundtrack is used when a compatible console soundtrack bank is unavailable.

## Install or update

1. Close Most Wanted. Copy `Documents\NFS Most Wanted` and, if present, `Documents\Unlimited Save Recovery` somewhere safe.
2. Download one installer. To verify the compact installer, run `Get-FileHash .\NFSMW-Unlimited-Compact-Setup.exe -Algorithm SHA256` in PowerShell and compare the result with [SHA256SUMS.txt](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/download/v1.1.0-beta.1/SHA256SUMS.txt).
3. Run Setup and select the folder containing the supported PC 1.3 `speed.exe`. If you downloaded the Drive guide ZIP, extract it and open the README for your language before downloading an installer.
4. Launch the game. Select DirectX 9 and enable **Xbox 360 effects** in Video to use the console renderer. Restart after changing the render API, console effects, texture quality or audio output.

Setup retains an existing `NFSMW-Unlimited.ini` and user music. It keeps original copies of replaced files in `UnlimitedCommunity\BeforeUnlimited`. On supported 64-bit Windows, Setup enables Large Address Aware on the verified x86 executable and backs up the original.

## Local split screen

Open **Quick Race → Split Screen** and choose two, three or four players. In supported Custom Race events, the player count is available during race setup. Connect controllers before joining, then assign a device to each local player. Use Unlimited's Controls menu to configure bindings. Each pane has its own camera, position table, minimap and tachometer. The tachometer needle measures engine RPM. The numeric value displays vehicle speed.

The resolution scale in Advanced Effects applies to each pane's scene. The HUD uses the display resolution. The Xbox 360 world renderer and its profiles are available on DirectX 9 when the complete graphics assets are installed. [Xbox 360 settings](../xbox-360-integration.md)

## DISCX and sound

Place your audio files in `UnlimitedMusic`. Open **Options → DISCX**, use **Refresh** after changing the library, and select a track. You can browse by title, album, artist or composer. The card uses the current track's tags and embedded front cover. Tracks without readable artwork use the EA logo.

The **Sound** menu shows the toggle for the selected music source. To return to the original soundtrack, open **EA Trax** in the Options carousel. The game's licensed-music controls, volume and scene-specific filtering apply to DISCX. The interactive score retains priority in scenes that request it.

**Automatic** chooses a speaker configuration based on the Windows playback device. **Stereo** exposes **Unlimited Matrix**, which encodes the surround mix as two-channel Lt/Rt for a compatible matrix decoder. Restart the game after changing the output mode.

## Display and performance

- Use **Video → Frame limit** to select a fixed cap, **Unlimited** or **Automatic**. Vertical sync and driver-level limits can still affect presentation. Automatic uses 60 FPS on a process with at most two available logical processors.
- Use **Video → Advanced → Advanced effects** to adjust motion blur, game filters, mirror effects and resolution scale. Lowering scene scale reduces rendering work while keeping the HUD at output resolution.
- The D3D11, D3D12 and Vulkan presentation modes are experimental. DirectX 9 is the supported path for the Xbox 360 world shaders.
- Optional timing logs are written to `%TEMP%\NFSMW-graphics.log` after enabling `FrameStatistics=1` under `[Diagnostics]` in `NFSMW-Unlimited.ini`. Disable the option after collecting a trace.

## Save recovery and problem reports

If a profile is rejected, use the recovery steps before restoring or replacing files manually. [Save recovery guide](save-recovery.md)

When reporting a problem, include the release tag, Windows version, resolution, presentation API, graphics options, reproduction steps, and a description of any optional asset packs. For races, include the track, mode and player count. Share only relevant log lines and remove personal paths and profile information. [Report an issue](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/issues/new)