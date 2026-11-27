#pragma once

#include <cstdint>

namespace discx_codec {

struct Metadata {
    wchar_t title[256] = {};
    wchar_t artist[256] = {};
    wchar_t album[256] = {};
    wchar_t composer[256] = {};
    double duration = 0;
    int sampleRate = 0;
};

struct Stream;
using Open = Stream* (__cdecl*)(const wchar_t*, Metadata*, bool scanOnly);
using OpenForMixer = Stream* (__cdecl*)(const wchar_t*, Metadata*, int mixerRate);
using Artwork = const std::uint8_t* (__cdecl*)(const Stream*, unsigned* bytes);
using Close = void (__cdecl*)(Stream*);
using Read = int (__cdecl*)(Stream*, float*, int frames);
using Seek = bool (__cdecl*)(Stream*, double seconds);

}
