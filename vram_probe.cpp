#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include "nfs_controller.hpp"
#include "nfs_discx.hpp"
#include "nfs_rumble.hpp"
#include "renderer-lab/scene_hud_d3d12.hpp"
#include "renderer-lab/frame_present.hpp"
#include "nfs_window_mode.hpp"
#include "third-party/split-screen/integration.hpp"
#include "nfs_settings.hpp"

namespace {
constexpr std::uint64_t MiB = 1024 * 1024;
constexpr std::uintptr_t trackStreamerRva = 0x5aa9d0;
constexpr std::uintptr_t gameFlowStateRva = 0x525e90;
constexpr std::uintptr_t simulationTickRva = 0x5885d4;
constexpr std::uintptr_t gameClockRva = 0x525b14;
constexpr std::uintptr_t realFrameTimeRva = 0x5259bc;

using Create9 = IDirect3D9* (WINAPI*)(UINT);
using CreateDevice = HRESULT (WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                      D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using AvailableMemory = UINT (WINAPI*)(IDirect3DDevice9*);
using DeviceReset = HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);

CreateDevice nativeCreateDevice;
AvailableMemory nativeAvailableMemory;
// The game owns this state for its process lifetime. Release the GPU objects
// before the game's device is released. A fast ExitProcess must never call a
// GPU driver from a CRT destructor while Windows is unloading driver DLLs.
frame_present::Presenter& nativeFrames = *new frame_present::Presenter;
D3DPRESENT_PARAMETERS requestedDisplay{};
bool originalPresentationOnly = false;
bool restoreOriginalOnReset = false;
HWND gameFocusWindow = nullptr;
unlimited_display::WindowHost displayHost;
unlimited_display::Mode requestedWindowMode = unlimited_display::Mode::Exclusive;
bool displayResetInstalled = false;
bool windowModePending = false;
bool frameStatistics = false;
std::atomic<unsigned> lastFrameApi{0};
IDirect3D9* trackedFactory;
IDirect3DDevice9* trackedDevice;
IDXGIAdapter3* adapter;
HMODULE engine;
LARGE_INTEGER frequency{};
wchar_t logPath[MAX_PATH]{};
wchar_t settingsPath[MAX_PATH]{};
constexpr unsigned automaticFrameLimit = UINT_MAX;
constexpr unsigned frameLimits[] = {30, 40, 60, 70, 90, 144, 0, automaticFrameLimit};
constexpr const char* frameLimitLabels[] = {"30", "40", "60", "70", "90", "144"};
constexpr std::size_t unlimitedIndex = std::size(frameLimits) - 2;
constexpr std::size_t automaticIndex = std::size(frameLimits) - 1;
std::size_t frameLimitIndex = automaticIndex;
unsigned availableThreads = 0;

unsigned AvailableGameThreads() {
    DWORD_PTR process = 0, system = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process) {
        unsigned count = 0;
        for (; process; process &= process - 1) ++count;
        return count;
    }
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}

bool LowThreadBudget() {
    return availableThreads > 0 && availableThreads <= 2;
}

unsigned EffectiveFrameLimit() {
    return frameLimitIndex == automaticIndex ? (LowThreadBudget() ? 60 : 0) : frameLimits[frameLimitIndex];
}
unsigned simulationRate = 60;
bool shadowHookInstalled = false;
bool aspectHookInstalled = false;
bool renderPoolsExpanded = false;
bool reflectionQualityInstalled = false;
int lastReflectionQuality = -1;
bool languageBootstrapInstalled = false;
int lastShadowSetting = -1;
constexpr unsigned movieApis[] = {9, 11, 12, 13};
constexpr const wchar_t* movieApiValues[] = {L"9", L"11", L"12", L"13"};
constexpr const char* movieApiNames[] = {"DirectX 9", "DirectX 11", "DirectX 12", "Vulkan"};
unsigned nativePassesRequested = 9;
unsigned nativePassesAtStartup = 9;
std::atomic<unsigned> lastMovieApi{0};
bool nativePassesActive = false;
bool nativePassFactorySelected = false;

class LogFile {
public:
    LogFile() : file(_wfopen(logPath, L"a")) {}
    ~LogFile() { if (file) std::fclose(file); }
    FILE* const file;
};

HMODULE LoadSystemLibrary(const wchar_t* file) {
    wchar_t path[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length + wcslen(file) + 1 >= MAX_PATH) return nullptr;
    wcscat(path, L"\\");
    wcscat(path, file);
    return LoadLibraryW(path);
}

HMODULE SystemD3D9() {
    static const auto library = LoadSystemLibrary(L"d3d9.dll");
    return library;
}

IDirect3D9* SystemFactory(UINT sdkVersion) {
    const auto library = SystemD3D9();
    static const auto create = library ? reinterpret_cast<Create9>(GetProcAddress(library, "Direct3DCreate9")) : nullptr;
    return create ? create(sdkVersion) : nullptr;
}

bool IsTrackedBuild(HMODULE module) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes + dos->e_lfanew);
    constexpr std::uint8_t signature[] = {
        0xb8,0x00,0x40,0xef,0x07,0x74,0x0b,0x81,0xee,0x00,0x00,0x04,0x00,0xb8,0x00,0x40,0xeb,0x07
    };
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->FileHeader.TimeDateStamp == 0x438e4c8c && nt->OptionalHeader.SizeOfImage == 0x678e4e &&
           std::memcmp(bytes + 0x264a35, signature, sizeof(signature)) == 0;
}

bool ReplaceMethod(void** table, std::size_t index, void* replacement) {
    void** slot = table + index;
    DWORD previous;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) return false;
    InterlockedExchangePointer(slot, replacement);
    DWORD restored;
    VirtualProtect(slot, sizeof(*slot), previous, &restored);
    return true;
}

template<typename T>
T& GameValue(std::uintptr_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(engine) + offset);
}

bool WriteCodeBytes(void* dest, const void* data, std::size_t size) {
    DWORD old;
    if (!VirtualProtect(dest, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(dest, data, size);
    FlushInstructionCache(GetCurrentProcess(), dest, size);
    DWORD restored;
    VirtualProtect(dest, size, old, &restored);
    return true;
}

bool WriteGameBytes(std::uintptr_t offset, const void* data, std::size_t size) {
    return WriteCodeBytes(&GameValue<std::uint8_t>(offset), data, size);
}

bool MatchGameBytes(std::uintptr_t offset, const void* bytes, std::size_t size) {
    return std::memcmp(&GameValue<std::uint8_t>(offset), bytes, size) == 0;
}

template<std::size_t N>
bool InstallInlineCall(std::uintptr_t offset, const std::uint8_t (&expected)[N], void (*handler)()) {
    static_assert(N >= 5);
    if (!MatchGameBytes(offset, expected, N)) return false;
    std::array<std::uint8_t, N> bytes;
    bytes.fill(0x90);
    bytes[0] = 0xe8;
    const auto displacement = reinterpret_cast<std::uintptr_t>(handler) -
                              (reinterpret_cast<std::uintptr_t>(engine) + offset + 5);
    std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    return WriteGameBytes(offset, bytes.data(), N);
}

// Font coordinates in language data are expressed in texels of the original
// atlas. Keep those units when the same atlas layout is replaced at higher
// resolution; other textures still use their own dimensions.
extern "C" __attribute__((used)) std::uint32_t __cdecl NfsmwFontAtlasSize(const std::uint8_t* texture) {
    std::uint32_t hash, size;
    std::memcpy(&hash, texture + 0x24, sizeof(hash));
    std::memcpy(&size, texture + 0x44, sizeof(size));
    struct Atlas {
        std::uint32_t hash;
        unsigned width;
        unsigned height;
    };
    constexpr Atlas atlases[] = {
        {0xa87927be, 256, 256}, // FONT_ARIAL
        {0xab6215c1, 256, 256}, // FONT_ARIAL12
        {0x545570c6, 256, 256}, // FONT_MW_BODY
        {0xe0463afa, 512, 512}, // FONT_MW_TITLE
        {0xc7bd87ce, 128,  64}, // FONT_MW_CUSTOM_NUMBERS
        {0x01424892, 128,  64}, // TAC_NUMBERS
    };
    const unsigned width = size & 0xffff;
    const unsigned height = size >> 16;
    for (const auto& atlas : atlases) {
        if (atlas.hash == hash && width > atlas.width && width * atlas.height == height * atlas.width) {
            return atlas.width | (atlas.height << 16);
        }
    }
    return size;
}

__attribute__((naked)) void FontGlyphDimensions() {
    asm(
        "pushfl\n"
        "pushl %eax\n"
        "pushl %ecx\n"
        "pushl %edx\n"
        "pushl %esi\n"
        "call _NfsmwFontAtlasSize\n"
        "addl $4, %esp\n"
        "movzwl %ax, %edi\n"
        "movl %eax, %esi\n"
        "shrl $16, %esi\n"
        "popl %edx\n"
        "popl %ecx\n"
        "popl %eax\n"
        "popfl\n"
        "movl %edi, 0x28(%esp)\n"
        "leal -1(%eax), %edi\n"
        "ret\n");
}

__attribute__((naked)) void FontSamplingDimensions() {
    asm(
        "pushfl\n"
        "pushl %ecx\n"
        "pushl %ecx\n"
        "call _NfsmwFontAtlasSize\n"
        "addl $4, %esp\n"
        "popl %ecx\n"
        "movzwl %ax, %edx\n"
        "shrl $16, %eax\n"
        "popfl\n"
        "movl %edx, 0x1c(%esp)\n"
        "ret\n");
}

bool InstallFontAtlasScaling() {
    constexpr std::uintptr_t glyphOffset = 0x185d70, samplerOffset = 0x2d14f6;
    constexpr std::uint8_t glyph[] = {0x0f,0xbf,0x7e,0x44,0x89,0x7c,0x24,0x24,0x8d,0x78,0xff,0x0f,0xbf,0x76,0x46};
    constexpr std::uint8_t sampler[] = {0x0f,0xbf,0x51,0x44,0x89,0x54,0x24,0x18,0x0f,0xbf,0x41,0x46};
    if (!MatchGameBytes(glyphOffset, glyph, sizeof(glyph)) ||
        !MatchGameBytes(samplerOffset, sampler, sizeof(sampler)) ||
        !InstallInlineCall(glyphOffset, glyph, &FontGlyphDimensions)) return false;
    if (InstallInlineCall(samplerOffset, sampler, &FontSamplingDimensions)) return true;
    WriteGameBytes(glyphOffset, glyph, sizeof(glyph));
    return false;
}

template<typename Function>
Function GameFunction(std::uintptr_t offset) {
    return reinterpret_cast<Function>(&GameValue<std::uint8_t>(offset));
}

bool RedirectCall(std::uintptr_t offset, std::uintptr_t original, std::uintptr_t destination) {
    if (GameValue<std::uint8_t>(offset) != 0xe8) return false;
    const auto next = offset + 5;
    const auto relative = original - next;
    if (!MatchGameBytes(offset + 1, &relative, sizeof(relative))) return false;
    const auto replacement = destination - (reinterpret_cast<std::uintptr_t>(engine) + next);
    return WriteGameBytes(offset + 1, &replacement, sizeof(replacement));
}

// The scheduler retains a fixed integration step. Drawing is independent:
// use the elapsed game clock, carry the rounding error, and skip the original
// per-frame Sleep that returned too late on coarse Windows timers.
struct SchedulerTime {
    std::uint32_t lastTicks;
    float total;
    float remainder;
    float step;
    std::uint8_t singleStep;
};

double __fastcall ServiceScheduler(void* scheduler, void*, std::uint8_t force) {
    using Service = double (__thiscall*)(void*, std::uint8_t);
    if (force) return GameFunction<Service>(0x242e60)(scheduler, force);

    using GetTime = void (__thiscall*)(void*, std::uint32_t*);
    using RunSteps = void (__thiscall*)(void*, unsigned);
    auto& timing = *reinterpret_cast<SchedulerTime*>(static_cast<std::uint8_t*>(scheduler) + 0x14);
    std::uint32_t ticks = 0;
    GameFunction<GetTime>(0x2277d0)(scheduler, &ticks);
    const auto elapsedTicks = static_cast<std::int32_t>(ticks - timing.lastTicks);
    timing.lastTicks = ticks;
    if (elapsedTicks < 0) return 0.0;
    const float elapsed = elapsedTicks * 0.00025f;
    timing.total += elapsed;
    const float due = elapsed / timing.step + timing.remainder;
    const auto requested = static_cast<unsigned>(std::clamp(due + 0.5f, 0.0f, 24.0f));
    const auto stepsBefore = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(scheduler) + 0x28);
    GameFunction<RunSteps>(0x242db0)(scheduler, requested);
    const auto stepsAfter = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(scheduler) + 0x28);
    const unsigned steps = stepsAfter - stepsBefore;
    timing.remainder = timing.singleStep ? 0.0f : std::clamp(due - steps, -0.5f, 18.0f);
    return static_cast<double>(steps) * timing.step;
}

template<unsigned TicksPerSecond>
int __cdecl FrontEndTicks() {
    static double remainder = 0;
    const double due = std::max(0.0, static_cast<double>(GameValue<float>(realFrameTimeRva))) *
                       TicksPerSecond + remainder;
    const int ticks = static_cast<int>(due);
    remainder = due - ticks;
    return ticks;
}

bool ReplaceTickConversion(std::uintptr_t offset, const std::uint8_t (&expected)[11], std::uintptr_t handler) {
    if (!MatchGameBytes(offset, expected, sizeof(expected))) return false;
    std::uint8_t code[] = {0xdd, 0xd8, 0xe8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
    const auto relative = handler - (reinterpret_cast<std::uintptr_t>(engine) + offset + 7);
    std::memcpy(code + 3, &relative, sizeof(relative));
    return WriteGameBytes(offset, code, sizeof(code));
}

bool InstallUncappedTiming(unsigned rate) {
    constexpr std::uintptr_t initialStep = 0x266100;
    constexpr std::uintptr_t schedulerCall = 0x263dcd;
    constexpr std::uintptr_t schedulerService = 0x242e60;
    constexpr std::uint8_t stockInit[] = {0x68, 0x89, 0x88, 0x88, 0x3c, 0xe8, 0xd7, 0x23, 0xfe, 0xff};
    constexpr std::uint8_t stockClock[] = {0xdd, 0xd8, 0x75, 0x05, 0xb8, 1, 0, 0, 0, 0xd9, 0x05};
    constexpr std::uint8_t saveScreenTicks[] = {0xd8, 0x0d, 0x00, 0x42, 0x89, 0, 0xe8, 0xa1, 8, 0x26, 0};
    constexpr std::uint8_t menuTicks[] = {0xd8, 0x0d, 0xbc, 0x70, 0x89, 0, 0xe8, 0x6a, 7, 0x26, 0};
    constexpr std::uintptr_t stepBudget = 0x242ddc;
    constexpr std::uint8_t originalBudget[] = {0x83,0xfa,5,0x8d,0x0c,0x8d,2,0,0,0};
    // At 240 Hz, a 30 FPS frame needs eight steps. Allow enough catch-up
    // for a slow frame, while preserving the original two-step pause path.
    constexpr std::uint8_t expandedBudget[] = {0x6b,0xc9,0x10,0x83,0xc1,2,0x83,0xfa,5,0x90};
    if (!MatchGameBytes(initialStep - 1, stockInit, sizeof(stockInit)) ||
        !MatchGameBytes(0x25c700, stockClock, sizeof(stockClock)) ||
        !MatchGameBytes(stepBudget, originalBudget, sizeof(originalBudget)) ||
        !RedirectCall(schedulerCall, schedulerService, reinterpret_cast<std::uintptr_t>(&ServiceScheduler))) return false;
    const auto restoreScheduler = [&] {
        RedirectCall(schedulerCall, reinterpret_cast<std::uintptr_t>(&ServiceScheduler) -
                     reinterpret_cast<std::uintptr_t>(engine),
                     reinterpret_cast<std::uintptr_t>(engine) + schedulerService);
    };
    if (!WriteGameBytes(stepBudget, expandedBudget, sizeof(expandedBudget))) {
        restoreScheduler();
        return false;
    }
    const float step = 1.0f / static_cast<float>(rate);
    if (!WriteGameBytes(initialStep, &step, sizeof(step))) {
        WriteGameBytes(stepBudget, originalBudget, sizeof(originalBudget));
        restoreScheduler();
        return false;
    }
    const std::uint8_t jump = 0xeb;
    WriteGameBytes(0x25c702, &jump, sizeof(jump));
    ReplaceTickConversion(0x1642d4, saveScreenTicks, reinterpret_cast<std::uintptr_t>(&FrontEndTicks<1000>));
    ReplaceTickConversion(0x16440b, menuTicks, reinterpret_cast<std::uintptr_t>(&FrontEndTicks<1200>));
    simulationRate = rate;
    return true;
}

bool LocateSettings() {
    if (settingsPath[0]) return true;
    const DWORD count = GetModuleFileNameW(engine, settingsPath, MAX_PATH);
    if (!count || count >= MAX_PATH) {
        settingsPath[0] = 0;
        return false;
    }
    auto* name = wcsrchr(settingsPath, L'\\');
    constexpr wchar_t settingsName[] = L"NFSMW-Unlimited.ini";
    if (!name || name + std::size(settingsName) >= settingsPath + MAX_PATH) {
        settingsPath[0] = 0;
        return false;
    }
    wcscpy(name + 1, settingsName);
    return true;
}

void LoadFrameLimit() {
    if (!LocateSettings()) return;
    requestedWindowMode = unlimited_display::DecodeMode(GetPrivateProfileIntW(L"Display",L"WindowMode",0,settingsPath));
    const unsigned saved = GetPrivateProfileIntW(L"Graphics", L"FrameLimit", -1, settingsPath);
    const auto movie = GetPrivateProfileIntW(L"Graphics", L"NativePasses", 9, settingsPath);
    const auto selected = std::find(std::begin(movieApis), std::end(movieApis), movie);
    nativePassesAtStartup = nativePassesRequested = selected == std::end(movieApis) ? 9 : *selected;
    const auto limit = std::find(std::begin(frameLimits), std::end(frameLimits), saved);
    if (limit != std::end(frameLimits)) frameLimitIndex = limit - std::begin(frameLimits);
}

bool SelectWindowMode(unlimited_display::Mode mode) {
    if (!displayResetInstalled || !settingsPath[0]) return false;
    wchar_t value[8]{};
    std::swprintf(value,std::size(value),L"%u",static_cast<unsigned>(mode));
    if (!WritePrivateProfileStringW(L"Display",L"WindowMode",value,settingsPath)) return false;
    requestedWindowMode=mode;
    windowModePending=true;
    return true;
}

} // namespace

#include "nfs_aspect.hpp"
#include "nfs_hud.hpp"
#include "nfs_hd_cursor.hpp"
#include "nfs_brand.hpp"
#include "nfs_language.hpp"
#include "nfs_frontend_layout.hpp"
#include "nfs_visual.hpp"
#include "nfs_console_assets.hpp"
#include "nfs_movies.hpp"
#include "nfs_scene_shadow.hpp"

namespace {

void SetOptionText(std::uint8_t* option, const char* title, const char* value) {
    using Print = void (__thiscall*)(std::uint8_t*, const char*);
    const auto setString = GameFunction<Print>(0x1bcc10);
    const auto setText = [&](std::uint8_t* object, const char* text) {
        if (!object) return;
        setString(object, text);
        *reinterpret_cast<std::uint32_t*>(object + 0x1c) |= 0x400000u;
    };
    setText(*reinterpret_cast<std::uint8_t**>(option + 0x2c), title);
    setText(*reinterpret_cast<std::uint8_t**>(option + 0x30), value);
}

void __fastcall DrawFrameLimit(std::uint8_t* option) {
    const bool spanish = GameValue<int>(0x4f41c0) == 4;
    const char* label = frameLimitIndex == automaticIndex
        ? (spanish ? u8"Automático" : "Automatic")
        : frameLimitIndex == unlimitedIndex ? (spanish ? u8"Sin límite" : "Unlimited")
                                            : frameLimitLabels[frameLimitIndex];
    SetOptionText(option, spanish ? u8"Límite de FPS" : "Frame limit", label);
}

void __fastcall ChangeFrameLimit(std::uint8_t* option, void*, const char*, std::int32_t message) {
    constexpr std::int32_t left = -1860157282, right = -1248388111;
    constexpr std::size_t count = std::size(frameLimits);
    if (message != left && message != right) return;
    frameLimitIndex = (frameLimitIndex + (message == left ? count - 1 : 1)) % count;
    wchar_t value[8]{};
    if (frameLimitIndex == automaticIndex) std::wcscpy(value, L"-1");
    else std::swprintf(value, std::size(value), L"%u", frameLimits[frameLimitIndex]);
    const bool saved = settingsPath[0] && WritePrivateProfileStringW(L"Graphics", L"FrameLimit", value, settingsPath);
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Graphics option: frame_limit=%u saved=%u\n", frameLimits[frameLimitIndex], saved);
    }
    DrawFrameLimit(option);
}

using OptionAct = void (__fastcall*)(std::uint8_t*, void*, const char*, std::int32_t);
using OptionDraw = void (__fastcall*)(std::uint8_t*);
std::uint8_t* AddUnlimitedVideoOption(std::uint8_t* screen, std::array<void*, 15>& vtable,
                             OptionAct change, OptionDraw draw) {
    // Both display packages have 15 authored label/value pairs. The advanced
    // list already occupies more than nine before custom controls are appended.
    const int row=*reinterpret_cast<int*>(screen + 0x104);
    if (row < 1 || row > 15) return nullptr;
    using FindRowObject = void* (__thiscall*)(std::uint8_t*, const char*);
    const auto findRowObject = GameFunction<FindRowObject>(0x173cc0);
    const auto* prefixes = reinterpret_cast<const char* const*>(screen + 0xb0);
    if (!prefixes[0] || !prefixes[1] ||
        !findRowObject(screen, prefixes[0]) || !findRowObject(screen, prefixes[1])) return nullptr;

    using Allocate = void* (__cdecl*)(std::size_t);
    using ConstructToggle = void (__thiscall*)(void*, bool);
    using AddOption = void (__thiscall*)(std::uint8_t*, void*, int);
    void* option = GameFunction<Allocate>(0x252ad0)(0x5c);
    if (!option) return nullptr;
    GameFunction<ConstructToggle>(0x11a8f0)(option, true);
    if (!vtable[0]) {
        std::copy_n(*static_cast<void***>(option), vtable.size(), vtable.begin());
        vtable[1] = reinterpret_cast<void*>(change);
        vtable[3] = reinterpret_cast<void*>(draw);
    }
    *static_cast<void***>(option) = vtable.data();
    GameFunction<AddOption>(0x188570)(screen, option, 1);
    return static_cast<std::uint8_t*>(option);
}

bool xenonPreference = false;
bool xenonStartup = false;
bool xenonUnavailable = false;

void __fastcall DrawXenonEffects(std::uint8_t* option) {
    const bool spanish = GameValue<int>(0x4f41c0) == 4;
    const bool changed = xenonPreference != xenonStartup;
    const char* value = spanish
        ? (xenonPreference ? (changed ? "Activados al reiniciar" : "Activados") : (changed ? "Desactivados al reiniciar" : "Desactivados"))
        : (xenonPreference ? (changed ? "On after restart" : "On") : (changed ? "Off after restart" : "Off"));
    SetOptionText(option, spanish ? "Efectos de Xbox 360" : "Xbox 360 effects",
                  !changed && xenonUnavailable ? (spanish ? "No disponibles" : "Unavailable") : value);
}

void __fastcall ChangeXenonEffects(std::uint8_t* option, void*, const char*, std::int32_t message) {
    constexpr std::int32_t left = -1860157282, right = -1248388111;
    if ((message != left && message != right) || !settingsPath[0] ||
        !WritePrivateProfileStringW(L"Graphics", L"XenonEffects", xenonPreference ? L"0" : L"1", settingsPath)) return;
    xenonPreference = !xenonPreference;
    option[0x2a] = 1;
    DrawXenonEffects(option);
}

void __fastcall DrawNativePassOption(std::uint8_t* option) {
    const bool spanish = GameValue<int>(0x4f41c0) == 4;
    const auto entry = std::find(std::begin(movieApis), std::end(movieApis), nativePassesRequested);
    const char* name = entry == std::end(movieApis) ? movieApiNames[0] : movieApiNames[entry - movieApis];
    char value[64]{};
    if (nativePassesRequested != nativePassesAtStartup) {
        std::snprintf(value, sizeof(value), spanish ? "%s al reiniciar" : "%s after restart", name);
    } else if (nativePassesRequested != 9 && (!nativePassesActive || lastFrameApi.load() == 9)) {
        std::snprintf(value, sizeof(value), spanish ? "%s (salida DX9)" : "%s (DX9 output)", name);
    } else {
        std::snprintf(value, sizeof(value), "%s", name);
    }
    SetOptionText(option, spanish ? "API de salida" : "Presentation API", value);
}

void __fastcall ChangeNativePassOption(std::uint8_t* option, void*, const char*, std::int32_t message) {
    constexpr std::int32_t left = -1860157282, right = -1248388111;
    if ((message != left && message != right) || !settingsPath[0]) return;
    const auto current = std::find(std::begin(movieApis), std::end(movieApis), nativePassesRequested);
    const size_t count = std::size(movieApis);
    const size_t next = (current - std::begin(movieApis) + (message == right ? 1 : count - 1)) % count;
    if (!WritePrivateProfileStringW(L"Graphics", L"NativePasses", movieApiValues[next], settingsPath)) return;
    nativePassesRequested = movieApis[next];
    option[0x2a] = 1;
    DrawNativePassOption(option);
}

struct WindowModeLabels { const char *title, *exclusive, *windowed, *borderless; };
WindowModeLabels WindowLabels(int language) {
    switch(language) {
    case 1: return {u8"Mode d’affichage",u8"Plein écran",u8"Fenêtré","Sans bordure"};
    case 2: return {"Anzeigemodus","Vollbild","Fenster","Randlos"};
    case 3: return {"Modalità schermo","Schermo intero","Finestra","Senza bordi"};
    case 4: return {"Modo de pantalla","Pantalla completa","Ventana","Sin bordes"};
    case 5: return {"Schermmodus","Volledig scherm","Venster","Randloos"};
    case 6: return {u8"Skärmläge",u8"Helskärm",u8"Fönster","Utan kant"};
    case 7: return {u8"Skærmtilstand",u8"Fuld skærm","Vindue","Uden kant"};
    case 12: return {u8"Tryb ekranu",u8"PeÅ‚ny ekran","Okno","Bez ramek"};
    case 13: return {u8"Näyttötila",u8"Koko näyttö","Ikkuna","Reunaton"};
    default: return {"Display mode","Fullscreen","Windowed","Borderless"};
    }
}

void __fastcall DrawWindowMode(std::uint8_t* option) {
    using Mode=unlimited_display::Mode;
    const auto text=WindowLabels(GameValue<int>(0x4f41c0));
    const char* value=requestedWindowMode==Mode::Windowed?text.windowed:
                      requestedWindowMode==Mode::Borderless?text.borderless:text.exclusive;
    SetOptionText(option,text.title,value);
}

void __fastcall ChangeWindowMode(std::uint8_t* option, void*, const char*, std::int32_t message) {
    constexpr std::int32_t left=-1860157282, right=-1248388111;
    if(message!=left && message!=right)return;
    using Mode=unlimited_display::Mode;
    constexpr Mode order[]{Mode::Exclusive,Mode::Windowed,Mode::Borderless};
    const auto current=std::find(std::begin(order),std::end(order),requestedWindowMode);
    const auto index=size_t(current-std::begin(order));
    if(SelectWindowMode(order[(index+(message==left?2:1))%std::size(order)])) {
        option[0x2a]=1;
        DrawWindowMode(option);
    }
}

#include "nfs_textures.hpp"
#include "nfs_video_options.hpp"

bool FitVideoCalibration(const char* package) {
    if (!package) return false;
    auto* group = FindHudObject(package, 0xad6b204f);
    if (!FindHudChild(group, 0x545570c6) || !FindHudChild(group, 0x7f30eb56) ||
        !FindHudChild(group, 0x135a0f20)) return false;
    auto* transform = HudField<void*>(group, 0x2c);
    if (!transform) return false;
    // The monitor guide and both EA logos share this parent. Fit the whole
    // group below the five rows, in the same 640 x 480 FE canvas as the list.
    constexpr float scale = 0.63f;
    HudField<float>(transform, 0x38) = HudField<float>(transform, 0x3c) = scale;
    HudField<float>(transform, 0x1c) = -36.0f;
    HudField<float>(transform, 0x20) = 82.0f;
    MarkHudTransformDirty(group);
    return true;
}

void __fastcall SetupVideoWithFrameLimit(std::uint8_t* screen, void*) {
    using SetupVideo = void (__thiscall*)(std::uint8_t*);
    GameFunction<SetupVideo>(0x1295f0)(screen);
    static std::array<void*, 15> apiVtable{}, modeVtable{}, fpsVtable{}, xenonVtable{};
    blurIntensityOption = nullptr;
    const auto page = screen[0x154];
    const bool advanced = page != 0;
    if (page == 2) {
        BuildVideoEffectsPage(screen);
    } else if(advanced) {
        if(settingsPath[0]) AddUnlimitedVideoOption(screen,apiVtable,&ChangeNativePassOption,&DrawNativePassOption);
    } else {
        if(settingsPath[0] && displayResetInstalled) AddUnlimitedVideoOption(screen,modeVtable,&ChangeWindowMode,&DrawWindowMode);
        AddUnlimitedVideoOption(screen,fpsVtable,&ChangeFrameLimit,&DrawFrameLimit);
        AddUnlimitedVideoOption(screen,xenonVtable,&ChangeXenonEffects,&DrawXenonEffects);
    }
    const char* package = HudField<const char*>(screen, 0x10);
    if (effectsPageInstalled && advanced)
        LabelVideoPageButton(package, page);
    if (!advanced) FitVideoCalibration(package);
    if (auto* calibration = package ? FindHudObject(package, 0xad6b204f) : nullptr) {
        constexpr std::uintptr_t hide = 0x114c70, show = 0x114cc0;
        GameFunction<void (__cdecl*)(void*)>(advanced ? hide : show)(calibration);
    }
}

bool InstallFrameLimitMenu() {
    constexpr std::uintptr_t setup = 0x145d95, initialize = 0x145d9e, nativeSetup = 0x1295f0;
    const auto replacement = reinterpret_cast<std::uintptr_t>(&SetupVideoWithFrameLimit);
    if (!RedirectCall(setup, nativeSetup, replacement)) return false;
    if (RedirectCall(initialize, 0x1a5ea0, reinterpret_cast<std::uintptr_t>(&SelectFirstVideoOption))) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(engine);
    RedirectCall(setup, replacement - base, base + nativeSetup);
    return false;
}

constexpr std::uintptr_t shadowSetting = 0x501830;
constexpr std::uintptr_t textureFiltering = 0x501818;
constexpr std::uintptr_t mipSkipCount = 0x582ae0;
constexpr std::uintptr_t worldLodSetting = 0x5017f4;
constexpr std::uintptr_t shadowWidth = 0x2c86b1;
constexpr std::uintptr_t shadowHeight = 0x2c86c1;
constexpr std::uintptr_t shadowSlopeBias = 0x501abc;
constexpr std::uintptr_t shadowPath = 0x582c08;
constexpr D3DFORMAT df16 = static_cast<D3DFORMAT>(MAKEFOURCC('D','F','1','6'));
constexpr D3DFORMAT intz = static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
D3DFORMAT shadowDepthFormat = D3DFMT_UNKNOWN;

struct ShadowFormat {
    int path;
    D3DFORMAT format;
};

ShadowFormat SelectShadowFormat(IDirect3D9* direct3d, UINT ordinal, int nativePath) {
    D3DDISPLAYMODE mode{};
    if (!direct3d || FAILED(direct3d->GetAdapterDisplayMode(ordinal, &mode))) return {0, D3DFMT_UNKNOWN};
    constexpr ShadowFormat hardwareCompare{1, D3DFMT_D24S8};
    constexpr ShadowFormat nativeDepthTexture{2, df16};
    constexpr ShadowFormat modernDepthTexture{2, intz};
    const std::array candidates = nativePath == 1
        ? std::array{hardwareCompare, modernDepthTexture, nativeDepthTexture}
        : std::array{nativeDepthTexture, modernDepthTexture, hardwareCompare};
    for (const auto& candidate : candidates) {
        if (SUCCEEDED(direct3d->CheckDeviceFormat(ordinal, D3DDEVTYPE_HAL, mode.Format,
                                                  D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, candidate.format)) &&
            SUCCEEDED(direct3d->CheckDepthStencilMatch(ordinal, D3DDEVTYPE_HAL, mode.Format,
                                                        D3DFMT_A8R8G8B8, candidate.format))) {
            return candidate;
        }
    }
    return {0, D3DFMT_UNKNOWN};
}

void __cdecl ConfigureShadowPath() {
    auto& nativePath = GameValue<int>(shadowPath);
    const auto choice = SelectShadowFormat(GameValue<IDirect3D9*>(0x582bd8),
                                            GameValue<UINT>(0x582c14), nativePath);
    if (choice.path != 0) {
        nativePath = choice.path;
        shadowDepthFormat = choice.format;
    }
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Shadow depth: path=%d format=0x%08lx\n", nativePath,
                     static_cast<unsigned long>(shadowDepthFormat));
    }
}

void __cdecl ConfigureDisplayCapabilities() {
    if (aspectHookInstalled) SetUpAspectVideoModes();
    else GameFunction<void (__cdecl*)()>(0x2c1510)();
    // The game has queried D3D caps and will next select the shader variant.
    if (shadowHookInstalled) ConfigureShadowPath();
    ApplyPostPreferences();
}

bool InstallDisplayCapabilityHook() {
    return RedirectCall(0x2e6a61, 0x2c1510, reinterpret_cast<std::uintptr_t>(&ConfigureDisplayCapabilities));
}

class ShadowTargets {
public:
    ~ShadowTargets() {
        if (depthSurface) depthSurface->Release();
        if (colorSurface) colorSurface->Release();
        if (depth) depth->Release();
        if (color) color->Release();
    }

    bool Create(IDirect3DDevice9* device, UINT size, D3DFORMAT format) {
        D3DSURFACE_DESC colorDesc{}, depthDesc{};
        return SUCCEEDED(device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                               D3DPOOL_DEFAULT, &color, nullptr)) &&
               SUCCEEDED(device->CreateTexture(size, size, 1, D3DUSAGE_DEPTHSTENCIL, format,
                                               D3DPOOL_DEFAULT, &depth, nullptr)) &&
               SUCCEEDED(color->GetSurfaceLevel(0, &colorSurface)) &&
               SUCCEEDED(depth->GetSurfaceLevel(0, &depthSurface)) &&
               SUCCEEDED(colorSurface->GetDesc(&colorDesc)) && SUCCEEDED(depthSurface->GetDesc(&depthDesc)) &&
               colorDesc.Width == size && colorDesc.Height == size &&
               depthDesc.Width == size && depthDesc.Height == size;
    }

    void Publish() {
        GameValue<IDirect3DTexture9*>(0x582cf0) = color;
        GameValue<IDirect3DTexture9*>(0x582cf8) = depth;
        GameValue<IDirect3DSurface9*>(0x582cec) = colorSurface;
        GameValue<IDirect3DSurface9*>(0x582cf4) = depthSurface;
        color = depth = nullptr;
        colorSurface = depthSurface = nullptr;
    }

private:
    IDirect3DTexture9* color = nullptr;
    IDirect3DTexture9* depth = nullptr;
    IDirect3DSurface9* colorSurface = nullptr;
    IDirect3DSurface9* depthSurface = nullptr;
};

HRESULT __cdecl CreateShadowTargets() {
    auto* device = GameValue<IDirect3DDevice9*>(0x582bdc);
    const int path = GameValue<int>(shadowPath);
    const int quality = GameValue<int>(shadowSetting);
    lastShadowSetting = quality;
    if (quality <= 0) return S_OK;

    D3DCAPS9 caps{};
    const UINT requested = quality >= 2 ? 2048u : 1024u;
    const UINT supported = device && SUCCEEDED(device->GetDeviceCaps(&caps))
                               ? std::min(caps.MaxTextureWidth, caps.MaxTextureHeight) : 1024u;
    const auto format = shadowDepthFormat != D3DFMT_UNKNOWN ? shadowDepthFormat
                        : path == 1 ? D3DFMT_D24S8
                        : path == 2 ? df16 : D3DFMT_UNKNOWN;
    static const float originalSlope = GameValue<float>(shadowSlopeBias);
    for (const UINT size : {2048u, 1024u, 512u}) {
        if (!device || format == D3DFMT_UNKNOWN || size > std::min(supported, requested)) continue;
        ShadowTargets targets;
        if (!targets.Create(device, size, format)) continue;
        const UINT oldSize = GameValue<UINT>(shadowWidth);
        if (!WriteGameBytes(shadowWidth, &size, sizeof(size))) break;
        if (!WriteGameBytes(shadowHeight, &size, sizeof(size))) {
            WriteGameBytes(shadowWidth, &oldSize, sizeof(oldSize));
            break;
        }
        GameValue<float>(shadowSlopeBias) = originalSlope * (static_cast<float>(size) / 1024.0f);
        targets.Publish();
        if (LogFile log; log.file) {
            std::fprintf(log.file, "Shadow map: game_quality=%d requested=%u allocated=%ux%u\n",
                         quality, requested, size, size);
        }
        return S_OK;
    }
    // The original caller continues rendering even when allocation fails.
    // Suppress the passes until the user can re-enable them after recovery.
    if (!device || SUCCEEDED(device->TestCooperativeLevel())) {
        GameValue<int>(shadowSetting) = 0;
        lastShadowSetting = 0;
    }
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Shadow map: requested=%u, allocation failed (path=%d, format=0x%08lx)\n",
                     requested, path, static_cast<unsigned long>(format));
    }
    return E_FAIL;
}

bool InstallShadowAllocation() {
    constexpr std::uint8_t oldCall[] = {0xe8, 0x6f, 0xaf, 0, 0};
    constexpr std::uint8_t originalGetter[] = {0xb8, 0, 4, 0, 0, 0xc3};
    constexpr std::uintptr_t callOffset = 0x2bd7fc;
    if (!MatchGameBytes(callOffset, oldCall, sizeof(oldCall)) ||
        !MatchGameBytes(shadowWidth - 1, originalGetter, sizeof(originalGetter)) ||
        !MatchGameBytes(shadowHeight - 1, originalGetter, sizeof(originalGetter))) return false;
    return RedirectCall(callOffset, 0x2c8770, reinterpret_cast<std::uintptr_t>(&CreateShadowTargets));
}

bool InstallShadowContinuity() {
    constexpr std::uintptr_t opacityAtCover = 0x2de377;
    constexpr std::uint8_t originalFade[] = {0x75,0x3b,0xc7,0x05,0xa8,0x2b,0x98,0,0,0,0x80,0x3f};
    constexpr std::uint8_t keepCastShadows[] = {0x90,0x90,0xc7,0x05,0xa8,0x2b,0x98,0,0,0,0,0};
    constexpr std::uint8_t originalCull[] = {0xd8,0x0d,0xc4,0x10,0x89,0,0xd9,0x5c,0x24,0x38};
    constexpr std::uintptr_t cullOffset = 0x2c9653;
    static const float casterMargin = 10.0f;
    const float* margin = &casterMargin;
    if (!MatchGameBytes(opacityAtCover, originalFade, sizeof(originalFade)) ||
        !MatchGameBytes(cullOffset, originalCull, sizeof(originalCull))) return false;
    if (!WriteGameBytes(opacityAtCover, keepCastShadows, sizeof(keepCastShadows))) return false;
    if (WriteGameBytes(cullOffset + 2, &margin, sizeof(margin))) return true;
    WriteGameBytes(opacityAtCover, originalFade, sizeof(originalFade));
    return false;
}

bool InstallTextureFiltering() {
    // Reuse the game's anisotropic path for both enabled settings. The
    // built-in capability checks and maximum sampler value still apply.
    constexpr std::uintptr_t filters[] = {
        0x2c1485, 0x2c14c5, 0x2c5eee, 0x2c5f36, 0x2cfd9b, 0x2cfdde
    };
    constexpr std::uintptr_t anisotropyEffects = 0x2c5fba;
    constexpr std::uintptr_t anisotropyDirect = 0x2cfe3e;
    const auto matchesFilter = [](std::uintptr_t offset) {
        constexpr std::uint8_t compareLow[] = {0x83,0xf8,1,0x7e};
        constexpr std::uint8_t compareHigh[] = {0x83,0xf8,2,0x75};
        return MatchGameBytes(offset, compareLow, sizeof(compareLow)) &&
               MatchGameBytes(offset + 5, compareHigh, sizeof(compareHigh));
    };
    constexpr std::uint8_t anisoCheck[] = {0x83,0x3d,0x18,0x18,0x90,0,2};
    if (!std::all_of(std::begin(filters), std::end(filters), matchesFilter) ||
        !MatchGameBytes(anisotropyEffects, anisoCheck, sizeof(anisoCheck)) ||
        !MatchGameBytes(anisotropyDirect, anisoCheck, sizeof(anisoCheck)) ||
        GameValue<std::uint8_t>(anisotropyEffects + 12) != 0x75 ||
        GameValue<std::uint8_t>(anisotropyDirect + 14) != 0x75) return false;
    const std::uint8_t zero = 0, greater = 0x7f, nonPositive = 0x7e;
    for (const auto offset : filters) {
        if (!WriteGameBytes(offset + 2, &zero, 1) || !WriteGameBytes(offset + 8, &greater, 1)) return false;
    }
    return WriteGameBytes(anisotropyEffects + 6, &zero, 1) &&
           WriteGameBytes(anisotropyDirect + 6, &zero, 1) &&
           WriteGameBytes(anisotropyEffects + 12, &nonPositive, 1) &&
           WriteGameBytes(anisotropyDirect + 14, &nonPositive, 1);
}

enum class TextureUpload { Native, Uploaded, Failed };

TextureUpload UploadFullSizeTexture(IDirect3DTexture9* texture, const std::uint8_t* info,
                                    const std::uint8_t* source) {
    // The PC loader's TextureInfo stores two 16-bit dimensions, the
    // byte sizes of the base image and all mips, and the pixel pointer.
    const auto width = *reinterpret_cast<const std::uint16_t*>(info + 0x44);
    const auto height = *reinterpret_cast<const std::uint16_t*>(info + 0x46);
    const auto packedBytes = *reinterpret_cast<const std::uint32_t*>(info + 0x38);
    const auto baseBytes = *reinterpret_cast<const std::uint32_t*>(info + 0x40);
    const auto* original = *reinterpret_cast<const std::uint8_t* const*>(info + 0x70);
    const unsigned mipCount = info[0x4e];
    if (std::max(width, height) < 1024 || !texture || !source || original != source ||
        !width || !height || !mipCount || mipCount > 16 || texture->GetLevelCount() != mipCount) {
        return TextureUpload::Native;
    }

    D3DSURFACE_DESC top{};
    if (FAILED(texture->GetLevelDesc(0, &top)) || top.Width != width || top.Height != height) {
        return TextureUpload::Native;
    }
    unsigned texelsPerBlock = 4;
    unsigned blockBytes = 16;
    switch (top.Format) {
        case D3DFMT_DXT1: blockBytes = 8; break;
        case D3DFMT_DXT2:
        case D3DFMT_DXT3:
        case D3DFMT_DXT4:
        case D3DFMT_DXT5: break;
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8: blockBytes = 4; texelsPerBlock = 1; break;
        default: return TextureUpload::Native;
    }

    struct MipRows { unsigned bytes, count; };
    std::array<MipRows, 16> rows{};
    std::size_t totalBytes = 0;
    for (unsigned mip = 0; mip < mipCount; ++mip) {
        const unsigned mipWidth = std::max(1u, static_cast<unsigned>(width) >> mip);
        const unsigned mipHeight = std::max(1u, static_cast<unsigned>(height) >> mip);
        rows[mip] = {((mipWidth + texelsPerBlock - 1) / texelsPerBlock) * blockBytes,
                     (mipHeight + texelsPerBlock - 1) / texelsPerBlock};
        totalBytes += static_cast<std::size_t>(rows[mip].bytes) * rows[mip].count;
    }
    // Leave palette-converted, reduced-detail, and nonstandard packs on
    // their existing loader. DXT mip tails have at least one full block.
    if (totalBytes != packedBytes || rows[0].bytes * rows[0].count != baseBytes) return TextureUpload::Native;

    for (unsigned mip = 0; mip < mipCount; ++mip) {
        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(mip, &locked, nullptr, 0))) return TextureUpload::Failed;
        if (!locked.pBits || locked.Pitch < static_cast<int>(rows[mip].bytes)) {
            texture->UnlockRect(mip);
            return TextureUpload::Failed;
        }
        auto* output = static_cast<std::uint8_t*>(locked.pBits);
        for (unsigned row = 0; row < rows[mip].count; ++row) {
            std::memcpy(output + static_cast<std::size_t>(row) * locked.Pitch, source, rows[mip].bytes);
            source += rows[mip].bytes;
        }
        if (FAILED(texture->UnlockRect(mip))) return TextureUpload::Failed;
    }
    return TextureUpload::Uploaded;
}

extern "C" __attribute__((used)) int __cdecl NfsmwUploadTextureMipChain(
        IDirect3DTexture9** destination, const std::uint8_t* info, const std::uint8_t* source) {
    const auto hash = *reinterpret_cast<const std::uint32_t*>(info + 0x24);
    if (unlimited_textures::Upload(GameValue<IDirect3DDevice9*>(0x582bdc), destination, hash)) return TRUE;
    const auto result = UploadFullSizeTexture(*destination, info, source);
    constexpr std::uint32_t uiSamples[] = {0xe0463afa, 0x545570c6, 0x1a6d3d2c, 0x03c174cf, 0x20865c0a, 0x839e7d77};
    static volatile LONG reported[std::size(uiSamples)]{};
    const auto sample = std::find(std::begin(uiSamples), std::end(uiSamples), hash);
    if (sample != std::end(uiSamples) && result != TextureUpload::Failed && *destination && logPath[0] &&
        InterlockedCompareExchange(&reported[sample - uiSamples], 1, 0) == 0) {
        D3DSURFACE_DESC desc{};
        if (SUCCEEDED((*destination)->GetLevelDesc(0, &desc))) {
            if (LogFile log; log.file)
                std::fprintf(log.file, "UI texture %.24s: GPU=%ux%u levels=%lu\n",
                             reinterpret_cast<const char*>(info + 0xc), desc.Width, desc.Height,
                             (*destination)->GetLevelCount());
        }
    }
    if (result == TextureUpload::Failed) {
        // The caller links the texture before upload. On failure unwind
        // that insertion so a later load can attach the node again.
        struct TextureLink { TextureLink* next; TextureLink* previous; };
        auto* link = reinterpret_cast<TextureLink*>(reinterpret_cast<std::uint8_t*>(destination) - 0x18);
        link->next->previous = link->previous;
        link->previous->next = link->next;
        (*destination)->Release();
        *destination = nullptr;
    }
    return result != TextureUpload::Native;
}

__attribute__((naked)) void UploadTextureMipChain() {
    asm(
        "xorl %ebx, %ebx\n"
        "testl %ecx, %ecx\n"
        "jg 1f\n"
        "addl $0x72, (%esp)\n"
        "ret\n"
        "1:\n"
        "pushfl\n"
        "pushal\n"
        "pushl %edi\n"
        "pushl %esi\n"
        "pushl 0x44(%esp)\n"
        "call _NfsmwUploadTextureMipChain\n"
        "addl $12, %esp\n"
        "testl %eax, %eax\n"
        "jz 2f\n"
        "addl $0x72, 36(%esp)\n"
        "2:\n"
        "popal\n"
        "popfl\n"
        "ret\n");
}

bool InstallFullSizeTextureUpload() {
    constexpr std::uint8_t nativeLoop[] = {0x33,0xdb,0x85,0xc9,0x7e,0x71};
    return InstallInlineCall(0x2ddfaa, nativeLoop, &UploadTextureMipChain);
}

bool InstallReflectionRefresh() {
    constexpr std::uint32_t nativePeriods[] = {6, 4, 1};
    constexpr std::array<std::uint32_t, 6> firstLow = {1, 0, 0, 0, 0, 0};
    constexpr std::array<std::uint32_t, 6> firstMedium = {1, 1, 0, 0, 1, 0};
    constexpr std::array<std::uint32_t, 6> allFaces = {1, 1, 1, 1, 1, 1};
    constexpr std::uintptr_t periods = 0x4f901c;
    constexpr std::uintptr_t low = 0x4f9028;
    constexpr std::uintptr_t medium = low + 6 * sizeof(firstLow);
    if (!MatchGameBytes(periods, nativePeriods, sizeof(nativePeriods)) ||
        !MatchGameBytes(low, firstLow.data(), sizeof(firstLow)) ||
        !MatchGameBytes(medium, firstMedium.data(), sizeof(firstMedium))) return false;
    std::memcpy(&GameValue<std::uint32_t>(low), allFaces.data(), sizeof(allFaces));
    std::memcpy(&GameValue<std::uint32_t>(medium), allFaces.data(), sizeof(allFaces));
    GameValue<std::uint32_t>(periods) = 1;
    GameValue<std::uint32_t>(periods + 4) = 1;
    return true;
}

void SelectAdapter(IDirect3D9* d3d, UINT ordinal) {
    D3DADAPTER_IDENTIFIER9 dx9{};
    if (FAILED(d3d->GetAdapterIdentifier(ordinal, 0, &dx9))) return;
    if (LogFile log; log.file) {
        std::fprintf(log.file, "D3D9 adapter=%s vendor=%04lX device=%04lX output=%s\n",
                     dx9.Description, dx9.VendorId, dx9.DeviceId, dx9.DeviceName);
    }
    HMODULE dxgi = LoadSystemLibrary(L"dxgi.dll");
    if (!dxgi) return;
    using CreateFactory = HRESULT (WINAPI*)(REFIID, void**);
    auto create = reinterpret_cast<CreateFactory>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    IDXGIFactory1* factory = nullptr;
    if (!create || FAILED(create(IID_PPV_ARGS(&factory)))) return;
    IDXGIAdapter1* match = nullptr;
    bool ambiguous = false;
    LUID luid{};
    IDirect3D9Ex* d3dEx = nullptr;
    bool hasLuid = SUCCEEDED(d3d->QueryInterface(IID_PPV_ARGS(&d3dEx)));
    if (hasLuid) {
        hasLuid = SUCCEEDED(d3dEx->GetAdapterLUID(ordinal, &luid));
        d3dEx->Release();
    }
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* candidate = nullptr;
        if (factory->EnumAdapters1(index, &candidate) != S_OK) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(candidate->GetDesc1(&desc)) &&
            (hasLuid ? (luid.LowPart == desc.AdapterLuid.LowPart && luid.HighPart == desc.AdapterLuid.HighPart)
                     : (desc.VendorId == dx9.VendorId && desc.DeviceId == dx9.DeviceId))) {
            if (!match) {
                match = candidate;
                continue;
            }
            ambiguous = true;
        }
        candidate->Release();
    }
    if (match) {
        if (!ambiguous) match->QueryInterface(IID_PPV_ARGS(&adapter));
        match->Release();
    }
    factory->Release();
}

void ReportTextureBudget(FILE* file) {
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if (adapter && SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory))) {
        std::fprintf(file, " local_usage=%.1fMiB local_budget=%.1fMiB",
                     double(memory.CurrentUsage) / MiB, double(memory.Budget) / MiB);
    }
}

void ReportTrackPool(FILE* file) {
    if (!engine) return;
    const auto* streamer = reinterpret_cast<const std::uint8_t*>(engine) + trackStreamerRva;
    const auto* pool = *reinterpret_cast<const std::uint32_t* const*>(streamer + 0x844);
    if (!pool || pool[0] != 7) return;
    std::fprintf(file, " track_capacity=%.1fMiB track_free=%.1fMiB",
                 double(pool[2]) / MiB, double(pool[6]) / MiB);
}

UINT WINAPI ProbeAvailableMemory(IDirect3DDevice9* device) {
    const UINT bytes = nativeAvailableMemory(device);
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Game texture-memory query: native_D3D9=%u bytes", bytes);
        ReportTextureBudget(log.file);
        std::fputc('\n', log.file);
    }
    return bytes;
}

std::uint32_t GameFlowState() {
    return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(engine) + gameFlowStateRva);
}

void PaceFrame() {
    const unsigned frameLimit = EffectiveFrameLimit();
    static unsigned previousLimit = 0;
    static LONGLONG next = 0;
    if (previousLimit != frameLimit) {
        next = 0;
        previousLimit = frameLimit;
    }
    if (!frameLimit || frequency.QuadPart <= 0) return;
    constexpr DWORD highResolution = 0x2;
    static const HANDLE timer = [] {
        using CreateTimer = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
        const auto create = reinterpret_cast<CreateTimer>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                         "CreateWaitableTimerExW"));
        const auto precise = create ? create(nullptr, nullptr, highResolution, TIMER_MODIFY_STATE | SYNCHRONIZE) : nullptr;
        return precise ? precise : LowThreadBudget() ? CreateWaitableTimerW(nullptr, FALSE, nullptr) : nullptr;
    }();
    const LONGLONG interval = (frequency.QuadPart + frameLimit / 2) / frameLimit;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    while (now.QuadPart < next) {
        const LONGLONG remaining = next - now.QuadPart;
        const LONGLONG microseconds = remaining * 1000000 / frequency.QuadPart;
        const LONGLONG waitMarginMicroseconds = LowThreadBudget() ? 0 : 400;
        if (microseconds > waitMarginMicroseconds + 60 && timer) {
            LARGE_INTEGER due{};
            due.QuadPart = -(microseconds - waitMarginMicroseconds) * 10;
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
            } else if (microseconds > 4000) {
                Sleep(1);
            }
        } else if (microseconds > 4000) {
            Sleep(1);
        } else if (LowThreadBudget()) {
            SwitchToThread();
        } else {
            YieldProcessor();
        }
        QueryPerformanceCounter(&now);
    }
    // Preserve the steady cadence after small scheduling delays. Restart the
    // schedule after longer stalls instead of presenting a burst to catch up.
    if (now.QuadPart - next > interval / 11) next = now.QuadPart;
    next += interval;
}

D3DPRESENT_PARAMETERS SceneTargetParameters(const D3DPRESENT_PARAMETERS& requested) {
    auto target = requested;
    target.Windowed = TRUE;
    target.FullScreen_RefreshRateInHz = 0;
    target.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    return target;
}

D3DPRESENT_PARAMETERS PrepareDisplay(HWND window,const D3DPRESENT_PARAMETERS& requested) {
    if (!displayResetInstalled) return requested;
    GameValue<BOOL>(0x582bf0)=requestedWindowMode!=unlimited_display::Mode::Exclusive;
    auto intent=unlimited_display::ForMode(requested,requestedWindowMode);
    if (!window) return intent;
    if (!displayHost.Apply(window,requestedWindowMode,intent.BackBufferWidth,intent.BackBufferHeight)) return intent;
    GetWindowRect(window,&GameValue<RECT>(0x53daa4));
    GetClientRect(window,&GameValue<RECT>(0x53cad0));
    if (requestedWindowMode==unlimited_display::Mode::Borderless) {
        RECT bounds{};
        if (GetClientRect(window,&bounds) && bounds.right>0 && bounds.bottom>0) {
            // Keep the chosen resolution as the pixel budget. The game's
            // scene, HUD and client must agree on the physical aspect ratio.
            const double scale=std::min(double(intent.BackBufferWidth)/bounds.right,
                                        double(intent.BackBufferHeight)/bounds.bottom);
            if (scale>0) {
                intent.BackBufferWidth=std::max(1l,std::lround(bounds.right*scale));
                intent.BackBufferHeight=std::max(1l,std::lround(bounds.bottom*scale));
            }
        }
    }
    return intent;
}

void RecordDisplayMode(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& caller,const D3DPRESENT_PARAMETERS& intent) {
    caller=intent;
    // Reset can zero dimensions, count and format in its in/out argument.
    // Read the allocated surface and carry the player's mode over both stock resets.
    movie_pass::Com<IDirect3DSwapChain9> chain;
    movie_pass::Com<IDirect3DSurface9> back;
    D3DSURFACE_DESC desc{};
    D3DPRESENT_PARAMETERS actual{};
    if (SUCCEEDED(device->GetSwapChain(0,chain.Put())) && SUCCEEDED(chain->GetPresentParameters(&actual)))
        caller.BackBufferCount=actual.BackBufferCount;
    if (SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.Put())) && SUCCEEDED(back->GetDesc(&desc))) {
        caller.BackBufferWidth=desc.Width;
        caller.BackBufferHeight=desc.Height;
        caller.BackBufferFormat=desc.Format;
    }
    requestedDisplay = caller;
    GameValue<UINT>(0x582be4)=caller.BackBufferWidth;
    GameValue<UINT>(0x582be8)=caller.BackBufferHeight;
    UpdateAspect(caller.BackBufferWidth, caller.BackBufferHeight);
}

HRESULT WINAPI ResetDisplay(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters) {
    // Keep the runtime/overlay Reset chain intact when called from the four
    // settings and device-loss paths in the retail engine.
    const auto reset = reinterpret_cast<DeviceReset>((*reinterpret_cast<void***>(device))[16]);
    if (device != trackedDevice || !parameters) return reset(device,parameters);
    if (displayHost.Applying()) return D3DERR_DEVICELOST;
    if (nativePassesActive && !restoreOriginalOnReset)
        nativeFrames.BeforeSceneReset(unlimited_display::ForMode(*parameters,requestedWindowMode));
    else
        nativeFrames.Reset();
    // Movie output borrows the D3D9 destination only for a single draw. Its
    // GPU device and private textures can survive a scene swapchain reset.
    nativeMoviePass.Invalidate();
    nativeShadowPass.Reset();
    const HWND window=parameters->hDeviceWindow ? parameters->hDeviceWindow : gameFocusWindow;
    const auto intent=PrepareDisplay(window,*parameters);
    if (nativePassesActive && !restoreOriginalOnReset) {
        auto target = SceneTargetParameters(intent);
        const auto result = reset(device,&target);
        if (LogFile log; log.file) std::fprintf(log.file,
            "Frame reset: selected=%u source=windowed requested=%ux%u msaa=%u quality=%lu result=%08lx\n",
            nativePassesAtStartup,parameters->BackBufferWidth,parameters->BackBufferHeight,
            static_cast<unsigned>(target.MultiSampleType),target.MultiSampleQuality,result);
        if (SUCCEEDED(result)) {
            originalPresentationOnly = false;
            RecordDisplayMode(device,*parameters,intent);
        }
        // The engine retries with reduced multisampling or a supported
        // resolution. Giving DX9 exclusive ownership here defeats that retry.
        return result;
    }
    auto target=intent;
    const auto result = reset(device,&target);
    if (SUCCEEDED(result)) {
        originalPresentationOnly = true;
        RecordDisplayMode(device,*parameters,intent);
    }
    return result;
}

HRESULT WINAPI ResetGameDisplay(IDirect3DDevice9* device) {
    return ResetDisplay(device,&GameValue<D3DPRESENT_PARAMETERS>(0x53e82c));
}

void __cdecl ShutdownGameDisplay() {
    unlimited_textures::ClearDemand();
    nativeFrames.Reset();
    nativeMoviePass.Reset();
    nativeShadowPass.Reset();
    trackedDevice=nullptr;
    nativePassesActive=false;
    if (adapter) { adapter->Release(); adapter=nullptr; }
    if (LogFile log; log.file) std::fputs("Frame presentation: released before game window and device shutdown\n",log.file);
    GameFunction<void(__cdecl*)()>(0x2dfa80)();
}

bool InstallGameDisplayReset() {
    // The engine calls Reset twice in succession, then has two retry sites
    // for multisampling and resolution changes. Some plug-ins replace the
    // device vtable after startup, so intercept these calls at their owner.
    constexpr struct { std::uintptr_t offset; std::uint8_t method; } sites[] = {
        {0x2db164,0x51},{0x2db174,0x52},{0x2db1d7,0x51},{0x2db23c,0x51}
    };
    std::uint8_t expected[] = {0x68,0x2c,0xe8,0x93,0x00,0x50,0xff,0x51,0x40};
    for (const auto& site : sites) {
        expected[7] = site.method;
        if (!MatchGameBytes(site.offset,expected,sizeof(expected))) return false;
    }
    constexpr std::uintptr_t releaseSite=0x266551;
    constexpr std::uint8_t stockRelease[]={0xe8,0x2a,0x95,0x07,0x00};
    if (!RedirectCall(releaseSite,0x2dfa80,reinterpret_cast<std::uintptr_t>(&ShutdownGameDisplay))) return false;
    for (std::size_t i=0; i<std::size(sites); ++i) {
        const auto site = sites[i].offset;
        std::uint8_t call[] = {0x50,0xe8,0,0,0,0,0x90,0x90,0x90};
        const auto delta = reinterpret_cast<std::uintptr_t>(&ResetGameDisplay) -
                           (reinterpret_cast<std::uintptr_t>(engine)+site+6);
        std::memcpy(call+2,&delta,sizeof(delta));
        if (WriteGameBytes(site,call,sizeof(call))) continue;
        while (i) {
            --i;
            expected[7] = sites[i].method;
            WriteGameBytes(sites[i].offset,expected,sizeof(expected));
        }
        WriteGameBytes(releaseSite,stockRelease,sizeof(stockRelease));
        return false;
    }
    return true;
}

HRESULT WINAPI GamePresent(IDirect3DDevice9* device) {
    UnlimitedRumble::Service();
    PaceFrame();
    LARGE_INTEGER begin{}, end{};
    if (frameStatistics) QueryPerformanceCounter(&begin);
    HRESULT result = D3D_OK;
    const bool presented = nativePassesActive && !originalPresentationOnly &&
        nativeFrames.TryPresent(device,nativePassesAtStartup,result,&requestedDisplay);
    const auto previousApi = lastFrameApi.exchange(presented ? nativePassesAtStartup : 9);
    if (!presented) {
        const char* reason = nativeFrames.LastErrorText();
        const HWND gameWindow = requestedDisplay.hDeviceWindow ? requestedDisplay.hDeviceWindow : gameFocusWindow;
        if (nativePassesActive && !originalPresentationOnly && reason && *reason &&
            device->TestCooperativeLevel() == D3D_OK && !IsIconic(gameWindow)) {
            static char reported[192]{};
            if (previousApi != 9 || std::strcmp(reported,reason)) {
                std::snprintf(reported,sizeof(reported),"%s",reason);
                if (LogFile log; log.file)
                    std::fprintf(log.file,"Frame presentation: selected=%u fallback=D3D9 reason=%s\n",nativePassesAtStartup,reason);
            }
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            if (!requestedDisplay.Windowed && foregroundPid == GetCurrentProcessId() &&
                (nativeFrames.NeedsPrivateSceneReset() || nativeFrames.ShouldRestoreOriginalMode())) {
                // Let the engine release the default-pool targets before Reset.
                restoreOriginalOnReset = nativeFrames.ShouldRestoreOriginalMode() && !nativeFrames.NeedsPrivateSceneReset();
                GameValue<std::uint8_t>(0x582c39) = 1;
            }
        }
        result = device->Present(nullptr, nullptr, nullptr, nullptr);
    } else if (previousApi != nativePassesAtStartup) {
        if (LogFile log; log.file)
            std::fprintf(log.file,"Frame presentation: active_api=%u fullscreen=%u mode=%s\n",nativePassesAtStartup,!requestedDisplay.Windowed,nativeFrames.PresentationMode());
    }
    if (SUCCEEDED(result)) unlimited_textures::ServiceDemand(device);
    if (frameStatistics) QueryPerformanceCounter(&end);
    if (FAILED(result)) {
        if (nativePassesActive && result == D3DERR_DEVICELOST) {
            nativeMoviePass.Reset();
            nativeShadowPass.Reset();
        }
        return result;
    }
    if (frequency.QuadPart <= 0) return result;
    const int quality = GameValue<int>(shadowSetting);
    const int reflection = GameValue<int>(0x5017b8);
    if (windowModePending && !displayHost.Applying()) {
        windowModePending=false;
        restoreOriginalOnReset=false;
        GameValue<std::uint8_t>(0x582c39)=1;
    }
    if ((shadowHookInstalled && lastShadowSetting != -1 && quality != lastShadowSetting) ||
        (reflectionQualityInstalled && lastReflectionQuality != -1 && reflection != lastReflectionQuality)) {
        // Use the game's normal reset, which releases and rebuilds the
        // shadow targets and viewport together when options are applied.
        GameValue<std::uint8_t>(0x582c39) = 1;
    }
    lastShadowSetting = quality;
    lastReflectionQuality = reflection;
    if (!frameStatistics) return result;
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    if (foreground != GetCurrentProcessId()) return result;
    static std::array<double, 4096> frames{};
    static LONGLONG first = 0, previous = 0;
    static std::size_t count = 0;
    static std::uint32_t state = 0;
    static double sum = 0, presentSum = 0;
    static std::uint32_t firstTick = 0, previousTick = 0, firstGameClock = 0;
    static std::size_t repeatedTicks = 0;
    const auto currentState = GameFlowState();
    const auto tick = GameValue<std::uint32_t>(simulationTickRva);
    const auto gameClock = GameValue<std::uint32_t>(gameClockRva);
    if (currentState != state || (previous && begin.QuadPart - previous > frequency.QuadPart)) {
        state = currentState;
        count = 0;
        sum = presentSum = 0;
        first = previous = 0;
    }
    if (!first) {
        first = begin.QuadPart;
        firstTick = tick;
        firstGameClock = gameClock;
        repeatedTicks = 0;
    }
    if (previous && count < frames.size()) {
        const double frameTime = 1000.0 * (begin.QuadPart - previous) / frequency.QuadPart;
        frames[count++] = frameTime;
        sum += frameTime;
        presentSum += 1000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart;
        if (tick == previousTick) ++repeatedTicks;
    }
    previous = begin.QuadPart;
    previousTick = tick;
    if (count && (begin.QuadPart - first >= 4 * frequency.QuadPart || count == frames.size())) {
        std::sort(frames.begin(), frames.begin() + count);
        if (LogFile log; log.file) {
            std::fprintf(log.file,
                         "pace=%u state=%u samples=%zu fps=%.1f p05=%.2fms p50=%.2fms p95=%.2fms p99=%.2fms avg_Present=%.2fms",
                         EffectiveFrameLimit(), state, count, 1000.0 * count / sum, frames[(count-1) * 5 / 100],
                         frames[(count-1) / 2], frames[(count-1) * 95 / 100], frames[(count-1) * 99 / 100], presentSum / count);
            ReportTextureBudget(log.file);
            ReportTrackPool(log.file);
            if (nativePassesActive)
                std::fprintf(log.file, " present_api=%u native_frames=%llu movie_requested=%u last_movie_api=%u native_movie_draws=%llu native_movie_uploads=%llu",
                             lastFrameApi.load(),nativeFrames.Count(),nativePassesAtStartup,lastMovieApi.load(),nativeMoviePass.DrawCount(),nativeMoviePass.UploadCount());
            if (nativePassesActive && nativePassesAtStartup == 12)
                std::fprintf(log.file, " native_shadow_batches=%llu meshes=%llu legacy_shadow_batches=%llu",
                             shadowBatchCount.load(), shadowMeshCount.load(), shadowFallbackCount.load());
            const double seconds = double(begin.QuadPart - first) / frequency.QuadPart;
            if (state == 6) {
                std::fprintf(log.file, " simulation=%.1fHz no_sim_update=%zu/%zu clock_speed=%.3f",
                             static_cast<std::uint32_t>(tick - firstTick) / seconds, repeatedTicks, count,
                             static_cast<std::uint32_t>(gameClock - firstGameClock) / (4000.0 * seconds));
                DWORD anisotropy = 0;
                device->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &anisotropy);
                std::fprintf(log.file, " world_lod=%i skipped_top_mips=%i filtering=%i sampler_aniso=%lu shadow_quality=%i",
                             GameValue<int>(worldLodSetting), GameValue<int>(mipSkipCount),
                             GameValue<int>(textureFiltering), anisotropy, quality);
            }
            std::fputc('\n', log.file);
        }
        count = 0;
        sum = presentSum = 0;
        first = begin.QuadPart;
        firstTick = tick;
        firstGameClock = gameClock;
        repeatedTicks = 0;
    }
    return result;
}

bool InstallPresentCall() {
    auto* callSite = reinterpret_cast<std::uint8_t*>(engine) + 0x2e75e5;
    constexpr std::uint8_t expected[] = {0x57, 0x57, 0x57, 0x57, 0x50, 0xff, 0x51, 0x44};
    if (std::memcmp(callSite, expected, sizeof(expected)) != 0) return false;
    std::uint8_t replacement[] = {0x50, 0xe8, 0, 0, 0, 0, 0x90, 0x90};
    const auto offset = reinterpret_cast<std::uintptr_t>(&GamePresent) -
                        reinterpret_cast<std::uintptr_t>(callSite + 6);
    static_assert(sizeof(offset) == 4);
    std::memcpy(replacement + 2, &offset, sizeof(offset));
    return WriteGameBytes(0x2e75e5, replacement, sizeof(replacement));
}

HRESULT WINAPI ProbeDevice(IDirect3D9* d3d, UINT ordinal, D3DDEVTYPE type, HWND window, DWORD behavior,
                          D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device) {
    const bool gameTarget = !trackedDevice || device == &GameValue<IDirect3DDevice9*>(0x582bdc);
    const bool privateRequested = nativePassFactorySelected && d3d == trackedFactory && parameters && device && gameTarget;
    if (privateRequested && trackedDevice) {
        nativeFrames.Reset();
        nativeMoviePass.Reset();
        nativeShadowPass.Reset();
    }
    const HWND host = parameters && parameters->hDeviceWindow ? parameters->hDeviceWindow : window;
    const auto requested = parameters && gameTarget ? PrepareDisplay(host,*parameters) :
                           (parameters ? *parameters : D3DPRESENT_PARAMETERS{});
    auto publicTarget=requested;
    auto sceneTarget = SceneTargetParameters(requested);
    HRESULT result = nativeCreateDevice(d3d, ordinal, type, window, behavior, parameters ? (privateRequested ? &sceneTarget : &publicTarget) : nullptr, device);
    if (privateRequested && SUCCEEDED(result)) {
        originalPresentationOnly = false;
        restoreOriginalOnReset = false;
    } else if (privateRequested) {
        originalPresentationOnly = true;
        *parameters = requested;
        result = nativeCreateDevice(d3d,ordinal,type,window,behavior,parameters,device);
    }
    movie_pass::Com<IDirect3D9> fallback;
    if (FAILED(result) && nativePassFactorySelected && d3d == trackedFactory && parameters && device) {
        // Match the same graphics output. Windows can enumerate hybrid adapters in
        // a different order on the two factories.
        *fallback.Put() = SystemFactory(D3D_SDK_VERSION);
        D3DADAPTER_IDENTIFIER9 source{};
        if (fallback.Get() && SUCCEEDED(d3d->GetAdapterIdentifier(ordinal, 0, &source))) {
            for (UINT i = 0; i < fallback->GetAdapterCount(); ++i) {
                D3DADAPTER_IDENTIFIER9 target{};
                if (FAILED(fallback->GetAdapterIdentifier(i, 0, &target)) || source.VendorId != target.VendorId ||
                    source.DeviceId != target.DeviceId || _stricmp(source.DeviceName, target.DeviceName)) continue;
                auto method = reinterpret_cast<CreateDevice>((*reinterpret_cast<void***>(fallback.Get()))[16]);
                if (method == &ProbeDevice) method = nativeCreateDevice;
                *parameters = requested;
                const HRESULT recovered = method(fallback.Get(), i, type, window, behavior, parameters, device);
                if (SUCCEEDED(recovered)) {
                    result = recovered;
                    d3d = fallback.Get();
                    ordinal = i;
                }
                if (LogFile log; log.file) std::fprintf(log.file, "DX9 compatibility retry: 0x%08lx\n", recovered);
                break;
            }
        }
    }
    if (FAILED(result) || !device || !*device || !parameters) return result;
    if (!gameTarget) { *parameters=publicTarget; return result; }
    RecordDisplayMode(*device,*parameters,requested);
    trackedDevice = *device;
    gameFocusWindow = window;
    unlimited_split_screen::SetGameWindow(window);
    nativePassesActive = nativePassesAtStartup != 9 && scene_hud::SupportsDevice(*device);
    nativeMoviePass.SetBackend(nativePassesAtStartup);
    UpdateAspect(parameters->BackBufferWidth, parameters->BackBufferHeight);
    if (LogFile log; log.file) {
        D3DDISPLAYMODE mode{};
        (*device)->GetDisplayMode(0, &mode);
        std::fprintf(log.file, "Device created: %ux%u, fullscreen=%u interval=0x%08X requested_refresh=%u actual_refresh=%u\n",
                     parameters->BackBufferWidth, parameters->BackBufferHeight, !parameters->Windowed,
                     parameters->PresentationInterval, parameters->FullScreen_RefreshRateInHz, mode.RefreshRate);
        std::fprintf(log.file, "Unlimited rendering: scene=D3D9 movie_api_requested=%u movie_interop_available=%u\n",
                     nativePassesAtStartup, nativePassesActive);
    }
    if (adapter) { adapter->Release(); adapter=nullptr; }
    SelectAdapter(d3d, ordinal);
    void** original = *reinterpret_cast<void***>(*device);
    if (original[4] != reinterpret_cast<void*>(&ProbeAvailableMemory))
        nativeAvailableMemory = reinterpret_cast<AvailableMemory>(original[4]);
    QueryPerformanceFrequency(&frequency);
    ReplaceMethod(original, 4, reinterpret_cast<void*>(&ProbeAvailableMemory));
    return result;
}
} // namespace

#include "nfs_audio.hpp"
#include "nfs_xenon.hpp"

#include "nfs_console_renderer.hpp"
#include "nfs_save_hooks.hpp"

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        const auto executable = GetModuleHandleW(nullptr);
        if (IsTrackedBuild(executable)) {
            engine = executable;
            unlimited_saves::Install();
            if (LocateSettings() && GetPrivateProfileIntW(L"Graphics", L"XenonEffects", 0, settingsPath)) {
                unlimited_console::InstallAssetRouting();
                unlimited_console::LoadConsolePlatformLights(instance);
                unlimited_console::InstallConsoleGarageShadows();
                unlimited_console::InstallConsoleShadowDirection();
                unlimited_console::InstallConsoleMaterials(instance);
            }
            renderPoolsExpanded = InstallReflectionRenderPools();
            languageBootstrapInstalled = unlimited_language::InstallLanguageBootstrap();
            unlimited_split_screen::Install(executable);
        }
    }
    return TRUE;
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdkVersion) {
    const auto executable = GetModuleHandleW(nullptr);
    if (trackedFactory || !IsTrackedBuild(executable)) return SystemFactory(sdkVersion);
    engine = executable;
    const DWORD length = GetTempPathW(MAX_PATH, logPath);
    if (!length || length >= MAX_PATH - 32) return SystemFactory(sdkVersion);
    wcscat(logPath, L"NFSMW-graphics.log");
    wchar_t mode[8]{};
    availableThreads = AvailableGameThreads();
    LoadFrameLimit();
    frameStatistics = GetPrivateProfileIntW(L"Diagnostics", L"FrameStatistics", 0, settingsPath) != 0;
    LoadVideoEffectsSettings();
    // The world renderer chains the active capability query when it installs its own.
    const bool displayCaps = InstallDisplayCapabilityHook();
    const char* worldRenderer = unlimited_console::SelectWorldRenderer();
    if (LogFile log; log.file) std::fprintf(log.file, "World renderer: %s\n", worldRenderer);
    unlimited_console::SelectPlatformReflections();
    const bool consoleMusic = unlimited_audio::SelectConsoleMusicBank();
    if (settingsPath[0] && GetPrivateProfileIntW(L"Graphics", L"XenonEffects", 0, settingsPath) == 1) {
        unlimited_textures::OpenXbox(settingsPath);
    }
    if (LogFile log; log.file)
        std::fprintf(log.file, "Pathfinder soundtrack bank: %s; console material entries=%u; console vault opened=%ld\n",
                     consoleMusic ? "Xbox 360" : "PC", unlimited_console::materialCount, unlimited_console::vaultOpened);
    IDirect3D9* factory = nativePassesAtStartup != 9 ? scene_hud::CreateInteropFactory(SystemD3D9(), sdkVersion) : nullptr;
    displayResetInstalled = InstallGameDisplayReset();
    if (displayResetInstalled)
        GameValue<BOOL>(0x582bf0)=requestedWindowMode!=unlimited_display::Mode::Exclusive;
    nativePassFactorySelected = factory != nullptr && displayResetInstalled;
    if (factory && !nativePassFactorySelected) { factory->Release(); factory=nullptr; }
    if (!factory) factory = SystemFactory(sdkVersion);
    if (!factory) return nullptr;
    trackedFactory = factory;
    const bool splashBrand = InstallSplashBrand();
    const bool languages = unlimited_language::InstallLanguageMenu();
    const bool audio = InstallSurroundAudio();
    const bool controller = UnlimitedController::Install(executable);
    const bool rumble = UnlimitedRumble::Install(executable, settingsPath);
    aspectHookInstalled = InstallAspectCorrection();
    const bool responsiveHud = aspectHookInstalled && InstallResponsiveRaceHud();
    const bool frontendServices = InstallResponsiveSongNotice(aspectHookInstalled);
    const bool responsiveSongNotice = aspectHookInstalled && frontendServices;
    const bool optionLayout = InstallResponsiveOptionLayout();
    const bool moviePresentation = InstallMoviePresentation();
    const bool shadowNative = nativePassFactorySelected && nativePassesAtStartup == 12 && InstallNativeShadowList();
    if (GetEnvironmentVariableW(L"NFSMW_PACE60", mode, 8) == 1 && mode[0] == L'1') frameLimitIndex = 2;
    const DWORD timingMode = GetEnvironmentVariableW(L"NFSMW_TIMING", mode, 8);
    const bool installed = InstallPresentCall();
    const unsigned rate = timingMode == 3 && wcscmp(mode, L"120") == 0 ? 120 : LowThreadBudget() ? 60 : 240;
    const bool timingInstalled = !(timingMode == 5 && wcscmp(mode, L"stock") == 0) && InstallUncappedTiming(rate);
    const bool frameMenu = installed && timingInstalled && InstallFrameLimitMenu();
    effectsPageInstalled = frameMenu && settingsPath[0] && InstallVideoEffectsPage();
    InstallVideoNavigation();
    blurIntensityInstalled = InstallMotionBlurIntensity();
    const bool eaTraxRotation = InstallEATraxDefaults();
    shadowHookInstalled = InstallShadowAllocation();
    const bool shadowContinuity = InstallShadowContinuity();
    const bool reflectionInstalled = !LowThreadBudget() && InstallReflectionRefresh();
    reflectionQualityInstalled = InstallVehicleReflectionQuality();
    const bool adaptiveMirror = InstallAdaptiveRearviewMirror();
    const auto mirrorDraw = unlimited_console::rendererBase
        ? reinterpret_cast<MirrorDraw>(unlimited_console::rendererBase + 0x2f2c0)
        : GameFunction<MirrorDraw>(0x2d1260);
    const bool responsiveMirror = aspectHookInstalled && InstallResponsiveMirrorComposition(mirrorDraw);
    const bool rearviewCameras = InstallRearviewCameraVisibility();
    rearviewEffectsInstalled = InstallRearviewEffects();
    const bool carDetail = InstallImprovedCarDetail();
    const bool fontScaling = InstallFontAtlasScaling();
    const bool cursorScaling = InstallHdCursorScale();
    const bool filtering = InstallTextureFiltering();
    const bool textureUpload = InstallFullSizeTextureUpload();
    unlimited_textures::installed = textureUpload && unlimited_textures::remaster.count != 0;
    if (textureUpload) unlimited_textures::InstallEffectDemandHook();
    if (LogFile log; log.file)
        std::fprintf(log.file, "Texture quality: profile=%u entries=%u console_entries=%u upload=%u\n",
                     static_cast<unsigned>(unlimited_textures::startup), unlimited_textures::remaster.count,
                     unlimited_textures::console.count, textureUpload);
    const bool reflectedLights = InstallRestoredReflectionLights();
    const char* xenonEffects = LoadOptionalXenonEffects();
    if (LogFile log; log.file)
        std::fprintf(log.file, "Advanced graphics: effects_page=%u motion_blur_intensity=%u render_scale=%u%% ea_trax_defaults=%u threads=%u\n",
                     effectsPageInstalled, blurIntensityInstalled, renderScaleTenths * 10, eaTraxRotation, availableThreads);
    xenonStartup = xenonPreference = settingsPath[0] &&
        GetPrivateProfileIntW(L"Graphics", L"XenonEffects", 0, settingsPath) == 1;
    xenonUnavailable = xenonStartup && std::strcmp(xenonEffects, "enabled") != 0 &&
                       std::strcmp(xenonEffects, "already loaded") != 0;
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Unlimited scene: dx12_shadow_list=%u rearview_effects=%u rearview_mode=%u rearview_cameras=%u rearview_layout=%u\n",
                     shadowNative, rearviewEffectsInstalled, static_cast<unsigned>(rearviewEffects),
                     rearviewCameras, responsiveMirror);
        std::fprintf(log.file, "Unlimited UI: artwork=game_textures instruments=game language_selector_layout=%u hd_cursor=%u splash_brand=%u song_notice_anchors=%u\n",
                     optionLayout, cursorScaling, splashBrand, responsiveSongNotice);
        std::fprintf(log.file, "\nSession pid=%lu frame_limit=%u graphics_menu=%s present_call=%s sim_step=%uHz timing=%s shadows=%s shadow_continuity=%s cube_reflections=%s font_atlas=%s filtering=%s\n",
                     GetCurrentProcessId(), EffectiveFrameLimit(), frameMenu ? "FPS option" : "stock",
                     installed ? "verified" : "unmodified",
                     simulationRate, timingInstalled ? "decoupled" : "stock",
                     shadowHookInstalled ? "adaptive" : "unmodified", shadowContinuity ? "enabled" : "unmodified",
                     reflectionInstalled ? "all faces per frame" : "unmodified",
                     fontScaling ? "preserve glyph bounds" : "unmodified", filtering ? "anisotropic when enabled" : "unmodified");
        std::fprintf(log.file, "Unlimited modules: aspect=%u native_display=%u hd_upload=%u surround=%u controllers=%u reflected_lights=%u reflection_pools=%u xenon_effects=%s\n",
                     aspectHookInstalled, displayCaps && aspectHookInstalled, textureUpload, audio,
                     controller, reflectedLights, renderPoolsExpanded, xenonEffects);
        std::fprintf(log.file, "Unlimited modules: hud_anchors=%u car_lod=%u cube_sizes=%u rearview_target=%u rumble=%u language_boot=%u language_menu=%u cinematic_fit=%u\n",
                     responsiveHud, carDetail, reflectionQualityInstalled, adaptiveMirror, rumble, languageBootstrapInstalled,
                     languages, moviePresentation);
    }
    void** vtable = *reinterpret_cast<void***>(factory);
    nativeCreateDevice = reinterpret_cast<CreateDevice>(vtable[16]);
    ReplaceMethod(vtable, 16, reinterpret_cast<void*>(&ProbeDevice));
    return factory;
}
