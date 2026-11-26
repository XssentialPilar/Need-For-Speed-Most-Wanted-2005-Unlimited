#pragma once

namespace {

bool InstallReflectionRenderPools() {
    // The two camera draw lists have fixed-size scratch buffers. A larger
    // list lets the native renderer keep geometry while submitting all six
    // faces and the restored lights. Apply this before eInitEngine runs.
    constexpr std::uintptr_t limits[] = {0x1009d2,0x1009dc,0x100a01,0x100a12};
    constexpr std::uint32_t original = 400 * 1024;
    constexpr std::uint32_t expanded = 1000 * 1024;
    for (const auto limit : limits) {
        if (!MatchGameBytes(limit, &original, sizeof(original))) return false;
    }
    std::size_t applied = 0;
    for (const auto limit : limits) {
        if (!WriteGameBytes(limit, &expanded, sizeof(expanded))) {
            while (applied) WriteGameBytes(limits[--applied], &original, sizeof(original));
            return false;
        }
        ++applied;
    }
    return true;
}

bool InstallRestoredReflectionLights() {
    // The cubemap already calls the native vehicle light renderer. Its last
    // argument suppresses flares. Keep the game's existing lighting check
    // and submit the flare pass using the same renderer and view.
    constexpr std::uintptr_t site = 0x2de81c;
    constexpr std::uint8_t nativePass[] = {0x6a,0x01,0x53,0x56,0xe8,0xdb,0x22,0x07,0,0x83,0xc4,0x18};
    constexpr std::uint8_t includeFlares = 0;
    return MatchGameBytes(site, nativePass, sizeof(nativePass)) &&
           WriteGameBytes(site + 1, &includeFlares, sizeof(includeFlares));
}

bool HasXenonEffectSites() {
    // XenonEffects 1.02 writes fixed addresses. Reject a changed image at
    // any of its hooks, including the release and reset paths.
    struct HookSite {
        std::uintptr_t offset;
        std::uint8_t bytes[5];
    };
    constexpr HookSite sites[] = {
        {0x10d43c, {0xe8,0xef,0xb7,0xff,0xff}},
        {0x2648bc, {0xe8,0x3f,0x67,0xe9,0xff}},
        {0x26493e, {0xe8,0xad,0xb1,0x07,0}},
        {0x2bd622, {0xe8,0x19,0x64,0,0}},
        {0x2db293, {0xe8,0x48,0x4a,0xff,0xff}},
        {0x2df4bb, {0xe8,0x40,0x48,0xe2,0xff}},
        {0x10a5d7, {0x8b,0x4c,0x24,0x18,0x89}},
        {0x350f48, {0x83,0x7e,0x04,0x03,0x75}},
        {0x356629, {0xe8,0xf2,0xf8,0xfe,0xff}},
        {0x35e6fc, {0,0x04,0,0,0xb9}},
        {0x35e766, {0,0x04,0,0,0x56}},
    };
    for (const auto& site : sites) {
        if (!MatchGameBytes(site.offset, site.bytes, sizeof(site.bytes))) return false;
    }
    return true;
}

const char* LoadOptionalXenonEffects() {
    if (!settingsPath[0] ||
        GetPrivateProfileIntW(L"Graphics", L"XenonEffects", 0, settingsPath) != 1) return "off";
    if (GetModuleHandleW(L"NFSMW_XenonEffects.asi")) return "already loaded";
    if (!HasXenonEffectSites()) return "overlapping or unsupported hooks";

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(engine, path, std::size(path));
    if (!length || length >= std::size(path)) return "invalid game path";
    auto* filename = wcsrchr(path, L'\\');
    if (!filename) return "invalid game path";
    *filename = 0;
    wchar_t workingDirectory[MAX_PATH]{};
    const DWORD current = GetCurrentDirectoryW(std::size(workingDirectory), workingDirectory);
    if (!current || current >= std::size(workingDirectory) || _wcsicmp(workingDirectory, path) != 0) {
        return "launch directory differs from game directory";
    }
    if (GetFileAttributesW(L"GLOBAL\\XenonEffects.tpk") == INVALID_FILE_ATTRIBUTES) return "missing texture pack";

    constexpr wchar_t module[] = L"\\UnlimitedCommunity\\NFSMW_XenonEffects.asi";
    if (filename - path + std::size(module) > std::size(path)) return "module path too long";
    wcscpy(filename, module);
    return LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)
               ? "enabled" : "module could not load";
}

} // namespace
