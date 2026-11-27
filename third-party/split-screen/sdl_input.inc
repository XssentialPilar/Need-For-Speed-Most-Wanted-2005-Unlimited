// The split-screen input code uses the XInput layout. Read the same SDL slots
// as the game's Xidi bridge so PlayStation and generic pads use the same IDs.
struct SDL_Gamepad;

struct SdlPadApi {
    void (__cdecl* lock)();
    void (__cdecl* unlock)();
    SDL_Gamepad* (__cdecl* player)(int);
    bool (__cdecl* connected)(SDL_Gamepad*);
    bool (__cdecl* button)(SDL_Gamepad*, int);
    SHORT (__cdecl* axis)(SDL_Gamepad*, int);
};

static SdlPadApi ReadSdlPadApi(HMODULE module) {
    return {
        reinterpret_cast<decltype(SdlPadApi::lock)>(GetProcAddress(module, "SDL_LockJoysticks")),
        reinterpret_cast<decltype(SdlPadApi::unlock)>(GetProcAddress(module, "SDL_UnlockJoysticks")),
        reinterpret_cast<decltype(SdlPadApi::player)>(GetProcAddress(module, "SDL_GetGamepadFromPlayerIndex")),
        reinterpret_cast<decltype(SdlPadApi::connected)>(GetProcAddress(module, "SDL_GamepadConnected")),
        reinterpret_cast<decltype(SdlPadApi::button)>(GetProcAddress(module, "SDL_GetGamepadButton")),
        reinterpret_cast<decltype(SdlPadApi::axis)>(GetProcAddress(module, "SDL_GetGamepadAxis"))
    };
}

static DWORD WINAPI PollGamepad(DWORD index, XINPUT_STATE* state) {
    if (!state || index >= 4) return ERROR_BAD_ARGUMENTS;
    *state = {};
    const auto module = GetModuleHandleW(L"SDL3.dll");
    if (!module) return ERROR_DEVICE_NOT_CONNECTED;

    static const SdlPadApi sdl = ReadSdlPadApi(module);
    if (!sdl.lock || !sdl.unlock || !sdl.player || !sdl.connected || !sdl.button || !sdl.axis)
        return ERROR_DEVICE_NOT_CONNECTED;

    sdl.lock();
    auto* pad = sdl.player(static_cast<int>(index));
    const bool connected = pad && sdl.connected(pad);
    if (connected) {
        constexpr WORD buttons[] = {
            XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
            XINPUT_GAMEPAD_BACK, 0, XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_LEFT_THUMB,
            XINPUT_GAMEPAD_RIGHT_THUMB, XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
            XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT
        };
        for (int i = 0; i < static_cast<int>(sizeof(buttons) / sizeof(buttons[0])); ++i) {
            if (buttons[i] && sdl.button(pad, i)) state->Gamepad.wButtons |= buttons[i];
        }
        const auto invertY = [](SHORT axis) { return static_cast<SHORT>(std::min(32767, -static_cast<int>(axis))); };
        const auto trigger = [](SHORT axis) { return static_cast<BYTE>(std::max(0, static_cast<int>(axis)) * 255 / 32767); };
        state->Gamepad.sThumbLX = sdl.axis(pad, 0);
        state->Gamepad.sThumbLY = invertY(sdl.axis(pad, 1));
        state->Gamepad.sThumbRX = sdl.axis(pad, 2);
        state->Gamepad.sThumbRY = invertY(sdl.axis(pad, 3));
        state->Gamepad.bLeftTrigger = trigger(sdl.axis(pad, 4));
        state->Gamepad.bRightTrigger = trigger(sdl.axis(pad, 5));
    }
    sdl.unlock();
    return connected ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}
