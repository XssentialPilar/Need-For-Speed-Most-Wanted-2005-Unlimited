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
