# Xbox 360 integration in Unlimited

## Included features

- **Vehicle lights in reflections.** Unlimited restores vehicle light flares in the reflection cubemap through the original PC renderer. The game's graphics settings determine when those lights are drawn. [Aero_'s NFS HD Reflections](https://nfsmods.xyz/mod/3363)
- **High-resolution rearview mirror.** The mirror's color and depth targets preserve the original proportions and adapt to the interface size. Unlimited uses 256, 512, or 1024 pixels per side depending on the resolution and can fall back to a smaller size if allocation fails. [NFS HD Reflections reference](https://github.com/AeroWidescreen/NFSHDReflections)
- **Larger draw lists.** Unlimited increases each of the two lists the engine allocates at startup from 400 to 1,000 KiB. The extra space accommodates reflection geometry. The car, customization, and save-game tables require separate integration. [NFS HD Reflections source](https://github.com/AeroWidescreen/NFSHDReflections)
- **XenonEffects 1.02.** This add-on restores Xbox 360 sparks and air trails. The **Xbox 360 effects** option in the video settings takes effect on restart and defaults to off. The loader checks patch sites before enabling the module. Tests covered the executable, instructions, and texture pack. In-race visual verification is pending. [Project and MIT license](https://github.com/xan1242/NFSMW_XenonEffects) · [Version 1.02](https://github.com/xan1242/NFSMW_XenonEffects/releases/tag/1.02)

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
