#pragma once

namespace {
namespace unlimited_console {

std::uintptr_t rendererBase = 0;

using ScenePass = void (__cdecl*)();
ScenePass previousConfigureViews = nullptr;
ScenePass previousDrawViews = nullptr;

bool UsesMultipleWorldViews() {
    // The loader enters the two-view mode before the race and its second
    // render target exist. The native PC controller owns that transition.
    const int renderMode = GameValue<int>(0x582b40);
    const int overrideMode = GameValue<int>(0x582b44);
    const int currentMode = GameValue<int>(0x582b3c);
    return renderMode >= 3 || currentMode >= 3 || overrideMode >= 3 || unlimited_split_screen::RaceActive();
}

void __cdecl ConfigureWorldViews() {
    if (UsesMultipleWorldViews()) previousConfigureViews();
    else reinterpret_cast<ScenePass>(rendererBase + 0x2fd80)();
}

void __cdecl DrawWorldViews() {
    const auto draw = reinterpret_cast<ScenePass>(rendererBase + 0x2ff20);
    if (unlimited_split_screen::DrawingPlayerPane()) {
        // Each Unlimited pane owns view 1 for the duration of its render. Use
        // the console single-view passes before the split compositor captures it.
        auto& mode = GameValue<int>(0x582b3c);
        const int saved = mode;
        mode = 2;
        draw();
        mode = saved;
    } else if (UsesMultipleWorldViews()) {
        previousDrawViews();
    } else {
        draw();
    }
}

void __cdecl SetWorldFrontendMatrix(D3DMATRIX* matrix, int view) {
    using SetMatrix = void (__cdecl*)(D3DMATRIX*, int);
    reinterpret_cast<SetMatrix>(rendererBase + 0x2e9a0)(matrix, view);
    // The frontend submits 640 x 480 coordinates. The HUD, mouse and FE
    // share Unlimited's fitted canvas, including when it is split into panes.
    const auto width = requestedDisplay.BackBufferWidth, height = requestedDisplay.BackBufferHeight;
    if (!width || !height) return;
    const auto layout = MakeAspectLayout(width, height);
    D3DMATRIX fitted{};
    fitted._11 = layout.hudX;
    fitted._22 = layout.hudY;
    fitted._33 = fitted._44 = 1;
    fitted._41 = -1.0f / width;
    fitted._42 = 1.0f / height;
    *reinterpret_cast<D3DMATRIX*>(rendererBase + 0x61568) = fitted;
}

void __cdecl InitializeWorldScene() {
    GameFunction<void (__cdecl*)()>(0x1abad0)();
    // These materials reference the live attribute vault. The renderer loads
    // the FE sky model here, after the engine registers global resources.
    reinterpret_cast<void (__cdecl*)()>(rendererBase + 0x384d0)();
    if (LogFile log; log.file) std::fprintf(log.file, "World scene: frontend sky and lighting initialized\n");
}

const char* SelectWorldRenderer() {
    if (!openGameFile || nativePassesAtStartup != 9) return "PC";
    const unsigned profile = GetPrivateProfileIntW(L"Graphics", L"X360WorldShaders", 1, settingsPath);
    constexpr const char* profiles[] = {"", "U360", "U36L", "U36T", "U36S"};
    if (profile == 0 || profile >= std::size(profiles)) return "PC";
    if (GetModuleHandleW(L"X360Stuff.asi")) return "standalone renderer present";
    if (!WorldTextureArchivesReady()) return "incomplete world textures";
    WIN32_FILE_ATTRIBUTE_DATA sky{};
    if (!GetFileAttributesExA("FRONTEND\\PLATFORMS\\NextGenSky.BIN", GetFileExInfoStandard, &sky) ||
        (sky.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !sky.nFileSizeLow) return "missing frontend sky";
    constexpr const char* programs[] = {"car", "carnormalmap", "debugPoly", "glassreflectshader", "grass", "hdr",
        "particles", "shadowscreenfilter", "shadowvolume", "sky", "standard", "trees", "visualtreatment",
        "waterseashader", "world", "worldbone", "worldnormalmap", "worldreflect"};
    for (auto* program : programs) {
        char path[80]{};
        std::snprintf(path, sizeof(path), "FX\\%s\\%s.fx.o", profiles[profile], program);
        WIN32_FILE_ATTRIBUTE_DATA file{};
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &file) ||
            (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !file.nFileSizeLow) return "incomplete shader set";
    }
    constexpr char component[] = "UnlimitedCommunity\\World360.dll";
    if (!MatchConsoleAsset(component, 402944, 0xde34f543)) return "missing or incompatible component";
    wchar_t path[MAX_PATH]{};
    const auto length = GetFullPathNameW(L"UnlimitedCommunity\\World360.dll", MAX_PATH, path, nullptr);
    if (!length || length >= MAX_PATH) return "component path unavailable";
    auto* module = LoadLibraryW(path);
    if (!module) return "component loading failed";
    const auto image = reinterpret_cast<std::uintptr_t>(module);
    auto* cfg = reinterpret_cast<char*>(image + 0x60fb8);
    const auto configLength = GetFullPathNameA("UnlimitedCommunity\\World360.ini", MAX_PATH, cfg, nullptr);
    if (!configLength || configLength >= MAX_PATH) { FreeLibrary(module); return "configuration path unavailable"; }
    if (profile != 1) {
        for (const auto rva : {0x4b618u, 0x4bee8u}) {
            auto* name = reinterpret_cast<char*>(image + rva + 3);
            DWORD oldProtect = 0;
            if (!VirtualProtect(name, 4, PAGE_READWRITE, &oldProtect)) {
                FreeLibrary(module);
                return "shader profile unavailable";
            }
            std::memcpy(name, profiles[profile], 4);
            VirtualProtect(name, 4, oldProtect, &oldProtect);
        }
    }
    // Only the cubemap glass uses the PC bezel coordinates. Other frontend
    // elements retain the scaling profile chosen in the component configuration.
    auto* mirrorLayout = reinterpret_cast<std::uint8_t*>(image + 0x34397);
    const auto scalingOption = image + 0x5bff4;
    constexpr std::uint8_t pcMirrorLayout[] = {0xb8, 2, 0, 0, 0};
    if (mirrorLayout[0] != 0xa1 || std::memcmp(mirrorLayout + 1, &scalingOption, 4) != 0 ||
        !WriteCodeBytes(mirrorLayout, pcMirrorLayout, sizeof(pcMirrorLayout))) {
        FreeLibrary(module);
        return "mirror layout unavailable";
    }
    auto** effectImport = reinterpret_cast<void**>(image + 0x401b0);
    const auto d3dx = GetModuleHandleW(L"d3dx9_26.dll");
    const auto create = d3dx ? GetProcAddress(d3dx, "D3DXCreateEffect") : nullptr;
    if (!create || *effectImport != reinterpret_cast<void*>(create)) {
        FreeLibrary(module);
        return "incompatible D3DX effect interface";
    }
    unlimited_textures::nativeMemoryEffect = reinterpret_cast<unlimited_textures::CreateMemoryEffect>(
        reinterpret_cast<std::uintptr_t>(create));
    if (!ReplaceMethod(effectImport, 0, reinterpret_cast<void*>(&unlimited_textures::CreateWorldEffect))) {
        FreeLibrary(module);
        return "effect binding unavailable";
    }
    if (!RedirectCall(0x2660c5, 0x1abad0, reinterpret_cast<std::uintptr_t>(&InitializeWorldScene))) {
        FreeLibrary(module);
        return "world scene initialization unavailable";
    }
    shaderTexturesSelected = true;
    rendererBase = image;
    using Init = void (__cdecl*)();
    reinterpret_cast<Init>(image + 0x2be30)();
    // The authored renderer has a second 60 Hz frontend limiter. Unlimited
    // advances frontend animations on their own clock and paces all game views.
    *reinterpret_cast<std::uint8_t*>(image + 0x5bffc) = 0;
    reinterpret_cast<Init>(image + 0x38930)();
    // The console passes populate the reflection faces and run the exposure chain.
    // Its saved callees include the previously installed Unlimited view controller.
    previousConfigureViews = *reinterpret_cast<ScenePass*>(image + 0x61180);
    previousDrawViews = *reinterpret_cast<ScenePass*>(image + 0x611cc);
    const auto game = reinterpret_cast<std::uintptr_t>(engine);
    RedirectCall(0x2e7300, image+0x2fd80-game, reinterpret_cast<std::uintptr_t>(&ConfigureWorldViews));
    RedirectCall(0x2e759c, image+0x2ff20-game, reinterpret_cast<std::uintptr_t>(&DrawWorldViews));
    RedirectCall(0x2e6fb7, image+0x2e9a0-game, reinterpret_cast<std::uintptr_t>(&SetWorldFrontendMatrix));
    return profiles[profile];
}

} // namespace unlimited_console
} // namespace
