#pragma once

namespace {

// The front end draws its cursor in a 640 x 480 coordinate system. Reading
// the dimensions from PC_CURSOR makes its quad grow when the atlas is upgraded.
bool InstallHdCursorScale() {
    struct Site {
        std::uintptr_t rva;
        std::uint8_t original[6];
        std::uint8_t fixed[6];
    };
    // Replace each signed texel-size load with xor reg,reg / mov lowbyte,16.
    // The original add then computes the edge and sets its usual flags.
    constexpr Site sites[] = {
        {0x17045c, {0x0f,0xbf,0x4e,0x44,0x03,0xcf}, {0x31,0xc9,0xb1,16,0x03,0xcf}},
        {0x170481, {0x0f,0xbf,0x46,0x44,0x03,0xc7}, {0x31,0xc0,0xb0,16,0x03,0xc7}},
        {0x170496, {0x0f,0xbf,0x4e,0x46,0x03,0xcb}, {0x31,0xc9,0xb1,16,0x03,0xcb}},
        {0x1704bf, {0x0f,0xbf,0x46,0x46,0x03,0xc3}, {0x31,0xc0,0xb0,16,0x03,0xc3}},
    };
    bool original = true, installed = true;
    for (const auto& site : sites) {
        original &= MatchGameBytes(site.rva, site.original, sizeof(site.original));
        installed &= MatchGameBytes(site.rva, site.fixed, sizeof(site.fixed));
    }
    if (installed || !original) return installed;

    // All four instructions occupy one code page. Acquire write access once,
    // before the front end starts, so a failed preflight cannot install half a quad.
    auto* begin = &GameValue<std::uint8_t>(sites[0].rva);
    constexpr auto size = sites[3].rva + sizeof(sites[3].fixed) - sites[0].rva;
    DWORD oldProtection, unused;
    if (!VirtualProtect(begin, size, PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
    for (const auto& site : sites)
        std::memcpy(&GameValue<std::uint8_t>(site.rva), site.fixed, 4);
    if (FlushInstructionCache(GetCurrentProcess(), begin, size) &&
        VirtualProtect(begin, size, oldProtection, &unused)) return true;

    for (const auto& site : sites)
        std::memcpy(&GameValue<std::uint8_t>(site.rva), site.original, 4);
    FlushInstructionCache(GetCurrentProcess(), begin, size);
    VirtualProtect(begin, size, oldProtection, &unused);
    return false;
}

}
