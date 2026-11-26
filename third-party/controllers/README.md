# Controller support in Unlimited

The game reads controllers through DirectInput 8. The DLLs in this folder use SDL3 to expose a DirectInput device with analog steering, a D-pad, and independent triggers. The `nfs_controller.hpp` module adds the default bindings to the 1.3 game and preserves keyboard behavior and saved bindings.

- `dinput8.dll` and `Xidi.32.dll` come from the Win32 build of Xidi-SDL 1.0.1. [Release and source code](https://github.com/ProjectXsent/Xidi-SDL/releases/tag/Xidi-SDL3-v1.0.1)
- `SDL3.dll` comes from SDL 3.4.20. [Source code](https://github.com/libsdl-org/SDL/tree/release-3.4.20)
- `Xidi.ini` assigns each trigger its own axis and centers the resting position where Most Wanted expects it.

Windows must recognize the controller. An original DualShock 3 requires a compatible device driver, such as DsHidMini on Windows 10 or 11. Unlimited uses the input the device exposes to Windows. [DsHidMini documentation](https://docs.nefarius.at/projects/DsHidMini/v3/)

The console tests used the libraries delivered by Setup. The input test verified steering, triggers, D-pad input, buttons, and disconnection through DirectInput. The vibration test verified independent motor strengths, collision feedback, expiry, stopping, and reconnection with virtual DualShock 3, DualShock 4, DualSense, Xbox 360, Xbox One, Xbox Series, and generic SDL devices. Testing with physical controllers is pending.
