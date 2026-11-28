#pragma once

namespace {

constexpr unsigned ReflectionEdge(unsigned quality, unsigned supported) {
    const unsigned requested = quality >= 3 ? 512 : quality == 2 ? 256 : 128;
    return supported >= requested ? requested : supported >= 256 ? 256 : supported >= 128 ? 128 : 0;
}

extern "C" {
__attribute__((used)) static std::uintptr_t resumeVehicleCubeAllocation;
}

__attribute__((naked)) void NativeVehicleCubes() {
    asm(
        "pushl %ebx\n"
        "xorl %ebx, %ebx\n"
        "pushl %esi\n"
        "pushl %edi\n"
        "jmpl *_resumeVehicleCubeAllocation\n");
}

void BuildVehicleCubes(UINT edge, void (*build)()) {
    auto& quality = GameValue<unsigned>(0x5017b8);
    auto& qualityMaximum = GameValue<unsigned>(0x501840);
    auto& enabled = GameValue<std::uint8_t>(0x4f945b);
    auto& fallbackCube = GameValue<IDirect3DCubeTexture9*>(0x582a64);
    const auto requestedQuality = quality;
    const auto nativeMaximum = qualityMaximum;
    auto& faceSize = GameValue<UINT>(0x4f8ff4);
    do {
        faceSize = edge;
        build();
        if (enabled) {
            if (LogFile log; log.file) {
                std::fprintf(log.file, "Vehicle reflections: quality=%u face=%ux%u\n", requestedQuality, edge, edge);
            }
            return;
        }
        // The stock failure path releases the dynamic cube and depth surfaces.
        if (fallbackCube) fallbackCube->Release();
        fallbackCube = nullptr;
        edge /= 2;
        if (edge < 128) return;
        enabled = 1;
        quality = requestedQuality;
        qualityMaximum = nativeMaximum;
    } while (true);
}

void ConfigureVehicleCube() {
    auto* device = GameValue<IDirect3DDevice9*>(0x582bdc);
    const auto quality = GameValue<unsigned>(0x5017b8);
    D3DCAPS9 caps{};
    if (!quality || !GameValue<std::uint8_t>(0x4f945b) || !device ||
        FAILED(device->GetDeviceCaps(&caps)) || !(caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP)) {
        GameValue<UINT>(0x4f8ff4) = 256;
        return NativeVehicleCubes();
    }
    const auto edge = ReflectionEdge(quality, std::min(caps.MaxTextureWidth, caps.MaxTextureHeight));
    BuildVehicleCubes(edge ? edge : 256, &NativeVehicleCubes);
}

bool InstallVehicleReflectionQuality() {
    constexpr auto entry = 0x2bd4b0U;
    constexpr std::uint8_t original[] = {0x53,0x33,0xdb,0x56,0x57,0x89,0x1d,0x30,0x2a,0x98,0};
    constexpr std::uint8_t failure[] = {0xa1,0x30,0x2a,0x98,0,0x3b,0xc3,0x88,0x1d,0x5b,0x94,0x8f,0};
    if (!MatchGameBytes(entry, original, sizeof(original)) ||
        !MatchGameBytes(0x2bd5a3, failure, sizeof(failure))) return false;
    std::uint8_t branch[] = {0xe9,0,0,0,0};
    const auto offset = reinterpret_cast<std::uintptr_t>(&ConfigureVehicleCube) -
                        reinterpret_cast<std::uintptr_t>(engine) - entry - sizeof(branch);
    std::memcpy(branch + 1, &offset, sizeof(offset));
    resumeVehicleCubeAllocation = reinterpret_cast<std::uintptr_t>(engine) + entry + sizeof(branch);
    return WriteGameBytes(entry, branch, sizeof(branch));
}

bool InstallImprovedCarDetail() {
    // Keep the native traffic thresholds and per-car min/max checks. The
    // authored high-detail variant stays visible longer on racing cars.
    constexpr float original[] = {120.0f, 25.0f, 20.0f, 10.0f, 0.0f};
    constexpr float improved[] = {84.0f, 22.0f, 16.0f, 8.0f, 0.0f};
    constexpr std::uintptr_t thresholdTable = 0x4b2ebc;
    return MatchGameBytes(thresholdTable, original, sizeof(original)) &&
           WriteGameBytes(thresholdTable, improved, sizeof(improved));
}

enum class RearviewEffects { Stock, Lights, Particles };
constexpr wchar_t rearviewEffectsKey[] = L"RearviewEffects";
RearviewEffects rearviewEffects = RearviewEffects::Stock;
bool rearviewEffectsInstalled = false;

void __cdecl RenderRearviewLights(void* view, int flags) {
    if (rearviewEffects != RearviewEffects::Stock && GameValue<int>(gameFlowStateRva) == 6)
        GameFunction<void (__cdecl*)(void*)>(0x107770)(view);
    using DrawFlares = void (__cdecl*)(void*, int);
    reinterpret_cast<DrawFlares>(&GameValue<std::uint8_t>(0x105e80))(view, flags);
}

void __cdecl RenderRearviewParticles(void* view, int flags, int quality) {
    GameFunction<void (__cdecl*)(void*, int, int)>(0x350b00)(view, flags, quality);
    if (rearviewEffects == RearviewEffects::Particles && GameValue<int>(gameFlowStateRva) == 6) {
        auto* emitters = &GameValue<std::uint8_t>(0x5166c0);
        GameFunction<void (__thiscall*)(void*, void*)>(0x103d00)(emitters, view);
    }
}

bool InstallRearviewEffects() {
    // These calls run with the mirror's viewport, targets and camera already
    // bound. The game's mirror visibility and native effect checks still apply.
    constexpr std::uintptr_t lightsSite = 0x2de9f0, particlesSite = 0x2de9d9;
    constexpr std::uint8_t lightsCall[]{0xe8, 0x8b, 0x74, 0xe2, 0xff};
    constexpr std::uint8_t particlesCall[]{0xe8, 0x22, 0x21, 0x07, 0x00};
    if (!MatchGameBytes(lightsSite, lightsCall, sizeof(lightsCall)) ||
        !MatchGameBytes(particlesSite, particlesCall, sizeof(particlesCall))) return false;
    if (!RedirectCall(lightsSite, 0x105e80, reinterpret_cast<std::uintptr_t>(&RenderRearviewLights)))
        return false;
    if (RedirectCall(particlesSite, 0x350b00, reinterpret_cast<std::uintptr_t>(&RenderRearviewParticles)))
        return true;
    WriteGameBytes(lightsSite, lightsCall, sizeof(lightsCall));
    return false;
}

bool InstallRearviewCameraVisibility() {
    // The HUD checks the camera preset, and the single-player view selector
    // separately rejects the exterior camera mover. Let both use the native
    // visibility decision, including the gameplay setting, pause and look-back.
    constexpr std::uintptr_t hudCameraCheck = 0x195dda;
    constexpr std::uint8_t hudExpected[] = {
        0x83, 0xf8, 0x02, 0x74, 0x2d, 0x83, 0xf8, 0x03, 0x74, 0x28,
        0x83, 0xf8, 0x04, 0x74, 0x23, 0x83, 0xf8, 0x05, 0x74, 0x1e,
        0x83, 0xf8, 0x06, 0x74, 0x19,
    };
    constexpr std::uintptr_t enterMirror = 0x2cfb72, keepMirror = 0x2cfbc5;
    constexpr std::uint8_t enterExpected[] = {0xff, 0x50, 0x24, 0x84, 0xc0, 0x75, 0x66};
    constexpr std::uint8_t keepExpected[] = {0xff, 0x52, 0x24, 0x84, 0xc0, 0x75, 0x0d};
    if (!MatchGameBytes(hudCameraCheck, hudExpected, sizeof(hudExpected)) ||
        !MatchGameBytes(enterMirror - 5, enterExpected, sizeof(enterExpected)) ||
        !MatchGameBytes(keepMirror - 5, keepExpected, sizeof(keepExpected))) return false;

    constexpr std::uint8_t skipExteriorPresetChecks[] = {0xeb, sizeof(hudExpected) - 2};
    constexpr std::uint8_t useVisibilityCheck[] = {0x90, 0x90};
    if (!WriteGameBytes(hudCameraCheck, skipExteriorPresetChecks, sizeof(skipExteriorPresetChecks)))
        return false;
    if (WriteGameBytes(enterMirror, useVisibilityCheck, sizeof(useVisibilityCheck))) {
        if (WriteGameBytes(keepMirror, useVisibilityCheck, sizeof(useVisibilityCheck))) return true;
        WriteGameBytes(enterMirror, enterExpected + 5, sizeof(useVisibilityCheck));
    }
    WriteGameBytes(hudCameraCheck, hudExpected, sizeof(skipExteriorPresetChecks));
    return false;
}

constexpr UINT MirrorTargetEdge(UINT width, UINT height, UINT limit) {
    const auto canvasWidth = std::min<std::uint64_t>(width, std::uint64_t{height} * 4 / 3);
    const UINT desired = canvasWidth >= 2500 ? 1024 : canvasWidth >= 1300 ? 512 : 256;
    return std::min(desired, limit >= 1024 ? 1024u : limit >= 512 ? 512u : limit >= 256 ? 256u : 0u);
}

extern "C" {
__attribute__((used)) static std::uint8_t mirrorTargetsAvailable;
}

bool AllocateMirrorTargets(IDirect3DDevice9* device, UINT edge) {
    auto& mirror = GameValue<IDirect3DTexture9*>(0x582a7c);
    auto& color = GameValue<IDirect3DSurface9*>(0x582a80);
    auto& depth = GameValue<IDirect3DSurface9*>(0x582a84);
    const auto colorFormat = GameValue<D3DFORMAT>(0x4f9000);
    const auto depthFormat = GameValue<D3DFORMAT>(0x4f9004);
    mirrorTargetsAvailable = 0;
    for (; edge >= 256; edge /= 2) {
        IDirect3DTexture9* texture = nullptr;
        IDirect3DSurface9* surface = nullptr;
        IDirect3DSurface9* stencil = nullptr;
        if (SUCCEEDED(device->CreateTexture(edge, edge, 1, D3DUSAGE_RENDERTARGET, colorFormat,
                                            D3DPOOL_DEFAULT, &texture, nullptr)) &&
            SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) &&
            SUCCEEDED(device->CreateDepthStencilSurface(edge, edge, depthFormat, D3DMULTISAMPLE_NONE,
                                                        0, FALSE, &stencil, nullptr))) {
            mirror = texture;
            color = surface;
            depth = stencil;
            GameValue<UINT>(0x4f9008) = GameValue<UINT>(0x4f900c) = edge;
            mirrorTargetsAvailable = 1;
            return true;
        }
        if (stencil) stencil->Release();
        if (surface) surface->Release();
        if (texture) texture->Release();
    }
    return false;
}

void CreateAdaptiveMirrorTargets() {
    auto* device = GameValue<IDirect3DDevice9*>(0x582bdc);
    D3DCAPS9 caps{};
    const UINT edge = SUCCEEDED(device->GetDeviceCaps(&caps))
        ? MirrorTargetEdge(GameValue<UINT>(0x582be4), GameValue<UINT>(0x582be8),
                           std::min(caps.MaxTextureWidth, caps.MaxTextureHeight))
        : 256;
    const bool available = AllocateMirrorTargets(device, edge);
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Rearview mirror: target=%u\n", available ? GameValue<UINT>(0x4f9008) : 0);
    }
    // Keep the original allocation after the mirror, which serves another pass.
    GameFunction<void (__cdecl*)()>(0x2bd00c)();
}

__attribute__((naked)) void RequireMirrorForRendering() {
    asm(
        "cmpb $0, _mirrorTargetsAvailable\n"
        "je 1f\n"
        "cmpb %bl, 0x919738\n"
        "1: ret\n");
}

__attribute__((naked)) void RequireMirrorForComposition() {
    asm(
        "movb 0x919738, %al\n"
        "cmpb $0, _mirrorTargetsAvailable\n"
        "jne 1f\n"
        "xorb %al, %al\n"
        "1: ret\n");
}

using MirrorDraw = void (__cdecl*)(std::uint8_t*, void*, int);
MirrorDraw nativeMirrorDraw = nullptr;

void __cdecl DrawResponsiveMirror(std::uint8_t* quad, void* texture, int flags) {
    // The stock compositor uses four 16-byte vertices in canvas coords.
    // Its render target already contains the glass, mask and frame.
    if (aspectHookInstalled && !unlimited_split_screen::RaceActive()) {
        const float shift = FitTopEdge(activeLayout.hudY);
        for (unsigned vertex = 0; vertex < 4; ++vertex)
            *reinterpret_cast<float*>(quad + vertex * 16 + 4) += shift;
    }
    nativeMirrorDraw(quad, texture, flags);
}

bool InstallResponsiveMirrorComposition(MirrorDraw draw) {
    const auto target = reinterpret_cast<std::uintptr_t>(draw) - reinterpret_cast<std::uintptr_t>(engine);
    if (!RedirectCall(0x2e7199, target, reinterpret_cast<std::uintptr_t>(&DrawResponsiveMirror))) return false;
    nativeMirrorDraw = draw;
    return true;
}

bool InstallAdaptiveRearviewMirror() {
    constexpr std::uintptr_t render = 0x2de8c1, composite = 0x2e70a6;
    constexpr std::uint8_t renderBytes[] = {0x38,0x1d,0x38,0x97,0x91,0};
    constexpr std::uint8_t compositeBytes[] = {0xa0,0x38,0x97,0x91,0};
    constexpr std::uint8_t remainingAllocation[] = {0xa1,0x18,0x2b,0x98,0,0x85,0xc0};
    if (!MatchGameBytes(0x2bd00c, remainingAllocation, sizeof(remainingAllocation)) ||
        !MatchGameBytes(composite, compositeBytes, sizeof(compositeBytes)) ||
        !InstallInlineCall(render, renderBytes, &RequireMirrorForRendering)) return false;
    if (InstallInlineCall(composite, compositeBytes, &RequireMirrorForComposition) &&
        RedirectCall(0x2bd810, 0x2bcfa0, reinterpret_cast<std::uintptr_t>(&CreateAdaptiveMirrorTargets))) return true;
    WriteGameBytes(render, renderBytes, sizeof(renderBytes));
    WriteGameBytes(composite, compositeBytes, sizeof(compositeBytes));
    return false;
}

} // namespace
