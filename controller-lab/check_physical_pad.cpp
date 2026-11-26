#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

namespace {

struct SdlApi {
    decltype(&SDL_SetMainReady) setMainReady{};
    decltype(&SDL_Init) init{};
    decltype(&SDL_InitSubSystem) initSubSystem{};
    decltype(&SDL_Quit) quit{};
    decltype(&SDL_GetError) error{};
    decltype(&SDL_GetJoysticks) joysticks{};
    decltype(&SDL_free) free{};
    decltype(&SDL_GetJoystickNameForID) name{};
    decltype(&SDL_GetJoystickVendorForID) vendor{};
    decltype(&SDL_GetJoystickProductForID) product{};
    decltype(&SDL_IsJoystickVirtual) isVirtual{};
    decltype(&SDL_IsGamepad) isGamepad{};
    decltype(&SDL_OpenGamepad) openGamepad{};
    decltype(&SDL_CloseGamepad) closeGamepad{};
    decltype(&SDL_GetGamepadProperties) properties{};
    decltype(&SDL_GetBooleanProperty) booleanProperty{};
    decltype(&SDL_RumbleGamepad) rumble{};
    decltype(&SDL_GetGamepadJoystick) gamepadJoystick{};
    decltype(&SDL_OpenHapticFromJoystick) openHaptic{};
    decltype(&SDL_CloseHaptic) closeHaptic{};
    decltype(&SDL_InitHapticRumble) initHaptic{};
    decltype(&SDL_PlayHapticRumble) playHaptic{};
    decltype(&SDL_StopHapticRumble) stopHaptic{};

    bool Bind(HMODULE module) {
        bool allPresent = true;
#define BIND(field, exportName)                                                      \
        field = reinterpret_cast<decltype(field)>(GetProcAddress(module, #exportName)); \
        allPresent = allPresent && field != nullptr
        BIND(setMainReady, SDL_SetMainReady);
        BIND(init, SDL_Init);
        BIND(initSubSystem, SDL_InitSubSystem);
        BIND(quit, SDL_Quit);
        BIND(error, SDL_GetError);
        BIND(joysticks, SDL_GetJoysticks);
        BIND(free, SDL_free);
        BIND(name, SDL_GetJoystickNameForID);
        BIND(vendor, SDL_GetJoystickVendorForID);
        BIND(product, SDL_GetJoystickProductForID);
        BIND(isVirtual, SDL_IsJoystickVirtual);
        BIND(isGamepad, SDL_IsGamepad);
        BIND(openGamepad, SDL_OpenGamepad);
        BIND(closeGamepad, SDL_CloseGamepad);
        BIND(properties, SDL_GetGamepadProperties);
        BIND(booleanProperty, SDL_GetBooleanProperty);
        BIND(rumble, SDL_RumbleGamepad);
        BIND(gamepadJoystick, SDL_GetGamepadJoystick);
        BIND(openHaptic, SDL_OpenHapticFromJoystick);
        BIND(closeHaptic, SDL_CloseHaptic);
        BIND(initHaptic, SDL_InitHapticRumble);
        BIND(playHaptic, SDL_PlayHapticRumble);
        BIND(stopHaptic, SDL_StopHapticRumble);
#undef BIND
        return allPresent;
    }
};

enum class GameStatus { Closed, Running, Unknown };

GameStatus CheckGameStatus() {
    const HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE) return GameStatus::Unknown;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(processes, &entry)) {
        do {
            found = _wcsicmp(entry.szExeFile, L"speed.exe") == 0;
        } while (!found && Process32NextW(processes, &entry));
    }
    const DWORD error = GetLastError();
    CloseHandle(processes);
    if (found) return GameStatus::Running;
    return error == ERROR_NO_MORE_FILES ? GameStatus::Closed : GameStatus::Unknown;
}

bool WaitForPulse(const SdlApi& api, SDL_Gamepad* gamepad, Uint16 low, Uint16 high) {
    constexpr Uint32 durationMs = 125;
    const bool accepted = api.rumble(gamepad, low, high, durationMs);
    if (accepted) Sleep(durationMs);
    const bool stopped = api.rumble(gamepad, 0, 0, 0);
    return accepted && stopped;
}

int Pulse(const SdlApi& api, SDL_JoystickID id) {
    std::unique_ptr<SDL_Gamepad, decltype(api.closeGamepad)> gamepad(api.openGamepad(id), api.closeGamepad);
    if (!gamepad) {
        std::fprintf(stderr, "No se pudo abrir el mando: %s\n", api.error());
        return 5;
    }
    const auto properties = api.properties(gamepad.get());
    if (api.booleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)) {
        constexpr Uint16 strength = 26000;
        const bool low = WaitForPulse(api, gamepad.get(), strength, 0);
        const bool high = WaitForPulse(api, gamepad.get(), 0, strength);
        std::printf("Baja frecuencia: %s. Alta frecuencia: %s.\n",
                    low ? "pulso y parada aceptados" : "prueba fallida",
                    high ? "pulso y parada aceptados" : "prueba fallida");
        return low && high ? 0 : 5;
    }

    if (!api.initSubSystem(SDL_INIT_HAPTIC)) {
        std::fprintf(stderr, "No se pudo inicializar la respuesta haptica: %s\n", api.error());
        return 5;
    }
    std::unique_ptr<SDL_Haptic, decltype(api.closeHaptic)> haptic(
        api.openHaptic(api.gamepadJoystick(gamepad.get())), api.closeHaptic);
    if (!haptic || !api.initHaptic(haptic.get())) {
        std::fprintf(stderr, "El controlador no expone vibracion compatible: %s\n", api.error());
        return 5;
    }
    constexpr Uint32 durationMs = 125;
    const bool accepted = api.playHaptic(haptic.get(), 0.4f, durationMs);
    if (accepted) Sleep(durationMs);
    const bool stopped = api.stopHaptic(haptic.get());
    std::printf("Respuesta haptica: %s.\n", accepted && stopped ? "pulso y parada aceptados" : "prueba fallida");
    return accepted && stopped ? 0 : 5;
}

struct DeviceMatch {
    Uint16 vendor{};
    Uint16 product{};
};

bool ParseDeviceMatch(const wchar_t* text, DeviceMatch& match) {
    if (std::wcslen(text) != 9 || text[4] != L':') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 7, 8}) {
        if (std::wcschr(L"0123456789abcdefABCDEF", text[i]) == nullptr) return false;
    }
    match.vendor = static_cast<Uint16>(std::wcstoul(text, nullptr, 16));
    match.product = static_cast<Uint16>(std::wcstoul(text + 5, nullptr, 16));
    return match.vendor != 0 || match.product != 0;
}

int Probe(const SdlApi& api, const DeviceMatch* match) {
    api.setMainReady();
    if (!api.init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "No se pudo inicializar SDL3: %s\n", api.error());
        return 2;
    }
    int count{};
    std::unique_ptr<SDL_JoystickID, decltype(api.free)> ids(api.joysticks(&count), api.free);
    if (!ids) {
        std::fprintf(stderr, "No se pudieron enumerar los mandos: %s\n", api.error());
        return 2;
    }
    SDL_JoystickID pulseTarget = 0;
    int matches = 0;
    for (int i = 0; i < count; ++i) {
        const auto id = ids.get()[i];
        const auto* name = api.name(id);
        const auto vendor = api.vendor(id);
        const auto product = api.product(id);
        const bool virtualDevice = api.isVirtual(id);
        const bool mapped = api.isGamepad(id);
        std::printf("ID %u | %s | %04X:%04X | %s | %s\n", id, name ? name : "Sin nombre",
                    unsigned(vendor), unsigned(product),
                    mapped ? "mando reconocido" : "sin asignacion de botones",
                    virtualDevice ? "virtual de SDL" : "detectado por SDL");
        if (match && mapped && !virtualDevice && match->vendor == vendor && match->product == product) {
            pulseTarget = id;
            ++matches;
        }
    }
    std::printf("Dispositivos encontrados: %d.\n", count);
    if (!match) return 0;
    if (matches != 1) {
        std::fprintf(stderr, "La prueba encontro %d mandos reconocidos con ese VID:PID. Se requiere uno.\n", matches);
        return 4;
    }
    if (CheckGameStatus() != GameStatus::Closed) {
        std::fputs("La prueba requiere que el juego este cerrado.\n", stderr);
        return 3;
    }
    return Pulse(api, pulseTarget);
}

int Usage() {
    std::fputs("Uso: check_physical_pad <ruta a SDL3.dll> [--pulse VID:PID]\n", stderr);
    return 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 4) return Usage();
    DeviceMatch match{};
    const bool sendPulse = argc == 4;
    if (sendPulse) {
        if (std::wcscmp(argv[2], L"--pulse") != 0 || !ParseDeviceMatch(argv[3], match)) return Usage();
        if (CheckGameStatus() != GameStatus::Closed) {
            std::fputs("La prueba requiere que el juego este cerrado.\n", stderr);
            return 3;
        }
    }
    wchar_t fullPath[32768]{};
    const DWORD length = GetFullPathNameW(argv[1], sizeof(fullPath) / sizeof(*fullPath), fullPath, nullptr);
    if (!length || length >= sizeof(fullPath) / sizeof(*fullPath)) return Usage();
    const auto sdl = LoadLibraryExW(fullPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!sdl) {
        std::fprintf(stderr, "No se pudo cargar SDL3. Error de Windows: %lu.\n", GetLastError());
        return 2;
    }
    SdlApi api;
    const bool ready = api.Bind(sdl);
    if (!ready) std::fputs("La biblioteca carece de las funciones SDL3 requeridas.\n", stderr);
    const int result = ready ? Probe(api, sendPulse ? &match : nullptr) : 2;
    if (ready) api.quit();
    FreeLibrary(sdl);
    return result;
}
