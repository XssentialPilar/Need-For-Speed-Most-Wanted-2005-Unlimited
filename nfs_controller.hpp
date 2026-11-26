#pragma once

#include <windows.h>
#include <dinput.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace UnlimitedController {
namespace Detail {

enum class BindingType : std::int32_t {
    None = 0, Button = 2, Pov = 3, AxisLow = 4, AxisHigh = 5, RotationHigh = 7,
};

struct Binding {
    BindingType type;
    std::int32_t device;
    std::int32_t index;
};

struct ActionEntry {
    const char* name;
    Binding keyboard[2];
    Binding joystick[2];
};

static_assert(sizeof(ActionEntry) == 0x34);

constexpr Binding Button(int index) { return {BindingType::Button, 0, index}; }
constexpr Binding Dpad(int direction) { return {BindingType::Pov, 0, direction}; }
constexpr Binding AxisLow(int axis) { return {BindingType::AxisLow, 0, axis}; }
constexpr Binding AxisHigh(int axis) { return {BindingType::AxisHigh, 0, axis}; }
constexpr int stickX = 0, stickY = 1, triggerAxis = 2;
constexpr Binding leftTrigger = AxisHigh(triggerAxis);
constexpr Binding rightTrigger{BindingType::RotationHigh, 0, triggerAxis};

enum PadButton { South, East, West, North, LeftShoulder, RightShoulder, Select, Start, LeftStick, RightStick };
enum Direction { Up, Right, Down, Left };

enum Action {
    Accelerate = 0, Brake = 1, SteerLeft = 2, SteerRight = 3, Handbrake = 4, Speedbreaker = 5, Nitrous = 6,
    ShiftDown = 7, ShiftUp = 8, ResetCar = 9, EnterEvent = 10, HudDown = 11, HudLeft = 12, HudRight = 13,
    ChangeCamera = 15, LookBack = 16, PullCameraBack = 17, Pause = 18, MenuLeft = 19, MenuRight = 20,
    MenuUp = 21, MenuDown = 22, SkipCutscene = 23, MenuRightTrigger = 29, MenuLeftTrigger = 30,
    MenuAccept = 32, MenuBack = 33, MenuStart = 34, MenuPrevious = 35, MenuNext = 36,
    MenuKey3 = 37, MenuKey4 = 39, MenuKey2 = 40, MenuKey1 = 41,
};

void ApplyBindings(ActionEntry* actions) {
    const auto bind = [actions](Action action, Binding primary, Binding secondary = {}) {
        actions[action].joystick[0] = primary;
        actions[action].joystick[1] = secondary;
    };

    bind(Accelerate, rightTrigger);
    bind(Brake, leftTrigger);
    bind(SteerLeft, AxisLow(stickX), Dpad(Left));
    bind(SteerRight, AxisHigh(stickX), Dpad(Right));
    bind(Handbrake, Button(South));
    bind(Speedbreaker, Button(West));
    bind(Nitrous, Button(East));
    bind(ShiftDown, Button(LeftShoulder));
    bind(ShiftUp, Button(RightShoulder));
    bind(ResetCar, Button(Select));
    bind(EnterEvent, Dpad(Up));
    bind(HudDown, Dpad(Down));
    bind(HudLeft, {});
    bind(HudRight, {});
    bind(ChangeCamera, Button(North));
    bind(LookBack, Button(LeftStick));
    bind(PullCameraBack, Button(RightStick));
    bind(Pause, Button(Start));
    bind(SkipCutscene, Button(South), Button(Start));
    bind(MenuLeft, Dpad(Left), AxisLow(stickX));
    bind(MenuRight, Dpad(Right), AxisHigh(stickX));
    bind(MenuUp, Dpad(Up), AxisLow(stickY));
    bind(MenuDown, Dpad(Down), AxisHigh(stickY));
    bind(MenuRightTrigger, rightTrigger);
    bind(MenuLeftTrigger, leftTrigger);
    bind(MenuAccept, Button(South));
    bind(MenuBack, Button(East));
    bind(MenuStart, Button(Start));
    bind(MenuPrevious, Button(LeftShoulder));
    bind(MenuNext, Button(RightShoulder));
    bind(MenuKey3, Button(Select));
    bind(MenuKey4, Button(Start));
    bind(MenuKey2, Button(West));
    bind(MenuKey1, Button(North));
}

constexpr std::uintptr_t defaultsRva = 0x23cd80;
constexpr std::uintptr_t firstJoystickRva = 0x51f150;
constexpr std::uintptr_t actionTableRva = 0x51f418;
std::uintptr_t imageBase = 0;

void __fastcall SetControllerDefaults(void* input, void*) {
    using Defaults = void (__thiscall*)(void*);
    reinterpret_cast<Defaults>(imageBase + defaultsRva)(input);

    const auto joystick = *reinterpret_cast<const std::uintptr_t*>(imageBase + firstJoystickRva);
    if (!joystick) return;
    auto* device = *reinterpret_cast<IDirectInputDevice8A**>(joystick + 0x218);
    if (!device) return;

    DIDEVICEINSTANCEA details{};
    details.dwSize = sizeof(details);
    if (FAILED(device->GetDeviceInfo(&details)) ||
        std::memcmp(details.guidInstance.Data4, "XIDIVC", 6) != 0) return;

    ApplyBindings(reinterpret_cast<ActionEntry*>(imageBase + actionTableRva));
}

struct Branch {
    std::uintptr_t rva;
    std::array<std::uint8_t, 5> original;
};

constexpr Branch defaultsCallers[] = {
    {0x11d702, {0xe8, 0x79, 0xf6, 0x11, 0x00}},
    {0x16e27d, {0xe8, 0xfe, 0xea, 0x0c, 0x00}},
    {0x23f976, {0xe9, 0x05, 0xd4, 0xff, 0xff}},
    {0x23f992, {0xe9, 0xe9, 0xd3, 0xff, 0xff}},
};

bool WriteBranch(std::uintptr_t address, const std::array<std::uint8_t, 5>& bytes) {
    auto* destination = reinterpret_cast<void*>(address);
    DWORD previous = 0;
    if (!VirtualProtect(destination, bytes.size(), PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(destination, bytes.data(), bytes.size());
    FlushInstructionCache(GetCurrentProcess(), destination, bytes.size());
    DWORD restored = 0;
    VirtualProtect(destination, bytes.size(), previous, &restored);
    return true;
}

} // namespace Detail

inline bool Install(HMODULE executable) {
    using namespace Detail;
    if (!executable) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(executable);
    if (imageBase) return imageBase == base;

    constexpr std::uint8_t defaultsSignature[] = {0xa1, 0x50, 0xf1, 0x91, 0, 0x81, 0xec, 0x40, 2, 0, 0};
    if (std::memcmp(reinterpret_cast<const void*>(base + defaultsRva),
                    defaultsSignature, sizeof(defaultsSignature)) != 0) return false;
    for (const auto& branch : defaultsCallers) {
        if (std::memcmp(reinterpret_cast<const void*>(base + branch.rva),
                        branch.original.data(), branch.original.size()) != 0) return false;
    }

    imageBase = base;
    std::size_t applied = 0;
    for (const auto& branch : defaultsCallers) {
        auto replacement = branch.original;
        const auto displacement = reinterpret_cast<std::uintptr_t>(&SetControllerDefaults) -
                                  (base + branch.rva + replacement.size());
        std::memcpy(replacement.data() + 1, &displacement, sizeof(displacement));
        if (!WriteBranch(base + branch.rva, replacement)) {
            while (applied) {
                const auto& previous = defaultsCallers[--applied];
                WriteBranch(base + previous.rva, previous.original);
            }
            imageBase = 0;
            return false;
        }
        ++applied;
    }
    return true;
}

} // namespace UnlimitedController
