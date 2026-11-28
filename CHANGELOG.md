# Changelog

## v1.1.0-beta.1

This beta introduces native save recovery, Xbox 360 world effects in local split screen, and updates to the display and soundtrack controls. The release offers the compact all-in-one installer and a 12-language guide to the full installer and console update. [Downloads](https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited/releases/tag/v1.1.0-beta.1)

### Compact distribution

- Combines the HD interface, runtime, Xbox 360 graphics and engine sounds, and the console update in a single 501.7 MB installer.
- The complete edition supplies the optional 8K remaster and Xbox 360 soundtrack. The compact edition retains all seven texture quality choices and applies the saved profile to installed textures.
- The compact installer generates the verified alternate track from a small delta against the stock PC track archive and shares storage for byte-identical console mip chains. [Compact build instructions](docs/build.md#compact-all-in-one-installer)

### Save integrity and recovery

- Manual saves and autosaves now stage and validate the native PC profile before replacing the previous save. A failed or incomplete write leaves the previous profile available.
- The save layer checks the native file size and integrity fields. An intact backup can take over automatically when the current file fails those checks.
- Keeps up to three recent valid revisions and the first and latest saves the game successfully decoded for each supported layout and database.
- A second load attempt can offer a matching confirmed copy when the game rejects an intact profile. The previous file is preserved when the recovered session is saved.
- Detects edits made outside the game after a profile was loaded or saved. Conflicting saves retain the on-disk version and attempt to preserve the pending progress separately.
- Fixed a crash when a save error occurred without an active save screen and a false corruption warning when switching between the supported PC and Xbox 360 visual databases.
- Profile deletion remains intentional. Recreating the name starts a fresh recovery history. [Save and recovery guide](docs/save-recovery.md)

### Graphics, display and performance

- Removed the Xbox 360 renderer's separate 60 FPS menu cap. The in-game frame limit controls menu and gameplay presentation, subject to the display and driver settings.
- In the tested menu at 1920 × 1200 and 180 Hz, performance rose from 60 to 180 FPS and median frame time dropped from 16.67 ms to 5.55 ms. With sync off, the startup screen exceeded 1,000 FPS in a separate run.
- Profiled startup, gameplay and frame times separately. Larger sequential reads reduce the cost of verifying packaged resources while preserving the expected CRC results.
- Texture uploads cache budget queries briefly, account for pending reservations, and group small mip reads. Texture streaming releases unused high-resolution resources and handles a graphics reset without leaving stale demand queued.
- Improved native display mode selection so the display menu can include the active monitor's native resolution. Updated the advanced video list navigation and kept the labels, values and arrows aligned.
- Fixed broken accent encoding in the FPS menu and restored original EA TRAX naming when that soundtrack is selected.
- Made frame timing diagnostics opt-in via `[Diagnostics] FrameStatistics=1`. [Measured results](docs/verification-1.1.0-beta.1.md)

### Split screen and HUD

- Applied console world passes separately to each complete player view while retaining the engine's multiview setup during loading and composition.
- Updated viewport placement for rearview mirrors, race standings and pursuit panels across aspect ratios. Rearview visibility also respects the gameplay setting in exterior single-player cameras.
- Preserved distinct cameras, HUDs, standings, minimaps and engine RPM for the selected cars. Tested races with two, three and four local players.
- Kept music notices on the game's native UI path in both the frontend and gameplay. The runtime uses the correct TRAX or DISCX tab as the source changes.

### DISCX and audio

- Standardized the feature's visible name to **DISCX**. Updated the icon and fitted the card to the game's artwork and typography.
- Uses the current track's embedded front cover and metadata. The card falls back to the original EA badge when artwork cannot be read and restores the stock badge and tab when EA TRAX resumes.
- Uses the native song notice scheduler and animation for menu and race transitions. The tested race showed the same five-second delay for both music sources.
- The Sound menu presents the toggle for the active music source. Verified pause, resume, seeking and track changes with the revised card path.
- Automatic output uses the selected Windows device's speaker configuration. Stereo offers **Unlimited Matrix**, which encodes the surround mix as stereo Lt/Rt.
- Combined the four FFmpeg components inside the replaceable x86 DISCX decoder. The distribution includes corresponding source and build recipes. The controller helpers remain part of the distribution.

### Optional Xbox 360 and remastered assets

- Added build recipes for the compatible Xbox 360 Stuff Pack 4.2 DirectX 9 renderer, with Standard, Lower-cost, Sharp Trees and Soft Shadows profiles.
- Added validated native inputs for cloud layers, sun, noise, water and vehicle normals. Restored matching lighting, reflections and materials in the five supported garage layouts.
- Added compilation and validation for the standard and alternate visual databases, optional track delta, console soundtrack bank and compatible engine files.
- Added Very Low, Low, Medium, High, Very High and Ultra settings for the optional 2.01 remaster. Original remains available. The runtime can select lower mips or the installed PC resource under memory pressure.
- Documented the optional assets, expected input paths, validation and restart requirements. [Xbox 360 integration](xbox-360-integration.md) · [Build guide](docs/build.md)

### Installation, testing and known issues

- The public installer targets a supported PC 1.3 installation. It keeps existing Unlimited preferences and user music. Setup checks the game folder and supports optional local import of matching HD resources and compatible VP6 movies.
- The complete build verified 295 HD assets and their originals, 87 engine sounds, two console music files, 722 console texture records, four shader profiles and two visual databases. The console update preserved the tested executable, texture bank and settings.
- The same benchmark showed lower race FPS with vertical sync active. In the tested scene, it fell from 156.5 to 147.3 FPS. The sync-off run rose from 143.9 to 145.8 FPS. These are measurements from the tested machine and scene.
- DirectX 11, DirectX 12 and Vulkan presentation remain experimental. Coverage for physical controllers, all courses and every weather and graphics combination remains limited.
- The optional alternate track archive defaults to off due to known compatibility issues in some modes. The beta is intended for testing with a backup of your profile. [Player guide](docs/player-guide.md) · [Verification](docs/verification-1.1.0-beta.1.md)

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
