#pragma once
#include "renderer-lab/scene_movie_d3d12.hpp"
#include "renderer-lab/movie_reconstruction_d3d9.hpp"

namespace {

constexpr std::uintptr_t movieTextureRva = 0x583e58;
constexpr std::uintptr_t movieTextureInfoRva = 0x51cb28;
constexpr std::uintptr_t movieFrameStorageRva = 0x582d18;
constexpr std::uintptr_t movieDrawOriginal = 0x191460;
constexpr std::uintptr_t movieUploadOriginal = 0x2caec0;
constexpr std::uintptr_t movieStreamOpenOriginal = 0x0b2ed0;
constexpr std::uintptr_t movieFileExistsOriginal = 0x25f7a0;
scene_movie::GameMovie& nativeMoviePass = *new scene_movie::GameMovie;
bool nativeMovieInstalled = false;
thread_local const void* nativeFullscreenMovieObject = nullptr;
thread_local bool drawingFullscreenMovie = false;

void __fastcall RenderMovieObjects(void* object, void*) {
    const bool previous = drawingFullscreenMovie;
    drawingFullscreenMovie = object && object == nativeFullscreenMovieObject;
    GameFunction<void (__thiscall*)(void*)>(0x182c70)(object);
    drawingFullscreenMovie = previous;
    nativeFullscreenMovieObject = nullptr;
}

HRESULT WINAPI DrawMovieFan(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT count,
                            const void* vertices, UINT stride) {
    if (drawingFullscreenMovie) {
        auto* texture = GameValue<IDirect3DTexture9*>(movieTextureRva);
        if (nativePassesActive && nativeMoviePass.Draw(device, texture, type, count, vertices, stride)) {
            lastMovieApi.store(nativePassesAtStartup);
            return D3D_OK;
        }
        lastMovieApi.store(9);
        static HRESULT reportedError{};
        static char reportedReason[192]{};
        const char* reason = nativeMoviePass.LastErrorText();
        const HRESULT error = nativeMoviePass.LastError();
        if (nativePassesActive && (error != reportedError || std::strcmp(reportedReason, reason) != 0)) {
            reportedError = error;
            std::snprintf(reportedReason, sizeof(reportedReason), "%s", reason);
            if (LogFile log; log.file)
                std::fprintf(log.file, "Movie: native API %u deferred (HRESULT=%08lx, %s); using D3D9 for this draw\n",
                             nativePassesAtStartup, static_cast<unsigned long>(error), reason);
        }
        HRESULT result{};
        if (movie_reconstruction::Draw(device, texture, type, count, vertices, stride, result)) return result;
    }
    return device->DrawPrimitiveUP(type, count, vertices, stride);
}

bool InstallNativeMoviePass() {
    constexpr std::uintptr_t submit = 0x19a04d, fan = 0x2d18ce;
    constexpr std::uint8_t originalFan[]{0xff, 0x92, 0x4c, 0x01, 0x00, 0x00};
    if (!MatchGameBytes(fan, originalFan, sizeof(originalFan))) return false;
    std::array<std::uint8_t, 5> oldSubmit{};
    std::memcpy(oldSubmit.data(), &GameValue<std::uint8_t>(submit), oldSubmit.size());
    if (!RedirectCall(submit, 0x182c70, reinterpret_cast<std::uintptr_t>(&RenderMovieObjects))) return false;
    if (!InstallInlineCall(fan, originalFan, reinterpret_cast<void(*)()>(&DrawMovieFan))) {
        WriteGameBytes(submit, oldSubmit.data(), oldSubmit.size());
        return false;
    }
    return true;
}

struct MovieFrameSize {
    std::uint16_t width;
    std::uint16_t height;
};

MovieFrameSize ReadMovieFrameSize(const void* textureInfo) {
    MovieFrameSize size{};
    std::memcpy(&size, static_cast<const std::uint8_t*>(textureInfo) + 0x44, sizeof(size));
    return size;
}

struct MovieQuadFit {
    float halfWidth;
    float halfHeight;
};

MovieQuadFit FitMovieQuad(float objectWidth, float objectHeight, float pictureAspect,
                          float boxWidth, float boxHeight) {
    const float width = std::min(boxWidth, boxHeight * pictureAspect);
    return {0.5f * width / objectWidth, 0.5f * width / (objectHeight * pictureAspect)};
}

void __fastcall DrawMovieQuad(const std::uint8_t* object, void*, float x0, float y0,
                               float x1, float y1, float z, float u0, float v0, float u1,
                               float v1, const void* colors, DWORD material, void* package) {
    nativeFullscreenMovieObject = nullptr;
    const auto size = ReadMovieFrameSize(&GameValue<std::uint8_t>(movieTextureInfoRva));
    auto* texture = GameValue<IDirect3DTexture9*>(movieTextureRva);
    D3DSURFACE_DESC allocation{};
    float transform[6];
    std::memcpy(transform, object + 0x30, sizeof(transform));
    const float objectWidth = std::fabs(transform[0]);
    const float objectHeight = std::fabs(transform[5]);
    if (texture && size.width && size.height && objectWidth >= 1.0f && objectHeight >= 1.0f &&
        SUCCEEDED(texture->GetLevelDesc(0, &allocation)) && allocation.Width >= size.width &&
        allocation.Height >= size.height) {
        const bool fullScreen = objectWidth >= 600.0f && objectHeight >= 450.0f;
        const float boxWidth = fullScreen ? 640.0f / activeLayout.hudX : objectWidth;
        const float boxHeight = fullScreen ? 480.0f / activeLayout.hudY : objectHeight;
        // Stock VP6 is anamorphic. Retain the complete authored frame, including
        // any embedded letterbox. Square-pixel HD and FHD replacements use their own ratio.
        const bool stockFrame = size.width == 1024 && size.height == 512;
        const float pictureAspect = stockFrame ? (fullScreen ? referenceAspect : objectWidth / objectHeight)
                                                : static_cast<float>(size.width) / size.height;
        const MovieQuadFit fit = FitMovieQuad(objectWidth, objectHeight, pictureAspect, boxWidth, boxHeight);
        x0 = -fit.halfWidth;
        y0 = -fit.halfHeight;
        x1 = fit.halfWidth;
        y1 = fit.halfHeight;

        // The engine assumes a power-of-two allocation. D3D9 can allocate the exact size.
        u1 = static_cast<float>(size.width) / allocation.Width;
        const float vEnd = static_cast<float>(size.height) / allocation.Height;
        const bool flip = v0 > v1;
        v0 = flip ? vEnd : 0.0f;
        v1 = flip ? 0.0f : vEnd;

        if (fullScreen) {
            if (nativeMovieInstalled) nativeFullscreenMovieObject = object;
            if (auto* device = GameValue<IDirect3DDevice9*>(0x582bdc)) {
                device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
            }
        }
    }

    using Draw = void (__thiscall*)(const void*, float, float, float, float, float, float, float,
                                    float, float, const void*, DWORD, void*);
    GameFunction<Draw>(movieDrawOriginal)(object, x0, y0, x1, y1, z, u0, v0, u1, v1, colors, material, package);
}

void CopyMovieRows(std::uint8_t* destination, unsigned pitch, const std::uint8_t* source,
                   unsigned rowBytes, unsigned height) {
    if (pitch == rowBytes) {
        std::memcpy(destination, source, static_cast<std::size_t>(rowBytes) * height);
        return;
    }
    for (unsigned row = 0; row < height; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(row) * pitch,
                    source + static_cast<std::size_t>(row) * rowBytes, rowBytes);
    }
}

void __fastcall UploadMovieFrame(void* player, void*, void*, const void* textureInfo, void* frame) {
    if (nativeMovieInstalled && nativePassesActive) nativeMoviePass.Invalidate();
    auto* texture = GameValue<IDirect3DTexture9*>(movieTextureRva);
    auto* storage = GameValue<std::uint8_t*>(movieFrameStorageRva);
    if (!texture || !storage || !textureInfo || !frame) return;

    const auto size = ReadMovieFrameSize(textureInfo);
    const unsigned rowBytes = size.width * 4u;
    std::uint32_t frameBytes, payload;
    std::memcpy(&frameBytes, static_cast<const std::uint8_t*>(textureInfo) + 0x40, sizeof(frameBytes));
    std::memcpy(&payload, storage + 8, sizeof(payload));
    if (!size.width || !size.height || !payload || frameBytes != std::uint64_t{rowBytes} * size.height) return;

    D3DSURFACE_DESC allocation{};
    if (FAILED(texture->GetLevelDesc(0, &allocation)) || allocation.Width < size.width ||
        allocation.Height < size.height || (allocation.Format != D3DFMT_A8R8G8B8 &&
          allocation.Format != D3DFMT_X8R8G8B8)) return;

    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) return;
    if (locked.pBits && locked.Pitch >= static_cast<INT>(rowBytes)) {
        using ConvertFrame = void (__thiscall*)(void*, void*, void*, std::uint32_t);
        GameFunction<ConvertFrame>(0x1176d0)(player, frame, storage, GameValue<std::uint32_t>(0x4faf18));
        CopyMovieRows(static_cast<std::uint8_t*>(locked.pBits), static_cast<unsigned>(locked.Pitch),
                      storage + payload, rowBytes, size.height);
        if (nativeMovieInstalled && nativePassesActive)
            nativeMoviePass.Update(texture, storage + payload, size.width, size.height);
    }
    texture->UnlockRect(0);
}

bool ReadMovieContainer(FILE* file, bool pal, bool requireHd, MovieFrameSize& size) {
    struct Header {
        char magic[4];
        std::uint32_t headerBytes;
        char codec[4];
        MovieFrameSize size;
        std::uint32_t frames;
        std::uint32_t maxVideoPacket;
        std::uint32_t rateNumerator;
        std::uint32_t rateDenominator;
    } header{};
    static_assert(sizeof(Header) == 32);
    if (std::fread(&header, sizeof(header), 1, file) != 1 || std::memcmp(header.magic, "MVhd", 4) != 0 ||
        std::memcmp(header.codec, "06PV", 4) != 0 || header.headerBytes != sizeof(header) ||
        !header.frames || !header.maxVideoPacket || !header.rateDenominator) return false;
    size = header.size;
    const bool hd = (size.width == 1280 && size.height == 720) ||
                    (size.width == 1920 && (size.height == 1080 || size.height == 1088));
    if (!hd && (requireHd || size.width != 1024 || size.height != 512)) return false;
    const float expectedRate = pal ? 25.0f : 30000.0f / 1001.0f;
    if (std::fabs(static_cast<float>(header.rateNumerator) / header.rateDenominator - expectedRate) > 0.1f) return false;

    // The legacy player allocates from the advertised metadata. Verify the
    // whole packet table and check the dimensions of every VP6 keyframe.
    struct Chunk {
        char magic[4];
        std::uint32_t bytes;
    };
    if (_fseeki64(file, 0, SEEK_END) != 0) return false;
    const auto length = _ftelli64(file);
    if (length < header.headerBytes || length > INT32_MAX || header.maxVideoPacket > length) return false;
    const auto alignedPacket = (std::uint64_t(header.maxVideoPacket) + 3) & ~std::uint64_t(3);
    unsigned videoFrames = 0;
    for (std::int64_t position = header.headerBytes; position < length; ) {
        Chunk chunk{};
        if (_fseeki64(file, position, SEEK_SET) != 0 || std::fread(&chunk, sizeof(chunk), 1, file) != 1 ||
            chunk.bytes < sizeof(chunk) || position + chunk.bytes > length) return false;
        const bool keyframe = std::memcmp(chunk.magic, "MV0K", 4) == 0;
        if (keyframe || std::memcmp(chunk.magic, "MV0F", 4) == 0) {
            const auto packetBytes = chunk.bytes - sizeof(chunk);
            if (!packetBytes || packetBytes > alignedPacket || (!videoFrames && !keyframe) ||
                ++videoFrames > header.frames) return false;
        }
        if (keyframe) {
            std::uint8_t frame[8]{};
            if (chunk.bytes <= sizeof(chunk) + sizeof(frame) || std::fread(frame, sizeof(frame), 1, file) != 1 ||
                (frame[0] & 0x80) || (frame[1] >> 3) > 8) return false;
            const unsigned macroblockOffset = ((frame[0] & 1) || !(frame[1] & 6)) ? 4 : 2;
            if (frame[macroblockOffset] != (size.height + 15) / 16 ||
                frame[macroblockOffset + 1] != (size.width + 15) / 16) return false;
        }
        position += chunk.bytes;
    }
    return videoFrames == header.frames;
}

bool ReadReplacementMovie(const char* path, bool pal, MovieFrameSize& size) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    const bool valid = ReadMovieContainer(file, pal, true, size);
    std::fclose(file);
    return valid;
}

bool FindReplacementMovie(const char* path, HMODULE gameModule, IDirect3DDevice9* device,
                          char (&replacement)[MAX_PATH], MovieFrameSize& size) {
    if (!path) return false;
    const auto* slash = std::strrchr(path, '\\');
    constexpr char folder[] = "MOVIES\\";
    constexpr auto folderLength = static_cast<std::ptrdiff_t>(sizeof(folder) - 1);
    if (!slash || slash - path < folderLength - 1) return false;
    const auto* folderStart = slash - (folderLength - 1);
    if (_strnicmp(folderStart, folder, folderLength) != 0 ||
        (folderStart != path && folderStart[-1] != '\\')) return false;

    const auto* filename = slash + 1;
    const auto length = std::strlen(filename);
    if (length < 10 || length > 100 || _stricmp(filename + length - 4, ".vp6") != 0) return false;
    const bool pal = _stricmp(filename + length - 8, "_pal.vp6") == 0;
    if (!pal && _stricmp(filename + length - 9, "_ntsc.vp6") != 0) return false;
    if (std::strspn(filename, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_.-") != length) return false;

    char root[MAX_PATH]{};
    const DWORD rootLength = GetModuleFileNameA(gameModule, root, MAX_PATH);
    if (!rootLength || rootLength >= MAX_PATH) return false;
    auto* executable = std::strrchr(root, '\\');
    if (!executable) return false;
    executable[1] = '\0';
    const int written = std::snprintf(replacement, MAX_PATH, "%sMOVIES_HD\\%s", root, filename);
    // The EA stream copies at most 0x103 bytes, including the terminator.
    if (written <= 0 || written >= 0x103 || !ReadReplacementMovie(replacement, pal, size)) return false;
    if (device) {
        D3DCAPS9 caps{};
        if (FAILED(device->GetDeviceCaps(&caps)) || caps.MaxTextureWidth < size.width ||
            caps.MaxTextureHeight < size.height) return false;
    }
    return true;
}

int __cdecl MovieFileExists(const char* filename) {
    const int original = GameFunction<int (__cdecl*)(const char*)>(movieFileExistsOriginal)(filename);
    if (original) return original;
    char replacement[MAX_PATH]{};
    MovieFrameSize size{};
    return FindReplacementMovie(filename, engine, GameValue<IDirect3DDevice9*>(0x582bdc), replacement, size) ? 1 : 0;
}

void* __cdecl OpenMovieStream(void* stream, const char* filename, void* options, std::uint32_t flags) {
    using Open = void* (__cdecl*)(void*, const char*, void*, std::uint32_t);
    const auto original = GameFunction<Open>(movieStreamOpenOriginal);
    char replacement[MAX_PATH]{};
    MovieFrameSize size{};
    if (FindReplacementMovie(filename, engine, GameValue<IDirect3DDevice9*>(0x582bdc), replacement, size)) {
        // The stream copies this path into its own fixed-size filename field.
        if (auto* opened = original(stream, replacement, options, flags)) {
            if (LogFile log; log.file) {
                std::fprintf(log.file, "Movie: %ux%u replacement %s\n", size.width, size.height, filename);
            }
            return opened;
        }
    }
    return original(stream, filename, options, flags);
}

bool InstallMovieReplacement() {
    struct MovieHook {
        std::uintptr_t call;
        std::uintptr_t original;
        std::uintptr_t replacement;
    };
    const MovieHook hooks[] = {
        {0x3f7b8b, movieStreamOpenOriginal, reinterpret_cast<std::uintptr_t>(&OpenMovieStream)},
        {0x15970a, movieFileExistsOriginal, reinterpret_cast<std::uintptr_t>(&MovieFileExists)},
        {0x159727, movieFileExistsOriginal, reinterpret_cast<std::uintptr_t>(&MovieFileExists)},
    };
    std::array<std::array<std::uint8_t, 5>, std::size(hooks)> stock{};
    for (std::size_t i = 0; i < std::size(hooks); ++i) {
        std::memcpy(stock[i].data(), &GameValue<std::uint8_t>(hooks[i].call), stock[i].size());
    }
    std::size_t installed = 0;
    for (const auto& hook : hooks) {
        if (!RedirectCall(hook.call, hook.original, hook.replacement)) {
            while (installed) {
                --installed;
                WriteGameBytes(hooks[installed].call, stock[installed].data(), stock[installed].size());
            }
            return false;
        }
        ++installed;
    }
    return true;
}

bool InstallMoviePresentation() {
    constexpr std::uintptr_t drawCall = 0x19a03f, uploadCall = 0x126030;
    constexpr std::uint8_t originalDraw[] = {0xe8, 0x1c, 0x74, 0xff, 0xff};
    if (!RedirectCall(drawCall, movieDrawOriginal, reinterpret_cast<std::uintptr_t>(&DrawMovieQuad))) return false;
    if (!RedirectCall(uploadCall, movieUploadOriginal, reinterpret_cast<std::uintptr_t>(&UploadMovieFrame))) {
        WriteGameBytes(drawCall, originalDraw, sizeof(originalDraw));
        return false;
    }
    nativeMovieInstalled = InstallNativeMoviePass();
    if (!InstallMovieReplacement()) {
        if (LogFile log; log.file) std::fprintf(log.file, "Movie: replacement file hook unavailable\n");
    }
    return true;
}

}
