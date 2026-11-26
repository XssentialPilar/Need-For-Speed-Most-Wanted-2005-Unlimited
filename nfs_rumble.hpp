#pragma once

#include <windows.h>
#include <dinput.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace UnlimitedRumble {
namespace Detail {

constexpr std::uintptr_t joystickRva = 0x51f150;
constexpr std::uintptr_t doFfbRva = 0x2ee490;
constexpr std::uintptr_t collisionRva = 0x2f2370;
constexpr std::uintptr_t getSettingsRva = 0x2e99a0;
constexpr std::uintptr_t gameFlowRva = 0x525e90;
std::uintptr_t imageBase = 0;
std::recursive_mutex feedbackMutex;

template<typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

float Unit(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

struct SdlGamepad;
struct SdlJoystick;
struct SdlHaptic;

class MotorOutput {
public:
    bool Open(int playerIndex) {
        Close();
        if (!Load()) return false;
        // Pin the same SDL gamepad assigned by Xidi while its slot table can
        // change on another thread. All calls below use SDL's recursive lock.
        lockJoysticks();
        if (auto* assigned = gamepadFromPlayerIndex(playerIndex)) {
            gamepad = openGamepad(getGamepadID(assigned));
        }
        unlockJoysticks();
        if (!gamepad) return false;
        if (getBooleanProperty(getGamepadProperties(gamepad), "SDL.joystick.cap.rumble", false)) return true;
        if (OpenHaptic()) return true;
        Close();
        return false;
    }

    bool Set(float low, float high, ULONGLONG now) {
        if (!gamepad) return false;
        const auto left = static_cast<std::uint16_t>(Unit(low) * 65535.0f);
        const auto right = static_cast<std::uint16_t>(Unit(high) * 65535.0f);
        const bool active = left || right;
        if (!active && !running) return true;
        if (active && now < refreshAt) return true;
        // Limit updates to 40 Hz. Finite effects expire if rendering or
        // simulation stalls, even before the game can send a stop command.
        refreshAt = now + 25;
        const bool success = haptic ?
            (active ? playHapticRumble(haptic, std::max(low, high), duration) : stopHapticRumble(haptic)) :
            rumbleGamepad(gamepad, left, right, active ? duration : 0);
        running = active && success;
        if (!success) Close();
        return success;
    }

    void Close() {
        if (haptic) {
            if (running) stopHapticRumble(haptic);
            closeHaptic(haptic);
            haptic = nullptr;
        } else if (gamepad && running) {
            rumbleGamepad(gamepad, 0, 0, 0);
        }
        if (gamepad) closeGamepad(gamepad);
        gamepad = nullptr;
        running = false;
        refreshAt = 0;
        checkAt = 0;
    }

    bool IsOpen() const { return gamepad != nullptr; }
    bool IsConnected() const { return gamepad && gamepadConnected(gamepad); }

    bool CheckConnection(ULONGLONG now) {
        if (gamepad && now < checkAt) return true;
        if (!IsConnected()) {
            Close();
            return false;
        }
        checkAt = now + 125;
        return true;
    }

private:
    static constexpr std::uint32_t duration = 125;
    static constexpr std::uint32_t initHaptic = 0x00001000u;

    bool Load() {
        if (loaded) return true;
        const auto sdl = GetModuleHandleW(L"SDL3.dll");
        if (!sdl) return false;
#define UNLIMITED_SDL(member, name) member = reinterpret_cast<decltype(member)>(GetProcAddress(sdl, "SDL_" #name))
        UNLIMITED_SDL(lockJoysticks, LockJoysticks);
        UNLIMITED_SDL(unlockJoysticks, UnlockJoysticks);
        UNLIMITED_SDL(gamepadFromPlayerIndex, GetGamepadFromPlayerIndex);
        UNLIMITED_SDL(getGamepadID, GetGamepadID);
        UNLIMITED_SDL(openGamepad, OpenGamepad);
        UNLIMITED_SDL(closeGamepad, CloseGamepad);
        UNLIMITED_SDL(gamepadConnected, GamepadConnected);
        UNLIMITED_SDL(getGamepadProperties, GetGamepadProperties);
        UNLIMITED_SDL(getBooleanProperty, GetBooleanProperty);
        UNLIMITED_SDL(rumbleGamepad, RumbleGamepad);
        UNLIMITED_SDL(initSubSystem, InitSubSystem);
        UNLIMITED_SDL(isMainThread, IsMainThread);
        UNLIMITED_SDL(getGamepadJoystick, GetGamepadJoystick);
        UNLIMITED_SDL(openHaptic, OpenHapticFromJoystick);
        UNLIMITED_SDL(closeHaptic, CloseHaptic);
        UNLIMITED_SDL(initHapticRumble, InitHapticRumble);
        UNLIMITED_SDL(playHapticRumble, PlayHapticRumble);
        UNLIMITED_SDL(stopHapticRumble, StopHapticRumble);
#undef UNLIMITED_SDL
        loaded = lockJoysticks && unlockJoysticks && gamepadFromPlayerIndex && getGamepadID &&
                 openGamepad && closeGamepad && gamepadConnected && getGamepadProperties &&
                 getBooleanProperty && rumbleGamepad;
        return loaded;
    }

    bool OpenHaptic() {
        if (!initSubSystem || !isMainThread || !getGamepadJoystick || !openHaptic || !closeHaptic ||
            !initHapticRumble || !playHapticRumble || !stopHapticRumble) return false;
        if (!hapticsInitialized && isMainThread()) hapticsInitialized = initSubSystem(initHaptic);
        if (!hapticsInitialized) return false;
        haptic = openHaptic(getGamepadJoystick(gamepad));
        return haptic && initHapticRumble(haptic);
    }

    void (__cdecl* lockJoysticks)() = nullptr;
    void (__cdecl* unlockJoysticks)() = nullptr;
    SdlGamepad* (__cdecl* gamepadFromPlayerIndex)(int) = nullptr;
    std::uint32_t (__cdecl* getGamepadID)(SdlGamepad*) = nullptr;
    SdlGamepad* (__cdecl* openGamepad)(std::uint32_t) = nullptr;
    void (__cdecl* closeGamepad)(SdlGamepad*) = nullptr;
    bool (__cdecl* gamepadConnected)(SdlGamepad*) = nullptr;
    std::uint32_t (__cdecl* getGamepadProperties)(SdlGamepad*) = nullptr;
    bool (__cdecl* getBooleanProperty)(std::uint32_t, const char*, bool) = nullptr;
    bool (__cdecl* rumbleGamepad)(SdlGamepad*, std::uint16_t, std::uint16_t, std::uint32_t) = nullptr;
    bool (__cdecl* initSubSystem)(std::uint32_t) = nullptr;
    bool (__cdecl* isMainThread)() = nullptr;
    SdlJoystick* (__cdecl* getGamepadJoystick)(SdlGamepad*) = nullptr;
    SdlHaptic* (__cdecl* openHaptic)(SdlJoystick*) = nullptr;
    void (__cdecl* closeHaptic)(SdlHaptic*) = nullptr;
    bool (__cdecl* initHapticRumble)(SdlHaptic*) = nullptr;
    bool (__cdecl* playHapticRumble)(SdlHaptic*, float, std::uint32_t) = nullptr;
    bool (__cdecl* stopHapticRumble)(SdlHaptic*) = nullptr;
    SdlGamepad* gamepad = nullptr;
    SdlHaptic* haptic = nullptr;
    bool loaded = false;
    bool hapticsInitialized = false;
    bool running = false;
    ULONGLONG refreshAt = 0;
    ULONGLONG checkAt = 0;
};

struct DrivingFeedback {
    void** vtable;
    MotorOutput motors;
    float road = 0.0f, slip = 0.0f, skid = 0.0f, rpm = 0.0f;
    bool nitrous = false, gearShift = false, damage = false;
    bool previousShift = false, previousDamage = false;
    float impulse = 0.0f;
    ULONGLONG impulseUntil = 0;
    IDirectInputDevice8A* gameDevice = nullptr;
    ULONGLONG retryAt = 0;

    void ClearFrame() {
        road = slip = skid = rpm = 0.0f;
        nitrous = gearShift = damage = false;
    }

    void Stop() {
        ClearFrame();
        impulse = 0.0f;
        impulseUntil = 0;
        previousShift = previousDamage = false;
        motors.Set(0.0f, 0.0f, GetTickCount64());
    }

    void Jolt(float strength, ULONGLONG now) {
        const float remaining = impulseUntil > now ? impulse * Unit(float(impulseUntil - now) / 180.0f) : 0.0f;
        if (strength <= remaining || strength <= 0) return;
        impulse = Unit(strength);
        impulseUntil = now + 180;
    }

    void Mix(ULONGLONG now) {
        if (gearShift && !previousShift) Jolt(0.26f, now);
        if (damage && !previousDamage) Jolt(0.6f, now);
        previousShift = gearShift;
        previousDamage = damage;
        const float hit = impulseUntil > now ? impulse * Unit(float(impulseUntil - now) / 180.0f) : 0.0f;
        const float low = std::max({hit, road * 0.5f, slip * 0.46f, nitrous ? 0.3f : 0.0f});
        const float high = std::max({hit * 0.58f, road * 0.26f, skid * 0.55f, rpm,
                                     nitrous ? 0.22f : 0.0f});
        motors.Set(low, high, now);
    }
};

void* __fastcall KeepFeedback(DrivingFeedback* self, void*, unsigned) { return self; }
void __fastcall Pause(DrivingFeedback* self) { self->Stop(); }
// The PC build passes the engine interface to this slot on the stack.
void __fastcall Resume(DrivingFeedback*, void*, void*) {}
void __fastcall Begin(DrivingFeedback* self) { self->ClearFrame(); }
void __fastcall End(DrivingFeedback* self) { self->Mix(GetTickCount64()); }

void __fastcall Road(DrivingFeedback* self, void*, int, void* surface, float speed) {
    if (!surface) return;
    const auto* layout = Field<const std::uint8_t*>(surface, 8);
    if (!layout) return;
    self->road = std::max(self->road, Unit(speed / 36.0f) * Unit(float(layout[0xfa]) / 100.0f));
}

void __fastcall Skid(DrivingFeedback* self, void*, int, void*, float magnitude) {
    self->skid = std::max(self->skid, Unit((magnitude - 1.5f) / 12.0f));
}

void __fastcall Slip(DrivingFeedback* self, void*, int, void*, float magnitude) {
    self->slip = std::max(self->slip, Unit((magnitude - 1.5f) / 12.0f));
}

void __fastcall Engine(DrivingFeedback* self, void*, float powerband, float, float throttle) {
    self->rpm = Unit((powerband - 0.72f) / 0.28f) * Unit(throttle) * 0.14f;
}

void __fastcall ShiftPotential(DrivingFeedback*, void*, int) {}
void __fastcall Nitrous(DrivingFeedback* self, void*, int active, float) { self->nitrous = (active & 0xff) != 0; }
void __fastcall Damage(DrivingFeedback* self, void*, int blown) { self->damage = (blown & 0xff) != 0; }
void __fastcall GearShift(DrivingFeedback* self, void*, int shift) { self->gearShift = (shift & 0xff) != 0; }

void __fastcall Collision(DrivingFeedback* self, void*, const void* info, int playerIsA) {
    if (!info) return;
    const auto* bytes = static_cast<const std::uint8_t*>(info);
    float speedChange{};
    std::memcpy(&speedChange, bytes + ((playerIsA & 0xff) ? 0x5c : 0x6c), sizeof(speedChange));
    self->Jolt(Unit((std::fabs(speedChange) - 1.0f) / 12.0f), GetTickCount64());
}

void* feedbackTable[] = {
    reinterpret_cast<void*>(&KeepFeedback), reinterpret_cast<void*>(&Pause), reinterpret_cast<void*>(&Resume),
    reinterpret_cast<void*>(&Pause), reinterpret_cast<void*>(&Begin), reinterpret_cast<void*>(&End),
    reinterpret_cast<void*>(&Road), reinterpret_cast<void*>(&Skid), reinterpret_cast<void*>(&Slip),
    reinterpret_cast<void*>(&Engine), reinterpret_cast<void*>(&ShiftPotential), reinterpret_cast<void*>(&Nitrous),
    reinterpret_cast<void*>(&Damage), reinterpret_cast<void*>(&GearShift), reinterpret_cast<void*>(&Collision),
};
DrivingFeedback feedback{feedbackTable, {}};

bool HasFocus() {
    static HWND previousWindow = nullptr;
    static bool wasFocused = false;
    const HWND window = GetForegroundWindow();
    if (window == previousWindow) return wasFocused;
    previousWindow = window;
    DWORD process = 0;
    if (window) GetWindowThreadProcessId(window, &process);
    wasFocused = process == GetCurrentProcessId();
    return wasFocused;
}

bool UsePrimaryPad(void* player) {
    // The PC controller bindings target the first enumerated gamepad.
    // Steering wheels and other native DirectInput devices keep their own path.
    if (Field<const int>(player, 0x64) != 0 ||
        *reinterpret_cast<const unsigned*>(imageBase + gameFlowRva) != 6) return false;
    if (!HasFocus()) return false;
    auto* joystick = *reinterpret_cast<void**>(imageBase + joystickRva);
    if (!joystick) return false;
    auto* device = Field<IDirectInputDevice8A*>(joystick, 0x218);
    if (!device) return false;
    const auto now = GetTickCount64();
    if (device == feedback.gameDevice && feedback.motors.CheckConnection(now)) return true;
    if (now < feedback.retryAt) return false;
    feedback.retryAt = now + 750;
    DIDEVICEINSTANCEA info{};
    info.dwSize = sizeof(info);
    if (FAILED(device->GetDeviceInfo(&info)) || std::memcmp(info.guidInstance.Data4, "XIDIVC", 6) != 0) return false;
    const auto xidiIndex = static_cast<int>(info.guidInstance.Data1 >> 16) - 1;
    if (xidiIndex < 0 || xidiIndex >= 16) return false;
    const bool opened = feedback.motors.Open(xidiIndex);
    if (opened) feedback.gameDevice = device;
    return opened;
}

void __fastcall DoFeedback(void* player) {
    const std::lock_guard<std::recursive_mutex> lock(feedbackMutex);
    using Native = void (__thiscall*)(void*);
    const auto native = reinterpret_cast<Native>(imageBase + doFfbRva);
    if (!UsePrimaryPad(player)) {
        feedback.Stop();
        return native(player);
    }
    auto& current = Field<void*>(player, 0x58);
    auto& wheel = Field<void*>(player, 0x5c);
    void* const original = current;
    void* const originalWheel = wheel;
    current = &feedback;
    wheel = nullptr;
    native(player);
    wheel = originalWheel;
    current = original;
}

void __fastcall HandleCollision(void* object, void*, const void* collision) {
    const std::lock_guard<std::recursive_mutex> lock(feedbackMutex);
    using Native = void (__thiscall*)(void*, const void*);
    const auto native = reinterpret_cast<Native>(imageBase + collisionRva);
    auto* player = static_cast<std::uint8_t*>(object) - 0x54;
    if (!UsePrimaryPad(player)) return native(object, collision);
    using Settings = std::uint8_t* (__thiscall*)(void*);
    const auto get = reinterpret_cast<Settings>(imageBase + getSettingsRva);
    auto* settings = get(player + 0x4c);
    if (!settings) return native(object, collision);
    auto& current = Field<void*>(player, 0x58);
    void* const original = current;
    // The PC menu cannot enable the stock collision-report flag. Set it
    // only across this call when Unlimited vibration is active.
    const auto originalVibration = settings[4];
    settings[4] = 1;
    current = &feedback;
    native(object, collision);
    current = original;
    settings[4] = originalVibration;
}

bool Write(std::uintptr_t address, const void* replacement, std::size_t length) {
    auto* dest = reinterpret_cast<void*>(address);
    DWORD old;
    if (!VirtualProtect(dest, length, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(dest, replacement, length);
    FlushInstructionCache(GetCurrentProcess(), dest, length);
    DWORD ignored;
    VirtualProtect(dest, length, old, &ignored);
    return true;
}

} // namespace Detail

inline bool Install(HMODULE executable, const wchar_t* settingsFile) {
    using namespace Detail;
    if (!executable) return false;
    if (settingsFile && *settingsFile &&
        !GetPrivateProfileIntW(L"Controller", L"Vibration", 1, settingsFile)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(executable);
    if (imageBase) return imageBase == base;
    constexpr std::uintptr_t ffbCall = 0x2f22c6;
    constexpr std::uintptr_t collisionSlots[] = {0x4b0b08, 0x4b0bcc};
    constexpr std::uint8_t originalBranch[] = {0xe8,0xc5,0xc1,0xff,0xff};
    const auto originalCollision = base + collisionRva;
    if (std::memcmp(reinterpret_cast<const void*>(base + ffbCall), originalBranch, sizeof(originalBranch)) != 0)
        return false;
    for (const auto slot : collisionSlots) {
        if (*reinterpret_cast<const std::uintptr_t*>(base + slot) != originalCollision) return false;
    }
    const auto route = reinterpret_cast<std::uintptr_t>(&HandleCollision);
    std::uint8_t call[] = {0xe8,0,0,0,0};
    const auto rel = reinterpret_cast<std::uintptr_t>(&DoFeedback) - (base + ffbCall + sizeof(call));
    std::memcpy(call + 1, &rel, sizeof(rel));
    imageBase = base;
    if (Write(base + ffbCall, call, sizeof(call)) &&
        Write(base + collisionSlots[0], &route, sizeof(route)) &&
        Write(base + collisionSlots[1], &route, sizeof(route))) return true;
    for (const auto slot : collisionSlots) Write(base + slot, &originalCollision, sizeof(originalCollision));
    Write(base + ffbCall, originalBranch, sizeof(originalBranch));
    imageBase = 0;
    return false;
}

inline void Service() {
    using namespace Detail;
    if (!imageBase) return;
    const std::lock_guard<std::recursive_mutex> lock(feedbackMutex);
    if (!feedback.motors.IsOpen()) return;
    if (*reinterpret_cast<const unsigned*>(imageBase + gameFlowRva) == 6 && HasFocus()) return;
    feedback.Stop();
    feedback.motors.Close();
}

} // namespace UnlimitedRumble
