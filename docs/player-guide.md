# Player guide

Unlimited is a community patch for the original Windows PC 1.3 release of Need for Speed Most Wanted (2005). Setup validates the executable and looks for the game's track and global resource directories. The game is launched through its normal shortcut or `speed.exe`.

## Choose a download

| Situation | Download |
| --- | --- |
| You want the latest stable, complete HD installer | [Stable release](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/latest) |
| You want to test the current beta on a supported PC installation | [v1.1.0-beta.1 runtime installer](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/tag/v1.1.0-beta.1) |
| You build your own complete installer with compatible asset packages | [Build guide](build.md) |

The public beta installer contains the runtime, DISCX decoder, controller support, and the XenonEffects add-on. It works on a supported plain PC installation and can update an existing Unlimited installation. It also offers optional import of a verified HD resource directory and compatible HD cutscenes. The remastered texture bank and complete Xbox 360 Stuff Pack integration require separately prepared assets.

## Install or update the beta

1. Close Most Wanted. Copy `Documents\NFS Most Wanted` and, if present, `Documents\Unlimited Save Recovery` somewhere safe.
2. Download `NFSMW-Unlimited-Runtime-Setup.exe` and `SHA256SUMS.txt` from the beta release. To verify the installer, run `Get-FileHash .\NFSMW-Unlimited-Runtime-Setup.exe -Algorithm SHA256` in PowerShell and compare the result with the published checksum.
3. Run Setup and select the folder containing the supported `speed.exe`. Leave the optional asset fields blank to install the runtime on its own.
4. Open the game. DirectX 9 is the default for a fresh installation. Changes to the renderer, texture profile and audio output take effect after a restart.

Setup retains an existing `NFSMW-Unlimited.ini` and user music. It keeps original copies of replaced files in `UnlimitedCommunity\BeforeUnlimited`. On a supported 64-bit Windows installation, Setup can enable Large Address Aware on the verified x86 executable, backing up the original file.

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