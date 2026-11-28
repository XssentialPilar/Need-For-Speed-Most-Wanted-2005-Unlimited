# Xbox 360 integration in Unlimited

## Included features

- **Vehicle lights in reflections.** Unlimited restores vehicle light flares in the reflection cubemap through the original PC renderer. The game's graphics settings determine when those lights are drawn. [Aero_'s NFS HD Reflections](https://nfsmods.xyz/mod/3363)
- **High-resolution rearview mirror.** The mirror's color and depth targets preserve the original proportions and adapt to the interface size. Unlimited uses 256, 512, or 1024 pixels per side depending on the resolution and can fall back to a smaller size if allocation fails. [NFS HD Reflections reference](https://github.com/AeroWidescreen/NFSHDReflections)
- **Rearview visibility.** The **Gameplay → Rearview Mirror** setting also applies to the exterior driving cameras in single-player. The game hides the mirror while paused or looking back.
- **Rearview effects.** Under **Video → Advanced → Advanced Effects**, choose **Off**, **Lights**, or **Lights and particles**. The effects apply immediately to an active mirror using the game's lighting and particle passes. The setting is off by default. [NFS HD Reflections reference](https://github.com/AeroWidescreen/NFSHDReflections)
- **Larger draw lists.** Unlimited increases each of the two lists the engine allocates at startup from 400 to 1,000 KiB. The extra space accommodates reflection geometry. The car, customization, and save-game tables require separate integration. [NFS HD Reflections source](https://github.com/AeroWidescreen/NFSHDReflections)
- **XenonEffects 1.02.** This add-on restores Xbox 360 sparks and air trails. The **Xbox 360 effects** option in the video settings takes effect on restart and defaults to off. The loader checks patch sites before enabling the module. Tests covered the executable, instructions, and texture pack. In-race visual verification is pending. [Project and MIT license](https://github.com/xan1242/NFSMW_XenonEffects) · [Version 1.02](https://github.com/xan1242/NFSMW_XenonEffects/releases/tag/1.02)

## Xbox 360 Stuff Pack 4.2

The **Complex Installation** package from the Xbox 360 Stuff Pack team supplies the authored rendering component, shaders, scripts and assets used by this integration. Unlimited implements the verified PC import pipeline, in-game controls and per-player rendering integration. The package contains 330 files and its readme identifies release 4.2. The first installer script retains a 4.1 label. [Upstream package and downloads](https://nfsmods.xyz/mod/1200) · [Attribution](CREDITS.md#upstream-projects-and-assets)

### Native assets in Unlimited

- **World and garage textures.** The build converts the standard world mapping to 378 PC texture IDs and reads 293 compatible replacements from the five garage, dealership and shop archives. Garage textures must keep their original proportions and match existing PC texture IDs. The 8K remaster takes priority for shared IDs at the selected texture quality. [TexWizard source mapping](https://github.com/R-033/texwizard)
- **Sky, sun, lights and particles.** The build adds 51 DDS replacements for known PC slots. Together the world, garage and effect assets produce 722 texture IDs in `GLOBAL/UnlimitedXboxTextures.bin`. Enable **Xbox 360 effects** on the main Video page and restart. Larger textures use the patch's demand-based D3D9 loader. With the original texture quality selected, the console pack uses mips up to 1024 pixels. The three previously supported flares and rain assets remain available when the build also includes the v4.2 `GlobalB.lzc`.
- **Race and region barriers.** The build reads the four barrier entries from the optional TexWizard manifest and embeds them in the patch's texture resource. The game uses its existing barrier IDs and progression rules.
- **Vehicle light materials.** The build converts all 48 material exports into the PC layout. The game's material loader applies the authored values to matching stock slots after its tint setup. This covers paint, wheels, glass and windshield tints, including the two HUFF-compressed exports.
- **Visual attributes.** The optional PC vault compiles all 1,075 operations from the supplied installer and lighting reset scripts, including four emitter-group expansions that restore 18 authored entries for smoke, skid particles and leaves. When **Xbox 360 effects** is enabled, the game selects `GLOBAL/ATTRIBUTE360.BIN` at startup after validating the original and compiled files. Set `X360Vlt=0` under `[Graphics]` for the stock vault, `1` for the standard console vault, or `2` to test the alternate vault. The alternate adds 20 light materials and 10 light shaders. The alternate is intended for testing. Its additional records have been verified in the compiled database. Their visual contribution depends on the scene and material.
- **Sky, exposure, and reflections.** The renderer initializes the frontend sky after the global resources load. Single-player uses the console reflection and exposure passes. During gameplay each split-screen pane receives the console world passes. The native PC multiview controller still initializes the race and its targets. The display menu uses the active monitor’s native resolution with either renderer.
- **Garage and showroom geometry.** The game's loader can select all five console platform archives for the quick-race garage, career safehouse, customization shop, back room, and car lot. Four console reflection maps supply all 24 faces through the native streaming pack `GLOBAL/DYNTEX36.BIN`. Its other 127 texture records retain the original compressed payloads. The stock archives provide a fallback if a platform has been customized or the matching console archive is missing. The build also applies five frontend vehicle lighting presets and one gameplay preset. On Direct3D 9, the garage geometry and cars enter the game's existing shadow pass when shadows are enabled. Use `X360Platforms`, `X360PlatformLighting`, and `X360PlatformShadows` under `[Graphics]` to control these changes independently.
- **Outdoor shadow direction.** The console setting limits the vertical component of the normalized sun direction to a minimum of 0.7 before the engine projects the shadow. Set `X360ShadowAngle=0` under `[Graphics]` to restore the PC angle.
- **Track archive.** The build applies the UPS delta to a verified copy and stages `TRACKS/STREAML2X6.BUN`. Set `X360Track=1` under `[Graphics]` with **Xbox 360 effects** enabled to select it at the next start. The option defaults to off because the author reports loading issues in some races and free-roam sessions. [Author's compatibility notes](https://nfsmods.xyz/mod/1200)
- **EA TRAX soundtrack.** Pathfinder loads the console bank when the matched `X3_Music.mpf` and `X3_Music.mus` files are installed. The build checks every stream offset and verifies that all 3,257 event entries preserve the PC sound IDs. This setting selects the EA TRAX bank. Set `X360Music=0` under `[Audio]` in `NFSMW-Unlimited.ini` to select the PC bank on the next launch.
- **Engine sounds.** Setup validates 87 EA GIN files and installs them in the corresponding PC sound slots. It backs up stock files and preserves existing sounds with other checksums. [EA GIN format](https://github.com/vgmstream/vgmstream/blob/master/src/meta/gin.c)

### World shaders on DirectX 9

Select **DirectX 9** and enable **Xbox 360 effects** in the video settings, then restart the game. The build stages the upstream 4.2 rendering component as `World360.dll`, with its shader lookup and startup entry adapted for Unlimited. The upstream renderer supplies the scene techniques and material inputs for car paint, reflective roads and windows, foliage, water, sky and exposure. Unlimited initializes the component, selects the appropriate shader set and coordinates the per-player world and HUD passes.

To choose an effect set, set `X360WorldShaders` under `[Graphics]` in `NFSMW-Unlimited.ini` and restart.

| Value | Effect set |
| --- | --- |
| 0 | PC shaders |
| 1 | Standard console shaders (default when Xbox 360 effects are enabled) |
| 2 | The author's lower-cost shader set |
| 3 | Standard set with the author's sharper tree shader |
| 4 | Standard set with the author's soft-shadow shaders |

The four sets use separate directories so the renderer can switch back to PC shaders. The experimental DirectX 11, DirectX 12 and Vulkan presentation modes use the PC shader path. Unlimited also checks the native texture collections and renderer component before enabling a console shader set.

### Compatibility findings

| Package resources | Finding |
| --- | --- |
| 18 default effect objects and 18 alternatives | Direct3D 9 reflection enumerated 276 techniques and 324 passes. The effects depend on the upstream renderer's technique selection, material inputs and scene passes. Unlimited loads the matching rendering component and offers all four complete effect sets on DirectX 9. Technique enumeration establishes shader coverage. In-game checks cover the specific scenes listed below. |
| VLT script differences | Five retail records use numeric identifiers in the PC database. The compiler resolves all 77 operations targeting those records, including the visual-treatment color matrix and the overcast lighting presets. The alternate vault loads through the same engine path as the standard vault. |
| Part 2 Binary script and five platform archives | The platform archives, material imports, vehicle lights and compatible texture IDs are integrated. Unlimited preserves the PC texture collections and adds the materials that the console programs look up by name. |
| Remaining DDS files | All standard world texture IDs are mapped. The glossy window uses the authored white texture and the grass opacity mask uses its existing PC slot. Both are embedded in the compact update as well. The native global and in-game texture collections supply 30 more shader inputs, including cloud layers, noise and the vehicle damage normal map. All 29 new global records match the author's completed package in pixel format and contents. The existing HD menu and boot artwork takes precedence where both packages supply replacements. |
| Track UPS delta | The build verifies input CRC `BD947661` and output CRC `AE90F386`. The delta changes 1,873 bytes across 899 ranges. Runtime checks covered a single-player race and a two-player split-screen race. |
| Scripts, configurations, symbols and previews | Unlimited loads the integrated assets directly. The preview images and PDB files are reference and debugging material. |

The shader comparison found concrete differences in the engine interfaces.

| Effect | PC entry points | Console entry points |
| --- | --- | --- |
| Particles | `world`, `world_fixed` | `particles`, `noshadow`, `onscreen_distort` |
| Road reflections | `world_reflect_shadow_2_0`, `world_reflect_noshadow_2_0` | `dryroad`, `wetroad`, both with two passes |
| Sky | `world` with three passes | Independent `sky` and `tag_hdr` passes |
| Visual treatment | `vt` | `visualtreatment`, `visualtreatment_branching`, `downscale4x4alphaluminance` |
| Exposure | `filter` | `calculate_luminance`, bloom and downscale passes, and `finalhdrpass` |

To build the integrated assets, pass `-XboxComplexDirectory` to `installer/build.ps1` with the extracted folder. Use a verified PC 1.3 game data source with the original audio, vault and track files. For the optional visual vault, pass `-XboxAttribulator` with the path to the Windows x86 Attribulator CLI. If using the managed CLI DLL, pass the x86 runtime host with `-XboxDotnet`. The compiler checks the generated vault against the verified build. `-ConsoleUpdateOnly` builds a smaller update for an existing Unlimited installation with the console texture pack. [Attribulator source and releases](https://github.com/NFSTools/Attribulator)

Runtime checks covered console textures with the original quality setting, a race with both console assets and Ultra active, the PC soundtrack fallback, and a race with the visual vault, materials and alternate track active together. During the combined race, the track streaming pool reported 100.9 MiB free out of 175.8 MiB. Direct3D 9 tests rendered and captured all five console garage scenes at 1920 × 1200 with the alternate vault loaded and the garage shadow pass enabled and disabled. The final console renderer also drew all five garage scenes at 1600 × 1200. Separate 1500 × 800 race captures covered the soft-shadow profile in single-player, the lower-cost profile with two players, the sharp-tree profile with three, and the standard profile with four. The split-screen views retained their own minimaps, tachometers and standings. [Garage captures](renderer-lab/xbox360-garages.jpg) · [Race captures](renderer-lab/xbox360-split-screen.jpg) Instrumentation recorded successful Direct3D 9 passes for dry roads, normal-mapped vehicles, grass, sky, particles and world lighting during a four-player race. The main installation also loaded the standard renderer, visual vault and first garage on an ordinary startup. Water distortion, transitions between light and dark, and every combination of weather, hardware and course have not been visually verified. The game already uses the Large Address Aware flag. The earlier complete install test verified both soundtrack banks, the 87 engine replacements and their originals, and all 295 HD interface resources. The final full installer was installed on a separate clean PC 1.3 fixture and its 295 interface files and backups, 87 engine sounds, both console music files, 722 console texture IDs, four shader profiles and both visual vaults were checked.

## Reference projects

| Project | Features | Integration considerations |
| --- | --- | --- |
| [Xbox 360 Stuff Pack 4.2](https://nfsmods.xyz/mod/1200) | Vehicle materials, exposure, time-of-day lighting, sky, water, vegetation, and textures | Verify each asset and its origin. The pack includes view scaling and XenonEffects, which overlap with Unlimited features. |
| [Kevin4e's Xbox 360 Visual Treatment 1.5](https://www.nfsaddons.com/downloads/nfsmw/tools/7927/updated-xbox-360-visual-treatment-v14.html) | Console color and lighting adjustments | Requires Extra Options, VltEd, and Stuff Pack 2.4 Beta. The author requires permission to include it in a modpack. |
| [Aero_'s NFS HD Reflections](https://github.com/AeroWidescreen/NFSHDReflections) | Reflection detail, rearview lighting, composition, and render resolution | Review each effect individually. The reflection code overlaps with Unlimited's cubemap updates and Direct3D resets. |
| [nlgxzef's NFSMW Unlimiter](https://nfsmods.xyz/mod/688) | Expanded car tables, memory pools, and customization options | Audit database changes and FNG menus with test profiles before combining the patches. |

XenonEffects comes from the author's `NFSMW_XenonEffects.zip` release, with SHA-256 `1a6d85cfdc274259b0ee1df0e2e51f7e273382e04bfdbe472b4688de53226082`. The installer copies the license to `UnlimitedCommunity/NFSMW_XenonEffects-LICENSE.txt` inside the game folder. [Bundled license](third-party/xenon/LICENSE)

## Full HD cutscenes

Unlimited looks for 1920 × 1080 EA VP6 replacements in the game's `MOVIES_HD` folder. Each video must use the filename the game requests for that language and a compatible frame rate. The loader validates the stream before opening it and falls back to the installed cutscene if the replacement is missing or fails validation. Full HD cutscenes are supplied separately.
