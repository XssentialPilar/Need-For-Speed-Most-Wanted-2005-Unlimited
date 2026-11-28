# v1.1.0-beta.1 verification

The performance results below are from the DirectX 9 build with the optional Xbox 360 data installed. Tests ran at 1920 × 1200 on a 180 Hz display. They describe the tested machine, settings and scenes.

| Measurement | v1.0.1-era local build | Beta runtime |
| --- | ---: | ---: |
| Main-menu FPS with vertical sync | 60.0 | 180.0 |
| Main-menu median frame time | 16.67 ms | 5.55 ms |
| Main-menu P99 frame time | 18.95 ms | 6.04 ms |
| Single-player race FPS with vertical sync off | 143.9 | 145.8 |
| Single-player race FPS with vertical sync on | 156.5 | 147.3 |
| Time to gameplay with vertical sync off | 4.321 s | 4.211 s |
| Time to gameplay with vertical sync on | 4.875 s | 4.672 s |

The baseline was the installed local build immediately before the performance changes. It included work that followed the public v1.0.1 release. The race numbers combine two launches and eight roughly four-second samples per configuration. Load measurements use a warm file cache. The race slowdown in the vertical-sync test remains open. Menu presentation also exceeded 1,000 FPS in a separate run with vertical sync off.

## Sound and UI

- Tested the Spanish video menu at full resolution. **Límite de FPS** and **Sin límite** displayed correctly.
- The DISCX and EA TRAX song cards both started their native animation in the same measurement tick as their requests in the menu. Each began five seconds after its request in the tested race.
- Checked title, artist, album, embedded artwork, the restoration of the EA badge and TRAX artwork, and the DISCX transport controls. FLAC, MP3 and Opus fixtures decoded and sought successfully at 48 kHz.

## Public installer and profile storage

- The initial runtime-only installer passed a PC 1.3 fixture installation and reinstall. The test verified the executable backup, the installed controller modules, the single DISCX decoder and its source bundle, and retention of existing settings and custom music.
- The compact installer passed a clean installation and reinstall. It retained the 295 HD resources, 87 engine sounds, and all 722 console texture IDs. In game, all seven quality choices remained visible and the same sampled texture loaded at 256 px on Very Low and 1,024 px on Ultra.
- The profile store test passed create, replace, integrity validation, contention, interrupted staging, recovery, damaged backups, rotation, and detection of conflicting edits.

## Optional assets and game modes

- Captured two-, three- and four-player races with console rendering enabled in each pane and a two-player race on the PC path for comparison.
- Checked the full local build on a clean PC 1.3 fixture. It verified 295 HD resources and backups, 87 engine sounds, two soundtrack files, 722 console texture records, four shader profiles and two visual vaults.
- Checked a local console update on a separate fixture. The installer kept the configured settings, verified executable and installed texture bank while updating the runtime and decoder.

Coverage for physical controllers, all tracks, changing weather and the experimental presentation modes remains limited. See the [Xbox 360 integration notes](../xbox-360-integration.md) for the specific scenes and profiles tested.