#pragma once

#include <array>
#include <cstdio>
#include <cwchar>

namespace {
namespace unlimited_language {

struct Mode {
    const wchar_t* key;
    const wchar_t* resource;
    const char* label;
    unsigned nativeIndex;
};

constexpr Mode modes[] = {
    {L"English US", L"english", "English (US)", 0},
    {L"English UK", L"english", "English (UK)", 1},
    {L"French", L"french", u8"Français", 2},
    {L"German", L"german", "Deutsch", 3},
    {L"Italian", L"italian", "Italiano", 4},
    {L"Spanish", L"spanish", u8"Español", 5},
    {L"Swedish", L"swedish", "Svenska", 6},
    {L"Danish", L"danish", "Dansk", 7},
    {L"Dutch", L"dutch", "Nederlands", 8},
    {L"Polish", L"polish", "Polski", 14},
    {L"Finnish", L"finnish", "Suomi", 15},
};

struct NativeMode {
    const char* name;
    int language;
    int region;
};

constexpr std::uintptr_t nativeModes = 0x4fb578;
constexpr std::uintptr_t nativeLanguage = 0x4fb574;
constexpr std::uintptr_t nativeLanguageName = 0x582d20;
constexpr std::uintptr_t nativeRegion = 0x582d24;

struct Choices {
    wchar_t directory[MAX_PATH]{};
    wchar_t ini[MAX_PATH]{};
    std::array<const Mode*, std::size(modes)> enabled{};
    std::size_t count = 0;
    const Mode* active = nullptr;
    const Mode* selected = nullptr;
    bool saveFailed = false;
};

Choices choices{};
bool bootstrapInstalled = false;
constexpr int category = 10;
constexpr std::uint32_t titleHash = 0x7a1c6e23;
constexpr std::uint32_t iconHash = 0x4df98fb2;
constexpr std::uint32_t acceptMessage = 0x0c407210;
constexpr std::uint32_t previousMessage = 0x9120409e;
constexpr std::uint32_t nextMessage = 0xb5971bf1;

bool ExistingFile(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA file{};
    return GetFileAttributesExW(path, GetFileExInfoStandard, &file) &&
           !(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           (file.nFileSizeHigh || file.nFileSizeLow);
}

bool HasMovie(const wchar_t* directory, const wchar_t* language, bool ntsc) {
    wchar_t name[MAX_PATH]{};
    const int size = std::swprintf(name, std::size(name), L"%ls\\MOVIES\\blacklist_01_%ls_%ls.vp6",
                                   directory, language, ntsc ? L"ntsc" : L"pal");
    return size > 0 && ExistingFile(name);
}

bool HasLanguage(const wchar_t* directory, const Mode& mode) {
    wchar_t name[MAX_PATH]{};
    const int size = std::swprintf(name, std::size(name), L"%ls\\LANGUAGES\\%ls.bin", directory, mode.resource);
    if (size <= 0 || !ExistingFile(name)) return false;
    const bool ntsc = mode.nativeIndex == 0;
    return HasMovie(directory, mode.resource, ntsc) || (!ntsc && HasMovie(directory, L"english", false));
}

bool LoadPaths(Choices& state) {
    const DWORD count = GetModuleFileNameW(engine, state.directory, MAX_PATH);
    auto* filename = count > 0 && count < MAX_PATH ? std::wcsrchr(state.directory, L'\\') : nullptr;
    if (!filename) return false;
    *filename = L'\0';

    // The executable may live under Program Files. Store the player's
    // selection in a writable location without modifying EA's registry.
    wchar_t userDirectory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", userDirectory, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        wchar_t preferences[MAX_PATH]{};
        const int written = std::swprintf(preferences, std::size(preferences),
                                          L"%ls\\NFSMW-Unlimited", userDirectory);
        if (written > 0) {
            CreateDirectoryW(preferences, nullptr);
            const auto attributes = GetFileAttributesW(preferences);
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
                std::swprintf(state.ini, std::size(state.ini), L"%ls\\Language.ini", preferences) > 0) return true;
        }
    }
    return std::swprintf(state.ini, std::size(state.ini), L"%ls\\NFSMW-Unlimited.ini", state.directory) > 0;
}

const NativeMode& NativeSettings(const Mode& mode) {
    return GameValue<NativeMode>(nativeModes + mode.nativeIndex * sizeof(NativeMode));
}

void ReadChoices(Choices& state) {
    wchar_t preferred[48]{};
    GetPrivateProfileStringW(L"Language", L"Game", L"", preferred, std::size(preferred), state.ini);
    state.count = 0;
    state.active = nullptr;
    state.selected = nullptr;
    const auto* currentName = GameValue<const char*>(nativeLanguageName);
    for (const auto& mode : modes) {
        if (!HasLanguage(state.directory, mode)) continue;
        const auto& native = NativeSettings(mode);
        state.enabled[state.count++] = &mode;
        if (currentName == native.name) state.active = &mode;
        if (_wcsicmp(preferred, mode.key) == 0) state.selected = &mode;
    }
    if (!state.selected) state.selected = state.active;
    if (!state.selected && state.count) state.selected = state.enabled[0];
}

void ApplySelection(Choices& state) {
    if (!state.selected || state.selected == state.active) return;
    const auto& native = NativeSettings(*state.selected);
    GameValue<int>(nativeLanguage) = native.language;
    GameValue<const char*>(nativeLanguageName) = native.name;
    GameValue<int>(nativeRegion) = native.region;
    state.active = state.selected;
}

bool SaveSelection(Choices& state, const Mode& mode) {
    if (!HasLanguage(state.directory, mode) ||
        !WritePrivateProfileStringW(L"Language", L"Game", mode.key, state.ini)) {
        state.saveFailed = true;
        return false;
    }
    state.saveFailed = false;
    state.selected = &mode;
    return true;
}

bool __cdecl ReadNativeAndPreferredLanguage() {
    const bool result = GameFunction<bool (__cdecl*)()>(0x2cb680)();
    if (LoadPaths(choices)) {
        ReadChoices(choices);
        ApplySelection(choices);
    }
    return result;
}

bool InstallLanguageBootstrap() {
    bootstrapInstalled = RedirectCall(0x2cba9f, 0x2cb680,
                                     reinterpret_cast<std::uintptr_t>(&ReadNativeAndPreferredLanguage));
    return bootstrapInstalled;
}

struct Strings {
    const char* categoryName;
    const char* row;
    const char* restart;
    const char* saveError;
};

const Strings& MenuStrings() {
    constexpr static Strings english = {"Languages", "Game language", " (restart)", "Could not save"};
    constexpr static Strings spanish = {"Idiomas", "Idioma del juego", " (reiniciar)", "Error al guardar"};
    constexpr static Strings french = {"Langues", "Langue du jeu", u8" (redémarrer)", u8"Échec de l’enregistrement"};
    constexpr static Strings german = {"Sprachen", "Spielsprache", " (Neustart)", "Speichern fehlgeschlagen"};
    constexpr static Strings italian = {"Lingue", "Lingua di gioco", " (riavvia)", "Salvataggio non riuscito"};
    constexpr static Strings dutch = {"Talen", "Speltaal", " (herstarten)", "Opslaan mislukt"};
    constexpr static Strings swedish = {u8"Språk", u8"Spelspråk", " (starta om)", "Kunde inte spara"};
    constexpr static Strings danish = {"Sprog", "Spilsprog", " (genstart)", "Kunne ikke gemme"};
    constexpr static Strings polish = {u8"Języki", u8"Język gry", " (restart)", u8"Błąd zapisu"};
    constexpr static Strings finnish = {"Kielet", "Pelin kieli", u8" (käynnistä uudelleen)", "Tallennus ei onnistunut"};
    switch (GameValue<int>(0x4f41c0)) {
    case 1: return french;
    case 2: return german;
    case 3: return italian;
    case 4: return spanish;
    case 5: return dutch;
    case 6: return swedish;
    case 7: return danish;
    case 12: return polish;
    case 13: return finnish;
    default: return english;
    }
}

int* CategoryIndex() {
    const auto* dal = GameValue<std::uint8_t*>(0x51cf90);
    if (!dal) return nullptr;
    auto* options = *reinterpret_cast<std::uint8_t* const*>(dal + 0x10);
    return options ? reinterpret_cast<int*>(options + 0x24) : nullptr;
}

constexpr std::uint32_t thinArrowTexture = 0x1a6d3d2c;
constexpr std::uint32_t languageChevronTexture = 0x03c174cf; // MWU_LANGUAGE_CHEVRON
constexpr std::uint32_t leftArrowRowOne = 0x7bcd6704, rightArrowRowOne = 0x7c51b6d7;

void UpdateLanguageChevron(void* object) {
    if (!object || HudField<std::uint32_t>(object,0x18)!=1) return;
    const auto name=HudField<std::uint32_t>(object,0x10);
    if (name!=leftArrowRowOne && name!=rightArrowRowOne) return;
    const auto current=HudField<std::uint32_t>(object,0x24);
    if (current!=thinArrowTexture && current!=languageChevronTexture) return;

    auto* rendering=GameValue<void*>(0x51cfac);
    const auto* package=rendering ? HudField<const char*>(rendering,0x0c) : nullptr;
    if (!package || _stricmp(package,"Options.fng")) return;
    const auto* index=CategoryIndex();
    const auto wanted=index && *index==category ? languageChevronTexture : thinArrowTexture;
    if (current==wanted) return;
    if (wanted==languageChevronTexture) {
        using Lookup=void* (__cdecl*)(std::uint32_t,short,short);
        auto* texture=GameFunction<Lookup>(0x103400)(wanted,0,0);
        if (!texture || HudField<std::uint32_t>(texture,0x24)!=wanted) return;
    }
    // FEngSetTextureHash marks the cached drawing commands dirty when the handle changes.
    GameFunction<void (__cdecl*)(void*,std::uint32_t)>(0x115b90)(object,wanted);
}

void __fastcall RenderLanguageChevron(void* renderer, void*, void* object, void* renderInfo) {
    UpdateLanguageChevron(object);
    GameFunction<void (__thiscall*)(void*,void*,void*)>(0x1a45b0)(renderer,object,renderInfo);
}

void __fastcall ReactToCategory(void*, void*, const char*, std::uint32_t message,
                                void*, std::uint32_t, std::uint32_t) {
    if (message == acceptMessage) {
        if (auto* current = CategoryIndex()) *current = category;
    }
}

using Allocate = void* (__cdecl*)(std::size_t);

void __fastcall AddControlAndLanguage(void* menu, void*, void* control) {
    const auto add = GameFunction<void (__thiscall*)(void*, void*)>(0x173960);
    add(menu, control);
    if (!choices.selected) return;
    auto* option = GameFunction<Allocate>(0x252ad0)(0x4c);
    if (!option) return;
    GameFunction<void (__thiscall*)(void*, std::uint32_t, std::uint32_t, int)>(0x186fa0)(option, iconHash, titleHash, 0);
    static void* table[] = {GameValue<void*>(0x49b8f0), reinterpret_cast<void*>(&ReactToCategory)};
    *static_cast<void***>(option) = table;
    add(menu, option);
}

void __fastcall DrawLanguage(std::uint8_t* option) {
    const auto& strings = MenuStrings();
    const auto write = GameFunction<void (__thiscall*)(std::uint8_t*, const char*)>(0x1bcc10);
    const auto draw = [&](std::size_t offset, const char* text) {
        auto* object = *reinterpret_cast<std::uint8_t**>(option + offset);
        if (object) {
            write(object, text);
            *reinterpret_cast<std::uint32_t*>(object + 0x1c) |= 0x400000u;
        }
    };
    draw(0x2c, strings.row);
    char value[80]{};
    if (choices.selected) {
        const char* restart = choices.selected != choices.active ? strings.restart : "";
        std::snprintf(value, sizeof(value), "%s%s", choices.selected->label, restart);
    }
    draw(0x30, choices.saveFailed ? strings.saveError : value);
}

const Mode* AdvanceChoice(const Choices& state, bool backwards) {
    const auto begin = state.enabled.begin();
    const auto end = begin + state.count;
    const auto found = std::find(begin, end, state.selected);
    if (found == end || state.count < 2) return nullptr;
    const auto index = static_cast<std::size_t>(found - begin);
    for (std::size_t step = 1; step < state.count; ++step) {
        const auto offset = backwards ? state.count - step : step;
        const auto* mode = state.enabled[(index + offset) % state.count];
        if (HasLanguage(state.directory, *mode)) return mode;
    }
    return nullptr;
}

void __fastcall ChangeLanguage(std::uint8_t* option, void*, const char*, std::uint32_t message) {
    if (message != previousMessage && message != nextMessage) return;
    const auto* mode = AdvanceChoice(choices, message == previousMessage);
    if (!mode) return;
    SaveSelection(choices, *mode);
    option[0x2a] = 1;
    const auto table = *reinterpret_cast<void***>(option);
    reinterpret_cast<void (__thiscall*)(void*, std::uint32_t)>(table[14])(option, message);
    DrawLanguage(option);
}

extern "C" __attribute__((used)) void __cdecl UnlimitedBuildLanguageOptions(std::uint8_t* screen) {
    const auto package = *reinterpret_cast<const char**>(screen + 0x10);
    using SetTitle = void (__cdecl*)(const char*, std::uint32_t, std::uint32_t);
    GameFunction<SetTitle>(0x125220)(package, 0x42adb44c, titleHash);

    const auto find = GameFunction<void* (__thiscall*)(void*, const char*)>(0x173cc0);
    const auto prefixes = reinterpret_cast<const char* const*>(screen + 0xb0);
    if (choices.selected && *reinterpret_cast<int*>(screen + 0x104) < 10 && prefixes[0] &&
        prefixes[1] && find(screen, prefixes[0]) && find(screen, prefixes[1])) {
        auto* option = GameFunction<Allocate>(0x252ad0)(0x5c);
        if (option) {
            static const auto table = [] {
                std::array<void*, 15> callbacks{};
                const auto* native = &GameValue<void*>(0x49bac8);
                std::copy_n(native, callbacks.size(), callbacks.begin());
                callbacks[1] = reinterpret_cast<void*>(&ChangeLanguage);
                callbacks[3] = reinterpret_cast<void*>(&DrawLanguage);
                return callbacks;
            }();
            GameFunction<void (__thiscall*)(void*, bool)>(0x11a7b0)(option, true);
            *static_cast<void***>(option) = const_cast<void**>(table.data());
            GameFunction<void (__thiscall*)(void*, void*, int)>(0x188570)(screen, option, 1);
        }
    }
    GameFunction<void (__thiscall*)(void*, int)>(0x1a5ea0)(screen, 0);
}

__attribute__((naked)) void DispatchLanguageOptions() {
    asm volatile(
        "pushl %esi\n"
        "call _UnlimitedBuildLanguageOptions\n"
        "addl $4, %esp\n"
        "popl %esi\n"
        "ret\n");
}

extern "C" {
__attribute__((used)) std::uintptr_t unlimitedLanguageStringTable = 0;
__attribute__((used)) std::uintptr_t unlimitedLanguageLookupResume = 0;

__attribute__((used)) const char* __cdecl UnlimitedLanguageTitle() {
    return MenuStrings().categoryName;
}
}

__attribute__((naked)) const char* __fastcall FindLanguageTitle(void*, std::uint32_t) {
    asm volatile(
        "cmpl $0x7a1c6e23, %edx\n"
        "jne 1f\n"
        "jmp _UnlimitedLanguageTitle\n"
        "1:\n"
        "pushl %ebx\n"
        "movl _unlimitedLanguageStringTable, %ebx\n"
        "movl (%ebx), %ebx\n"
        "jmp *_unlimitedLanguageLookupResume\n");
}

bool __fastcall ExitLanguageOptions(void* screen, void*) {
    const auto* index = CategoryIndex();
    return (index && *index == category) || GameFunction<bool (__thiscall*)(void*)>(0x110360)(screen);
}

struct Patch {
    std::uintptr_t offset = 0;
    std::size_t size = 0;
    std::array<std::uint8_t, 7> original{};
    std::array<std::uint8_t, 7> replacement{};
};

template<class T>
Patch Replace(std::uintptr_t offset, const T& oldValue, const T& newValue) {
    static_assert(sizeof(T) <= 7);
    Patch patch{};
    patch.offset = offset;
    patch.size = sizeof(T);
    std::memcpy(patch.original.data(), &oldValue, sizeof(T));
    std::memcpy(patch.replacement.data(), &newValue, sizeof(T));
    return patch;
}

Patch ReplaceCall(std::uintptr_t offset, std::uintptr_t original, std::uintptr_t destination) {
    const auto image = reinterpret_cast<std::uintptr_t>(engine);
    std::array<std::uint8_t, 5> before{0xe8}, after{0xe8};
    const auto expected = original - offset - 5;
    const auto displacement = destination - image - offset - 5;
    std::memcpy(before.data() + 1, &expected, sizeof(expected));
    std::memcpy(after.data() + 1, &displacement, sizeof(displacement));
    return Replace(offset, before, after);
}

bool InstallLanguageMenu() {
    if (!bootstrapInstalled) return false;
    constexpr std::uintptr_t openTable = 0x1456fc, buildTable = 0x145dfc;
    constexpr std::uintptr_t lookup = 0x16bb80, originalImageBase = 0x400000;
    const auto image = reinterpret_cast<std::uintptr_t>(engine);
    static std::array<std::uintptr_t, category + 1> open{}, build{};
    std::copy_n(&GameValue<std::uintptr_t>(openTable), category, open.begin());
    std::copy_n(&GameValue<std::uintptr_t>(buildTable), category, build.begin());
    open.back() = open.front();
    build.back() = reinterpret_cast<std::uintptr_t>(&DispatchLanguageOptions);

    constexpr std::array<std::uint8_t, 7> before{0x53, 0x8b, 0x1d, 0x80, 0xcf, 0x91, 0x00};
    std::array<std::uint8_t, 7> after{0xe9, 0, 0, 0, 0, 0x90, 0x90};
    const auto displacement = reinterpret_cast<std::uintptr_t>(&FindLanguageTitle) - image - lookup - 5;
    std::memcpy(after.data() + 1, &displacement, sizeof(displacement));
    unlimitedLanguageStringTable = image + 0x51cf80;
    unlimitedLanguageLookupResume = image + lookup + before.size();

    const auto leave = reinterpret_cast<std::uintptr_t>(&ExitLanguageOptions);
    const Patch patches[] = {
        Replace(lookup, before, after),
        Replace(0x1455fd, originalImageBase + openTable, reinterpret_cast<std::uintptr_t>(open.data())),
        Replace(0x1455f3, std::uint8_t{9}, std::uint8_t{category}),
        Replace(0x145d7d, originalImageBase + buildTable, reinterpret_cast<std::uintptr_t>(build.data())),
        Replace(0x145d77, std::uint8_t{9}, std::uint8_t{category}),
        ReplaceCall(0x1459e1, 0x110360, leave),
        ReplaceCall(0x145b5d, 0x110360, leave),
        ReplaceCall(0x12909c, 0x173960, reinterpret_cast<std::uintptr_t>(&AddControlAndLanguage)),
        ReplaceCall(0x1a46fd, 0x1a45b0, reinterpret_cast<std::uintptr_t>(&RenderLanguageChevron)),
    };
    for (const auto& patch : patches) {
        if (!MatchGameBytes(patch.offset, patch.original.data(), patch.size)) return false;
    }
    for (std::size_t i = 0; i < std::size(patches); ++i) {
        const auto& patch = patches[i];
        if (WriteGameBytes(patch.offset, patch.replacement.data(), patch.size)) continue;
        while (i > 0) {
            const auto& previous = patches[--i];
            WriteGameBytes(previous.offset, previous.original.data(), previous.size);
        }
        return false;
    }
    return true;
}

} // namespace unlimited_language
} // namespace

