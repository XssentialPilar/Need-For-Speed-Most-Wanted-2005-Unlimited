#pragma once

namespace {
namespace unlimited_console {

using FileOpen = decltype(&CreateFileA);
FileOpen openGameFile = nullptr;
bool consoleTrack = false;
unsigned consoleVault = 0;
bool consolePlatforms = false;
volatile LONG trackOpened = 0;
volatile LONG vaultOpened = 0;

bool IsPathSeparator(char ch) { return ch == '\\' || ch == '/'; }

bool HasAssetFolder(const char* path, const char* filename, const char* folder) {
    const auto size = std::strlen(folder);
    if (filename - path < static_cast<std::ptrdiff_t>(size + 1) ||
        !IsPathSeparator(filename[-1])) return false;
    const auto* start = filename - size - 1;
    if (start != path && !IsPathSeparator(start[-1])) return false;
    for (std::size_t i = 0; i < size; ++i) {
        if (IsPathSeparator(folder[i]) && IsPathSeparator(start[i])) continue;
        if (_strnicmp(start + i, folder + i, 1) != 0) return false;
    }
    return true;
}

bool MatchConsoleAsset(const char* path, LONGLONG expectedSize, ULONG expectedCrc,
                       ULONG alternateCrc = 0, ULONG secondAlternateCrc = 0) {
    using Crc = ULONG (WINAPI*)(ULONG, const void*, ULONG);
    static const auto update = reinterpret_cast<Crc>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlComputeCrc32"));
    if (!update) return false;
    const auto file = openGameFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool valid = GetFileSizeEx(file, &size) && size.QuadPart == expectedSize;
    ULONG crc = 0;
    // Use large sequential reads without consuming the game's thread stack.
    constexpr DWORD chunk = 512 * 1024;
    const auto length = static_cast<DWORD>(std::min<LONGLONG>(chunk, expectedSize));
    std::unique_ptr<std::uint8_t[]> block(valid && length ? new (std::nothrow) std::uint8_t[length] : nullptr);
    valid = valid && block != nullptr;
    for (LONGLONG remaining = size.QuadPart; valid && remaining > 0;) {
        const auto requested = static_cast<DWORD>(std::min<LONGLONG>(remaining, length));
        DWORD received = 0;
        valid = ReadFile(file, block.get(), requested, &received, nullptr) && received == requested;
        if (!valid) break;
        crc = update(crc, block.get(), received);
        remaining -= received;
    }
    CloseHandle(file);
    return valid && (crc == expectedCrc || (alternateCrc && crc == alternateCrc) ||
                     (secondAlternateCrc && crc == secondAlternateCrc));
}

void ReportConsoleAsset(volatile LONG& counter, const char* name) {
    if (InterlockedExchange(&counter, 1) == 0 && logPath[0]) {
        if (LogFile log; log.file) std::fprintf(log.file, "%s\n", name);
    }
}

void SelectPlatformReflections() {
    if (!consolePlatforms || !openGameFile) return;
    constexpr char stock[] = "GLOBAL\\DYNTEX.BIN";
    static constexpr char console[] = "GLOBAL\\DYNTEX36.BIN";
    constexpr std::uintptr_t filenameRva = 0x4a866c, pointerRva = 0x4f7ed4;
    const char* original = &GameValue<char>(filenameRva);
    if (!MatchGameBytes(filenameRva, stock, sizeof(stock)) ||
        GameValue<const char*>(pointerRva) != original ||
        !MatchConsoleAsset(stock, 15320576, 0x15f2de0f) ||
        !MatchConsoleAsset(console, 14339072, 0x7aa3afe0)) return;
    // GameFlow uses this same filename for streaming, waiting and unloading.
    // Its async reads must all use the directory and offsets from the same pack.
    const char* selected = console;
    if (WriteGameBytes(pointerRva, &selected, sizeof(selected))) {
        if (LogFile log; log.file) std::fprintf(log.file, "Console showroom reflection archive selected\n");
    }
}

// The two collections are a pair. Moving the four sky IDs requires both archives.
bool shaderTexturesSelected = false;

bool WorldTextureArchivesReady() {
    if (!openGameFile || !GetPrivateProfileIntW(L"Graphics", L"X360ShaderTextures", 1, settingsPath)) return false;
    static constexpr char globalPc[] = "GLOBAL\\GLOBALB.LZC", globalRaw[] = "GLOBAL\\GLOBALB.BUN";
    static constexpr char global360[] = "GLOBAL\\GLOB36B.LZC";
    if (!MatchConsoleAsset(globalPc, 1922184, 0xd97f1754) ||
        !MatchConsoleAsset("GLOBAL\\INGAMEA.BUN", 11439360, 0x20eba990) ||
        !MatchConsoleAsset(global360, 13537216, 0xc7b964d6) ||
        !MatchConsoleAsset("GLOBAL\\INGAM36A.BUN", 11472384, 0xde83f081)) return false;
    // Some installations also carry the expanded PC copy. Preserve custom versions.
    if (GetFileAttributesA(globalRaw) != INVALID_FILE_ATTRIBUTES &&
        !MatchConsoleAsset(globalRaw, 6161984, 0x358e49ac)) return false;
    return true;
}

struct ConsolePlatform {
    const char* name;
    LONGLONG stockSize;
    ULONG stockCrc;
    LONGLONG consoleSize;
    ULONG consoleCrc;
    volatile LONG validation;
    volatile LONG opened;
};

ConsolePlatform platforms[] = {
    // FEManager garage types, starting at 1.
    {"PlatformCrib.BIN",               11158624, 0xb4b75f27, 32331104, 0x0388ffd6, 0, 0},
    {"CAREER_SAFEHOUSE.BIN",            9433536, 0x8baa5290, 30793152, 0x892cf806, 0, 0},
    {"CUSTOMIZATION_SHOP.BIN",         16606848, 0xa8b66248, 23209088, 0x2c8e9c49, 0, 0},
    {"CUSTOMIZATION_SHOP_BACKROOM.BIN",16579120, 0x6cccacf5, 23181360, 0x21f963bc, 0, 0},
    {"CAR_LOT.BIN",                     6476448, 0x45862d1e, 27175072, 0x61f5e9a3, 0, 0},
};

// Back room, dealership, shop, quick race, safehouse and cars in gameplay.
constexpr std::uintptr_t consoleCarRigRvas[] = {0x4f2b50, 0x4f2bf0, 0x4f2c90, 0x4f2d30, 0x4f2dd0, 0x4f2e70};

struct PlatformLightRig {
    std::uint8_t pc[160];
    std::uint8_t xbox[160];
};

const PlatformLightRig* platformLightRigs = nullptr;
volatile LONG rigsAttempted = 0;
volatile LONG garageShadowPass = 0;

void LoadConsolePlatformLights(HMODULE wrapper) {
    if (!consolePlatforms || !GetPrivateProfileIntW(L"Graphics", L"X360PlatformLighting", 1, settingsPath)) return;
    const auto resource = FindResourceW(wrapper, MAKEINTRESOURCEW(203), MAKEINTRESOURCEW(10));
    if (!resource || SizeofResource(wrapper, resource) != 4 + std::size(consoleCarRigRvas) * sizeof(PlatformLightRig))
        return;
    const auto* data = static_cast<const std::uint8_t*>(LockResource(LoadResource(wrapper, resource)));
    if (data && std::memcmp(data, "ULP1", 4) == 0)
        platformLightRigs = reinterpret_cast<const PlatformLightRig*>(data + 4);
}

void ApplyConsolePlatformLights() {
    if (!platformLightRigs || InterlockedExchange(&rigsAttempted, 1)) return;
    for (std::size_t i = 0; i < std::size(consoleCarRigRvas); ++i) {
        const auto& rig = platformLightRigs[i];
        if (!MatchGameBytes(consoleCarRigRvas[i], rig.pc, sizeof(rig.pc))) return;
    }
    for (std::size_t i = 0; i < std::size(consoleCarRigRvas); ++i) {
        const auto& rig = platformLightRigs[i];
        if (WriteGameBytes(consoleCarRigRvas[i], rig.xbox, sizeof(rig.xbox))) continue;
        while (i > 0) {
            --i;
            const auto& previous = platformLightRigs[i];
            WriteGameBytes(consoleCarRigRvas[i], previous.pc, sizeof(previous.pc));
        }
        return;
    }
    if (logPath[0]) {
        if (LogFile log; log.file) std::fprintf(log.file, "Console vehicle light rigs applied: %zu\n",
                                               std::size(consoleCarRigRvas));
    }
}

HANDLE OpenConsolePlatform(const char* path, const char* filename, DWORD access,
                           DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags,
                           HANDLE model) {
    if (!HasAssetFolder(path, filename, "FRONTEND\\PLATFORMS")) return INVALID_HANDLE_VALUE;
    const auto prefix = static_cast<std::size_t>(filename - path);
    constexpr char directory[] = "Unlimited360\\";
    if (prefix + sizeof(directory) + std::strlen(filename) > MAX_PATH) return INVALID_HANDLE_VALUE;
    for (auto& platform : platforms) {
        if (_stricmp(filename, platform.name)) continue;
        char alternate[MAX_PATH]{};
        std::memcpy(alternate, path, prefix);
        std::memcpy(alternate + prefix, directory, sizeof(directory) - 1);
        std::memcpy(alternate + prefix + sizeof(directory) - 1, platform.name, std::strlen(platform.name) + 1);

        // The stock CRC protects customized platform meshes. Check both data
        // files on the first load and reuse the result during menu transitions.
        LONG result = InterlockedCompareExchange(&platform.validation, 0, 0);
        if (!result) {
            result = MatchConsoleAsset(path, platform.stockSize, platform.stockCrc) &&
                MatchConsoleAsset(alternate, platform.consoleSize, platform.consoleCrc) ? 1 : -1;
            InterlockedCompareExchange(&platform.validation, result, 0);
        }
        if (result < 0) return INVALID_HANDLE_VALUE;
        const auto file = openGameFile(alternate, access, share, security, creation, flags, model);
        if (file == INVALID_HANDLE_VALUE) return file;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart != platform.consoleSize) {
            CloseHandle(file);
            return INVALID_HANDLE_VALUE;
        }
        if (InterlockedExchange(&platform.opened, 1) == 0 && logPath[0]) {
            if (LogFile log; log.file) std::fprintf(log.file, "Console platform opened: %s\n", platform.name);
        }
        ApplyConsolePlatformLights();
        return file;
    }
    return INVALID_HANDLE_VALUE;
}

HANDLE WINAPI OpenConsoleAsset(const char* path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                               DWORD creation, DWORD flags, HANDLE model) {
    if (path && creation == OPEN_EXISTING && !(access & (GENERIC_WRITE | DELETE)) && std::strlen(path) < MAX_PATH) {
        const auto* filename = path;
        for (auto* ch = path; *ch; ++ch) {
            if (IsPathSeparator(*ch)) filename = ch + 1;
        }
        if (shaderTexturesSelected && HasAssetFolder(path, filename, "GLOBAL")) {
            const char* selected = nullptr;
            if (!_stricmp(filename, "GLOBALB.LZC") || !_stricmp(filename, "GLOBALB.BUN"))
                selected = "GLOB36B.LZC";
            else if (!_stricmp(filename, "INGAMEA.BUN")) selected = "INGAM36A.BUN";
            if (selected) {
                const auto prefix = static_cast<std::size_t>(filename - path);
                char variant[MAX_PATH]{};
                if (prefix + std::strlen(selected) < sizeof(variant)) {
                    std::memcpy(variant, path, prefix);
                    std::strcpy(variant + prefix, selected);
                    const auto file = openGameFile(variant, access, share, security, creation, flags, model);
                    if (file != INVALID_HANDLE_VALUE) return file;
                }
            }
        }
        if (consoleVault && !_stricmp(filename, "attributes.bin") && HasAssetFolder(path, filename, "GLOBAL") &&
            MatchConsoleAsset(path, 689728, 0xca3ac558)) {
            char variant[MAX_PATH]{};
            const auto prefix = filename - path;
            std::memcpy(variant, path, prefix);
            if (consoleVault == 2 && prefix + sizeof("ATTRIBX36ALT.BIN") <= MAX_PATH) {
                std::memcpy(variant + prefix, "ATTRIBX36ALT.BIN", sizeof("ATTRIBX36ALT.BIN"));
                if (MatchConsoleAsset(variant, 659184, 0x8de46c81, 0xe7a81f07)) {
                    const auto file = openGameFile(variant, access, share, security, creation, flags, model);
                    if (file != INVALID_HANDLE_VALUE) {
                        ReportConsoleAsset(vaultOpened, "Console alternative material vault opened");
                        return file;
                    }
                }
            }
            if (prefix + sizeof("ATTRIBUTE360.BIN") <= MAX_PATH) {
                std::memcpy(variant + prefix, "ATTRIBUTE360.BIN", sizeof("ATTRIBUTE360.BIN"));
                if (MatchConsoleAsset(variant, 652288, 0x1b8067b8, 0x2e0c210f, 0x0cf8f430)) {
                    const auto file = openGameFile(variant, access, share, security, creation, flags, model);
                    if (file != INVALID_HANDLE_VALUE) {
                        ReportConsoleAsset(vaultOpened, "Console visual vault opened");
                        return file;
                    }
                }
            }
        }
        if (consoleTrack && !_stricmp(filename, "STREAML2RA.BUN") && HasAssetFolder(path, filename, "TRACKS")) {
            char variant[MAX_PATH]{};
            const auto prefix = filename - path;
            std::memcpy(variant, path, prefix);
            std::memcpy(variant + prefix, "STREAML2X6.BUN", sizeof("STREAML2X6.BUN"));
            HANDLE file = openGameFile(variant, access, share, security, creation, flags, model);
            if (file != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER size{};
                if (GetFileSizeEx(file, &size) && size.QuadPart == 532663556) {
                    ReportConsoleAsset(trackOpened, "Console track stream opened");
                    return file;
                }
                CloseHandle(file);
            }
        }
        if (consolePlatforms) {
            const auto file = OpenConsolePlatform(path, filename, access, share, security, creation, flags, model);
            if (file != INVALID_HANDLE_VALUE) return file;
        }
    }
    return openGameFile(path, access, share, security, creation, flags, model);
}

void InstallAssetRouting() {
    consoleTrack = GetPrivateProfileIntW(L"Graphics", L"X360Track", 0, settingsPath) == 1;
    const auto selectedVault = GetPrivateProfileIntW(L"Graphics", L"X360Vlt", 1, settingsPath);
    consoleVault = selectedVault <= 2 ? selectedVault : 0;
    consolePlatforms = GetPrivateProfileIntW(L"Graphics", L"X360Platforms", 1, settingsPath) == 1;
    if (!consoleTrack && !consoleVault && !consolePlatforms &&
        !GetPrivateProfileIntW(L"Graphics", L"X360ShaderTextures", 1, settingsPath)) return;
    constexpr auto fileOpenImport = 0x4902ac;
    openGameFile = GameValue<FileOpen>(fileOpenImport);
    const auto hook = &OpenConsoleAsset;
    if (openGameFile) WriteGameBytes(fileOpenImport, &hook, sizeof(hook));
}

constexpr std::uintptr_t queueSceneGeometry = 0x323fa0;

void __cdecl BuildConsoleShadowDirection(float* destination, const float* sun) {
    // The console preset limits the normalized sun vector's vertical component
    // before the game's own normalization builds the projected shadow direction.
    const float length = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
    float direction[3] = {1.0f, 0.0f, 0.7f};
    if (std::isfinite(length) && length > 0.0f) {
        direction[0] = sun[0] / length;
        direction[1] = sun[1] / length;
        direction[2] = std::max(0.7f, sun[2] / length);
    }
    using Normalize = void (__cdecl*)(float*, const float*);
    GameFunction<Normalize>(0x5f490)(destination, direction);
}

void InstallConsoleShadowDirection() {
    if (GetPrivateProfileIntW(L"Graphics", L"X360ShadowAngle", 1, settingsPath))
        RedirectCall(0x2e455a, 0x5f490, reinterpret_cast<std::uintptr_t>(&BuildConsoleShadowDirection));
}

// __fastcall preserves the retail method's ECX and two stack arguments.
void __fastcall QueueGarageShadowGeometry(void* scene, void*, void* view, std::uint32_t flags) {
    using QueueScene = void (__thiscall*)(void*, void*, std::uint32_t);
    const auto queueScene = reinterpret_cast<QueueScene>(&GameValue<std::uint8_t>(queueSceneGeometry));
    queueScene(scene, view, flags);
    if (nativePassesAtStartup != 9 || GameValue<unsigned>(gameFlowStateRva) != 3 ||
        GameValue<std::int32_t>(0x501830) <= 0) return;
    const auto* manager = GameValue<const std::uint8_t*>(0x51cae0);
    if (!manager) return;
    const auto garage = *reinterpret_cast<const unsigned*>(manager + 0x18);
    if (!garage || garage > std::size(platforms) ||
        !InterlockedCompareExchange(&platforms[garage - 1].opened, 0, 0)) return;

    // Use the existing showroom package and the engine's shadow render view.
    auto* showroom = GameFunction<void* (__cdecl*)()>(0x3a1fa0)();
    if (!showroom) return;
    constexpr int shadowView = 4;
    using RenderShowroom = void (__thiscall*)(void*, int);
    const auto renderShowroom = reinterpret_cast<RenderShowroom>(&GameValue<std::uint8_t>(0x3a2200));
    renderShowroom(showroom, shadowView);
    using RenderCarShadows = void (__cdecl*)(void*, int);
    const auto renderCars = reinterpret_cast<RenderCarShadows>(&GameValue<std::uint8_t>(0x350980));
    renderCars(&GameValue<std::uint8_t>(0x519c70), 0);
    ReportConsoleAsset(garageShadowPass, "Console garage shadow geometry submitted");
}

void InstallConsoleGarageShadows() {
    if (!consolePlatforms || !GetPrivateProfileIntW(L"Graphics", L"X360PlatformShadows", 1, settingsPath)) return;
    RedirectCall(0x2e5186, queueSceneGeometry, reinterpret_cast<std::uintptr_t>(&QueueGarageShadowGeometry));
}

struct LightMaterial {
    std::uint32_t hash;
    std::array<float, 30> pc;
    std::array<float, 30> xbox;
};

struct MaterialHeader {
    char magic[4];
    std::uint32_t count;
};

static_assert(sizeof(LightMaterial) == 244 && sizeof(MaterialHeader) == 8);
const LightMaterial* materials = nullptr;
std::uint32_t materialCount = 0;
volatile LONG appliedMaterials = 0;

const LightMaterial* FindConsoleMaterial(const std::uint8_t* material) {
    const auto hash = *reinterpret_cast<const std::uint32_t*>(material + 0x0c);
    const auto* end = materials + materialCount;
    const auto* entry = std::lower_bound(materials, end, hash,
        [](const LightMaterial& item, std::uint32_t key) { return item.hash < key; });
    return entry != end && entry->hash == hash ? entry : nullptr;
}

void WriteConsoleMaterial(std::uint8_t* material, const LightMaterial& entry) {
    std::memcpy(material + 0x30, entry.xbox.data(), sizeof(entry.xbox));
    InterlockedIncrement(&appliedMaterials);
    if (logPath[0]) {
        if (LogFile log; log.file)
            std::fprintf(log.file, "Console light material: %.28s (%08x)\n",
                         reinterpret_cast<const char*>(material + 0x14), entry.hash);
    }
}

using BuildMaterial = void (__thiscall*)(void*);
constexpr std::uintptr_t buildMaterialRva = 0x2be1b0;

void __fastcall ApplyConsoleMaterial(std::uint8_t* material, void*) {
    const auto* entry = FindConsoleMaterial(material);
    const bool matches = entry && std::memcmp(material + 0x30, entry->pc.data(), sizeof(entry->pc)) == 0;
    GameFunction<BuildMaterial>(buildMaterialRva)(material);
    if (matches) WriteConsoleMaterial(material, *entry);
}

bool InstallConsoleMaterials(HMODULE wrapper) {
    constexpr std::uint8_t materialBuild[] = {0x8b, 0x41, 0x0c, 0x3d, 0x14, 0x3a, 0xf2, 0x89};
    if (!MatchGameBytes(buildMaterialRva, materialBuild, sizeof(materialBuild))) return false;
    const auto resource = FindResourceW(wrapper, MAKEINTRESOURCEW(202), MAKEINTRESOURCEW(10));
    if (!resource) return false;
    const auto length = SizeofResource(wrapper, resource);
    const auto* data = static_cast<const MaterialHeader*>(LockResource(LoadResource(wrapper, resource)));
    if (!data || length < sizeof(MaterialHeader) || std::memcmp(data->magic, "ULM1", 4) != 0 ||
        !data->count || data->count > 128 ||
        length != sizeof(MaterialHeader) + data->count * sizeof(LightMaterial)) return false;
    const auto* entries = reinterpret_cast<const LightMaterial*>(data + 1);
    std::uint32_t previous = 0;
    for (std::uint32_t i = 0; i < data->count; ++i) {
        if (entries[i].hash <= previous) return false;
        previous = entries[i].hash;
    }
    materials = entries;
    materialCount = data->count;
    return RedirectCall(0x101a31, buildMaterialRva, reinterpret_cast<std::uintptr_t>(&ApplyConsoleMaterial));
}

}
}
