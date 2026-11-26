#pragma once

namespace {
int __cdecl PrintSplashBrand(const char* package, int hash, const char*, const char*) {
    using Print = int (__cdecl*)(const char*, int, const char*, ...);
    using Find = std::uint8_t* (__cdecl*)(const char*, unsigned);
    const int result = GameFunction<Print>(0x1252d0)(package, hash, "%s", "Unlimited");
    auto* object = result == 0 ? GameFunction<Find>(0x124850)(package, hash) : nullptr;
    if (object && *reinterpret_cast<unsigned*>(object + 0x18) == 2)
        *reinterpret_cast<unsigned*>(object + 0x1c) |= 0x400000u;
    if (LogFile log; log.file)
        std::fprintf(log.file, "Splash: package=%s label=Unlimited object=%u result=%d\n",
                     package ? package : "", object != nullptr, result);
    return result;
}

bool InstallSplashBrand() {
    // Only this splash-screen label uses our project name. The engine also
    // queries its retail version when initializing the multiplayer lobby.
    constexpr std::uint8_t arguments[] = {0x68, 0xd7, 0x31, 0xef, 0x69, 0x52};
    return MatchGameBytes(0x1a3272, arguments, sizeof(arguments)) &&
           RedirectCall(0x1a3278, 0x1252d0, reinterpret_cast<std::uintptr_t>(&PrintSplashBrand));
}
}
