# Credits and licensing

## Unlimited development

**XssentialPilar** develops and maintains Need for Speed Most Wanted 2005 Unlimited. The work in this repository includes:

- **DISCX and Unlimited Matrix.** The local music library, playback controls, integration with the game's song-card scheduler and music mixer, Windows output selection and stereo Lt/Rt encoding. [DISCX player](nfs_discx.cpp) · [Audio integration](nfs_audio.hpp)
- **Save protection.** Staged replacement of native PC saves, integrity checks, revision history, recovery of compatible saves and detection of conflicting edits. [Save storage](nfs_save_store.hpp) · [Game integration](nfs_save_hooks.hpp)
- **Local split-screen adaptation.** Unlimited builds on Abdullah374's restored split-screen code and four-player engine hooks. Unlimited extends the setup and per-player controls, adapts the race HUD and driver data to each viewport, and integrates the Xbox 360 scene passes, separate reflection and post-processing state, and adaptive scene resolution. [Split-screen integration](third-party/split-screen/integration.hpp) · [HUD code](nfs_hud.hpp)
- **PC graphics and interface.** Display mode, frame timing and scene resolution controls, aspect-aware HUD placement and new native menu entries. Unlimited also prepared supplemental resources and the validated HD interface installer. [Video options](nfs_video_options.hpp) · [Interface layout](nfs_frontend_layout.hpp) · [Asset installer](installer/AssetImport.iss)
- **Native integration of external assets.** Unlimited converts compatible community files into PC texture banks, garage archives, lighting and visual databases, and audio resources. It adapts the upstream renderer's startup and shader lookup, hooks asset loading into the PC engine, and manages mip selection, memory budgets and fallback to the installed game assets. [Integration details](xbox-360-integration.md) · [Build tooling](installer) · [Texture loader](nfs_textures.hpp)
- **Packaging and verification.** Compact and full builds, checksummed installer inputs, backups of replaced files, and release testing across clean installs, upgrades, rendering modes and save recovery. [Build guide](docs/build.md) · [Beta verification](docs/verification-1.1.0-beta.1.md)

## Upstream projects and assets

| Project or author | Contribution used by Unlimited |
| --- | --- |
| Abdullah374 | Split-screen restoration and four-player engine hooks. See the [upstream v2.4.1 source](https://github.com/Abdullah374/NFS_MW_SplitScreenMOD/tree/v2.4.1) and [bundled MIT notice](third-party/split-screen/LICENSE). |
| Xbox 360 Stuff Pack 4.2 team | Authored rendering component, shaders, resources and scripts used by the console integration. The package credits Toru the Red Fox, xanvier / xan1242, elaymm4, Aero_, TMCharly and osdever. See the package's full readme and [upstream project page](https://nfsmods.xyz/mod/1200). |
| Lovro Pleše (xan1242) | XenonEffects 1.02 supplies sparks and air trails. See the [upstream project](https://github.com/xan1242/NFSMW_XenonEffects) and [bundled MIT notice](third-party/xenon/LICENSE). |
| LunicAura106 | Upscaled HUD provides the basis for the HD interface assets. See [NFSMW: Upscaled HUD](https://nfsmods.xyz/mod/1096). |
| CaptainTany and the 8K Remade Textures team | 8K Remade Textures V2 supplies the optional remaster used by the full edition. See the [team's project update](https://www.reddit.com/r/needforspeed/comments/1xsyjy4/update_on_8k_textures_for_nfs_mw_2005/). |
| Aero_ | NFS HD Reflections is a technical reference for rearview and cubemap effects. See [NFS HD Reflections](https://github.com/AeroWidescreen/NFSHDReflections). |
| R-033 and NFSTools | TexWizard supplies the upstream texture mapping used by the conversion pipeline. Attribulator is a build tool for the optional visual vaults. See [TexWizard](https://github.com/R-033/texwizard) and [Attribulator](https://github.com/NFSTools/Attribulator). |
| FFmpeg contributors | FFmpeg 6.1.6 provides the decoding libraries used by DISCX under LGPL 2.1 or later. See the [decoder build and source details](third-party/discx-codec/README.md). |
| Xidi-SDL, SDL and their dependencies | DirectInput controller support through SDL3. See the [controller credits and versions](third-party/controllers/README.md) and [bundled notices](third-party/controllers/Licenses). |

## Licensing scope

- The [Unlimited MIT License](LICENSE) applies to the original Unlimited code and documentation that the project has the right to license, including its own contributions to adapted modules. Retain the upstream notices when redistributing adapted or bundled code.
- Upstream code, binaries, artwork, shaders, textures, music and other resources remain subject to their respective terms. The Xbox 360 Stuff Pack supplies its own notice in the source package, which Setup carries with the console renderer. Refer to the upstream projects for the terms that apply to their individual assets.
- Need for Speed: Most Wanted and its original game assets belong to their respective rights holders. Unlimited is an independent community project and requires a supported copy of the PC game.
