# Changelog

## v1.0.1

This maintenance release fixes audio dropouts in **EAX Discx** during gameplay and at automatic track changes.

### EAX Discx fixes

- The decoder refills the playback buffer without blocking the game mixer. A larger buffer absorbs short decoder stalls.
- The next track is buffered before the current one ends. Discx also keeps playing when the original EA Trax stream cycles.
- The decoder preserves pending input samples when resampling audio.

### Installation

Close the game and run **NFSMW-Unlimited-Setup.exe** over your current Unlimited installation. For a fresh installation, select the original PC game folder with a supported 1.3 `speed.exe`. Setup includes the 295 HD resource files and preserves existing settings and music. Use **SHA256SUMS.txt** to verify the download.

### Verification

A two-player split-screen race logged zero dropped PCM frames across 1,059,456 frames and six automatic track changes, including a forced restart of the EA Trax stream. A separate test logged zero missing frames during an injected 450 ms decoder stall. The complete installer passed offline installation and reinstall checks.

## v1.0.0

The first official release of **Need for Speed Most Wanted 2005 Unlimited** brings the HD interface, local split screen, and custom music together in one offline installer for the original PC game.

### Installation

1. Download **NFSMW-Unlimited-Setup.exe** from the release assets. The installer targets Windows 7 SP1 and later on systems that can run x86 applications.
2. Close the game, run Setup, and select the original game folder containing a supported PC 1.3 `speed.exe`. Setup checks the executable and the files it replaces.
3. Launch the game normally. Setup preserves existing settings and music and keeps backups under `UnlimitedCommunity/BeforeUnlimited` inside the game folder.

Setup includes the patch, controller libraries, and 295 HD resource files. English, Spanish, French, German, Italian, Dutch, Portuguese, Polish, Russian, Japanese, Swedish, and Danish are available in Setup. The release includes **SHA256SUMS.txt** for download verification.

### Highlights

- **HD interface and scalable HUD.** The original visual style is preserved. The minimap, tachometer, pursuit panel, and music card adapt to the viewport, and the title screen preserves its artwork proportions.
- **Local split screen.** Play Quick Race and supported Custom Race events with two, three, or four players. Each driver gets an independent camera, minimap, and HUD. Each tachometer now follows its own car's RPM. The 3D scene scales per view while the HUD draws at output resolution.
- **Graphics and display.** Choose Fullscreen, Windowed, or Borderless. Set a frame limit of 30, 40, 60, 70, 90, or 144 FPS, or choose Unlimited. Advanced options include game filters, motion blur with adjustable intensity, and scene resolution scaling from 0.4× to 2.0×. The patch includes higher-resolution shadows, revised reflections, and optional Xbox 360-inspired effects.
- **EAX Discx.** Put your own audio files in `UnlimitedMusic`, then open **Options → EAX Discx**. Browse by song, album, artist, or composer. Supported formats include FLAC, WAV, MP3, Opus, and PCM. The music card reads embedded tags and cover art and uses the EA logo when a track has no readable cover. Starting a custom track makes **EAX Discx** the music toggle in **Sound**. Opening **EA Trax** switches back to the original soundtrack.
- **Audio, languages, and controls.** Sound output offers Automatic, Stereo, 5.1, and 7.1, subject to device support. Change the game language from the title menu and save subtitle preferences per language. Local controls support per-player keyboard and controller bindings, with vibration on supported devices.

### Compatibility and limitations

- **DirectX 9 remains the default.** DirectX 11, DirectX 12, and Vulkan are experimental presentation options. Most scene rendering still uses Direct3D 9. The game can fall back to DirectX 9, and presentation changes require a restart.
- **Discx uses the game's music routing, volume controls, and next-track command.** EA Trax runs silently in the background to retain native transitions. Discx uses a shared queue for menu and race contexts. The original interactive score continues to take priority where the game enables it.
- **Controller vibration was tested with virtual devices.** Physical controller coverage is still pending and depends on the device driver and exposed motor support.
- **Full HD cutscenes are optional imports.** Supply compatible VP6 files separately to install them. The installer preserves the original movies.

### Verification

The release installer passed an offline clean installation and reinstall. The test verified all 295 HD files, backups, controller libraries, Discx codecs, and preservation of the user's music folder. In-game checks covered the Discx sound switch in Options and while paused, native track advance and music volume, embedded cover art and the EA fallback, and independent tachometer movement for two, three, and four players.

### Credits and support

- HD interface work builds on [Upscaled HUD by LunicAura106](https://nfsmods.xyz/mod/1096). Local split screen builds on [Abdullah374's split-screen mod](https://github.com/Abdullah374/NFS_MW_SplitScreenMOD/tree/v2.4.1).
- [Installation and usage guide](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/blob/v1.0.0/README.md) · [Rendering notes](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/blob/v1.0.0/renderer-lab/presentation.md) · [Report an issue](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/issues/new)
