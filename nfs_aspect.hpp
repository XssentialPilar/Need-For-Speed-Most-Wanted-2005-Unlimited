#pragma once

namespace {

constexpr float referenceAspect = 4.0f / 3.0f;

struct AspectLayout {
    float aspect;
    float hudX;
    float hudY;
    float cameraX;
    float cameraY;
};

constexpr AspectLayout MakeAspectLayout(unsigned width, unsigned height) {
    const float aspect = width && height ? static_cast<float>(width) / height : referenceAspect;
    const float horizontalScale = std::min(1.0f, referenceAspect / aspect);
    return {aspect, horizontalScale, std::min(1.0f, aspect / referenceAspect),
            horizontalScale, horizontalScale * aspect};
}

// Translate the top of the fitted canvas to the top of the actual viewport
// in logical 640 by 480 coordinates. All top-centered HUD draws share it.
constexpr float FitTopEdge(float scaleY) {
    return 240.0f - 240.0f / scaleY;
}

constexpr float UiToClip(float coordinate, float size, float scale) {
    return (2.0f * coordinate / size - 1.0f) * scale;
}

constexpr float ClipToUi(float coordinate, float size, float scale) {
    return (1.0f + coordinate / scale) * size * 0.5f;
}

AspectLayout activeLayout = MakeAspectLayout(640, 480);
float uiHorizontalGain = 2.0f / 640.0f;
float uiVerticalGain = 2.0f / 480.0f;
float uiHorizontalOrigin = 1.0f;
float uiVerticalOrigin = 1.0f;
thread_local float viewAspect = 0.0f;
D3DDISPLAYMODE nativeMode{};
bool startInNativeMode = true;

void UpdateAspect(unsigned width, unsigned height) {
    if (!width || !height) return;
    activeLayout = MakeAspectLayout(width, height);
    uiHorizontalOrigin = activeLayout.hudX;
    uiVerticalOrigin = activeLayout.hudY;
    uiHorizontalGain = (2.0f / 640.0f) * uiHorizontalOrigin;
    uiVerticalGain = (2.0f / 480.0f) * uiVerticalOrigin;
}

bool ReadNativeMode(IDirect3D9* factory, UINT adapter, unsigned width, unsigned height, D3DDISPLAYMODE& selected) {
    selected = {};
    D3DDISPLAYMODE desktop{};
    if (!factory || FAILED(factory->GetAdapterDisplayMode(adapter, &desktop))) return false;
    const bool custom = width && height;
    if (!custom) {
        width = desktop.Width;
        height = desktop.Height;
    }
    const UINT modes = factory->GetAdapterModeCount(adapter, D3DFMT_X8R8G8B8);
    for (UINT i = 0; i < modes; ++i) {
        D3DDISPLAYMODE choice{};
        if (FAILED(factory->EnumAdapterModes(adapter, D3DFMT_X8R8G8B8, i, &choice)) ||
            choice.Width != width || choice.Height != height) continue;
        if (!custom && choice.RefreshRate == desktop.RefreshRate) {
            selected = choice;
            return true;
        }
        if (!selected.Width || choice.RefreshRate > selected.RefreshRate) selected = choice;
    }
    return selected.Width != 0;
}

void __cdecl SetUpAspectVideoModes() {
    const auto queryCaps = GameFunction<void (__cdecl*)()>(0x2c1510);
    auto* factory = GameValue<IDirect3D9*>(0x582bd8);
    const UINT adapter = GameValue<UINT>(0x582c14);
    const auto read = [](const wchar_t* key, unsigned fallback) {
        return settingsPath[0] ? GetPrivateProfileIntW(L"Display", key, fallback, settingsPath) : fallback;
    };
    startInNativeMode = read(L"StartAtNative", 1) != 0;
    const unsigned width = read(L"Width", 0), height = read(L"Height", 0);
    const D3DDISPLAYMODE previous = nativeMode;
    D3DDISPLAYMODE next{};
    const bool found = ReadNativeMode(factory, adapter, width, height, next) ||
                       ((width || height) && ReadNativeMode(factory, adapter, 0, 0, next));
    if (found) {
        struct ModeValue {
            std::uintptr_t offset;
            UINT value;
            UINT original;
        };
        const UINT oldWidth = previous.Width ? previous.Width : 1600u;
        const UINT oldHeight = previous.Height ? previous.Height : 1200u;
        const ModeValue changes[] = {
            {0x4f9428, next.Width, oldWidth}, {0x4f9440, next.Height, oldHeight},
            {0x2c2862, next.Width, oldWidth}, {0x2c2868, next.Height, oldHeight},
            {0x2c2942, next.Width, oldWidth}, {0x2c2948, next.Height, oldHeight},
        };
        std::size_t applied = 0;
        for (const auto& change : changes) {
            if (!MatchGameBytes(change.offset, &change.original, sizeof(UINT)) ||
                !WriteGameBytes(change.offset, &change.value, sizeof(UINT))) break;
            ++applied;
        }
        if (applied != std::size(changes)) {
            while (applied) {
                const auto& change = changes[--applied];
                WriteGameBytes(change.offset, &change.original, sizeof(UINT));
            }
        } else {
            nativeMode = next;
        }
    }
    queryCaps();
    if (nativeMode.Width) {
        GameValue<UINT>(0x582a10) = nativeMode.RefreshRate;
        GameValue<UINT>(0x53dc00) = 1;
    }
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Display: native_slot=%ux%u @ %uHz requested=%ux%u\n",
                     nativeMode.Width, nativeMode.Height, nativeMode.RefreshRate, width, height);
    }
}

void __fastcall GetInitialAspectSize(void* video, void*, UINT* width, UINT* height) {
    constexpr std::uintptr_t selectedMode = 0x50181c;
    if (nativeMode.Width && startInNativeMode) GameValue<int>(selectedMode) = 5;
    GameFunction<void (__thiscall*)(void*, UINT*, UINT*)>(0x2c27d0)(video, width, height);
    UpdateAspect(*width, *height);
}

void __cdecl SetAspectPresentParameters(UINT width, UINT height, UINT refresh) {
    GameFunction<void (__cdecl*)(UINT, UINT, UINT)>(0x2bfab0)(width, height, refresh);
    UpdateAspect(width, height);
}

void __fastcall DrawAspectResolution(std::uint8_t* option) {
    GameFunction<void (__thiscall*)(void*)>(0x11b7a0)(option);
    const auto* database = GameValue<const std::uint8_t*>(0x51ca20);
    if (!nativeMode.Width || !database) return;
    const auto* choices = *reinterpret_cast<const std::uint8_t* const*>(database);
    auto* value = *reinterpret_cast<std::uint8_t**>(option + 0x30);
    if (!choices || *reinterpret_cast<const int*>(choices + 0x6c) != 5 || !value) return;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%u x %u", nativeMode.Width, nativeMode.Height);
    GameFunction<void (__thiscall*)(void*, const char*)>(0x1bcc10)(value, text);
    *reinterpret_cast<std::uint32_t*>(value + 0x1c) |= 0x400000u;
}

void __fastcall UpdateAspectMouse(std::int32_t* cursor, void*) {
    const auto xBefore = cursor[1];
    const auto yBefore = cursor[2];
    GameFunction<void (__thiscall*)(std::int32_t*)>(0x2c9b60)(cursor);
    if (requestedWindowMode!=unlimited_display::Mode::Exclusive && displayResetInstalled) {
        const auto window=GameValue<HWND>(0x582bf4);
        POINT position{};
        RECT client{};
        // Stock windowed input subtracts a fixed 24-pixel caption. Use the
        // actual client origin for borderless, DPI scaling and decorated windows.
        if (window && GetCursorPos(&position) && ScreenToClient(window,&position) &&
            GetClientRect(window,&client) && client.right>0 && client.bottom>0) {
            cursor[1]=std::lround(640.0*position.x/client.right);
            cursor[2]=std::lround(480.0*position.y/client.bottom);
        }
    }
    // Input comes from the whole window; use the inverse of the HUD fit so
    // any points in the side bars remain outside the menu's logical canvas.
    cursor[1] = std::lround(ClipToUi(UiToClip(static_cast<float>(cursor[1]), 640.0f, 1.0f),
                                     640.0f, activeLayout.hudX));
    cursor[2] = std::lround(ClipToUi(UiToClip(static_cast<float>(cursor[2]), 480.0f, 1.0f),
                                     480.0f, activeLayout.hudY));
    cursor[5] = cursor[1] - xBefore;
    cursor[6] = cursor[2] - yBefore;
}

extern "C" __attribute__((used)) void __cdecl UnlimitedReadViewAspect(const std::uint8_t* view) {
    viewAspect = 0.0f;
    const unsigned id = *reinterpret_cast<const unsigned*>(view + 4);
    // The player cameras and their road mirror use the display framing.
    // Rear-view mirror, cube maps and shadow textures have their own optics.
    if (id != 1 && id != 2 && id != 4 && id != 5) return;
    viewAspect = activeLayout.aspect;
    if (id <= 2) {
        const auto* target = *reinterpret_cast<const unsigned* const*>(view + 0x60);
        if (target && target[5] && target[6]) viewAspect = static_cast<float>(target[5]) / target[6];
    }
}

__attribute__((naked)) void ReadProjectionView() {
    asm(
        "pushfl\n"
        "pushal\n"
        "pushl %edi\n"
        "call _UnlimitedReadViewAspect\n"
        "addl $4, %esp\n"
        "popal\n"
        "popfl\n"
        "movl 0x60(%edi), %eax\n"
        "fildl 0x18(%eax)\n"
        "fidivl 0x14(%eax)\n"
        "ret\n");
}

void ApplyAspectToProjection(D3DMATRIX& projection, float aspect) {
    const float originalHorizontal = projection._11;
    const float scale = std::min(1.0f, referenceAspect / aspect);
    projection._11 = originalHorizontal * scale;
    projection._22 = -originalHorizontal * scale * aspect;
}

D3DMATRIX* WINAPI MultiplyAspectView(D3DMATRIX* result, const D3DMATRIX* view, D3DMATRIX* projection) {
    const float ratio = viewAspect;
    viewAspect = 0.0f;
    if (std::isfinite(ratio) && ratio > 0.0f) ApplyAspectToProjection(*projection, ratio);
    using Multiply = D3DMATRIX* (WINAPI*)(D3DMATRIX*, const D3DMATRIX*, const D3DMATRIX*);
    return GameFunction<Multiply>(0x3e7d5e)(result, view, projection);
}

bool InstallAspectCorrection() {
    constexpr std::uint8_t stockProjection[] = {0x8b,0x47,0x60,0xdb,0x40,0x18,0xda,0x70,0x14};
    constexpr std::uint8_t stockMode[] = {
        0x8b,0x4c,0x24,4,0x8b,0x54,0x24,8,0xc7,1,0x40,6,0,0,0xc7,2,0xb0,4,0,0,0xc2,8,0
    };
    constexpr std::uintptr_t modeSize[] = {0x2c2858, 0x2c2938};
    constexpr std::uintptr_t inputCalls[] = {0x2c9c83, 0x2d809c, 0x2d80a3};
    struct CanvasAxis {
        std::uintptr_t gain;
        std::uintptr_t origin;
        bool horizontal;
    };
    constexpr CanvasAxis canvas[] = {
        {0x2d12c2,0x2d12c8,true}, {0x2d1304,0x2d1312,true},
        {0x2d1387,0x2d1395,true}, {0x2d140a,0x2d1418,true},
        {0x2d12d5,0x2d12db,false}, {0x2d131f,0x2d132b,false},
        {0x2d13a2,0x2d13ae,false}, {0x2d1428,0x2d1434,false},
    };
    constexpr std::uintptr_t modeDraw = 0x49bb10;
    if (GameValue<std::uintptr_t>(modeDraw) != reinterpret_cast<std::uintptr_t>(engine) + 0x11b7a0 ||
        !MatchGameBytes(0x2cf4ed, stockProjection, sizeof(stockProjection))) return false;
    for (const auto offset : modeSize) {
        if (!MatchGameBytes(offset, stockMode, sizeof(stockMode))) return false;
    }
    for (const auto& axis : canvas) {
        const std::uint32_t stockGain = axis.horizontal ? 0x8af9a4 : 0x8af9a0;
        constexpr std::uint32_t stockOrigin = 0x89096c;
        if (!MatchGameBytes(axis.gain, &stockGain, 4) || !MatchGameBytes(axis.origin, &stockOrigin, 4)) return false;
    }
    if (!InstallInlineCall(0x2cf4ed, stockProjection, &ReadProjectionView) ||
        !RedirectCall(0x2cf62a, 0x3e7d5e, reinterpret_cast<std::uintptr_t>(&MultiplyAspectView)) ||
        !RedirectCall(0x2e6bb5, 0x2c27d0, reinterpret_cast<std::uintptr_t>(&GetInitialAspectSize)) ||
        !RedirectCall(0x2e72b9, 0x2bfab0, reinterpret_cast<std::uintptr_t>(&SetAspectPresentParameters))) return false;
    auto* draw = &DrawAspectResolution;
    if (!WriteGameBytes(modeDraw, &draw, sizeof(draw))) return false;
    for (const auto& axis : canvas) {
        const float* gain = axis.horizontal ? &uiHorizontalGain : &uiVerticalGain;
        const float* origin = axis.horizontal ? &uiHorizontalOrigin : &uiVerticalOrigin;
        if (!WriteGameBytes(axis.gain, &gain, sizeof(gain)) || !WriteGameBytes(axis.origin, &origin, sizeof(origin)))
            return false;
    }
    for (const auto offset : inputCalls) {
        if (!RedirectCall(offset, 0x2c9b60, reinterpret_cast<std::uintptr_t>(&UpdateAspectMouse))) return false;
    }
    return true;
}

} // namespace
