#pragma once

#include "nfs_frontend_artwork.hpp"
#include "nfs_frontend_layout.hpp"

namespace {

scene_hud::GameHud nativeInstrumentPass;
constexpr char instrumentHudPackage[] = "HUD_SingleRace.fng";
constexpr std::uint32_t nativeSpeedGroup = 0x941fff09;
constexpr std::uint32_t nativeRpmGroup = 0x5164d4ea;

struct InstrumentReading {
    unsigned speed = 0;
    char gear = 'N';
    float revs = 0;
    float redline = 1;
    bool metric = true;
    bool shift = false;
    ULONGLONG sampledAt = 0;
};

InstrumentReading liveInstruments{};
bool redesignedHudAvailable = false;
bool redesignedHudEnabled = true;

bool ReadInstrumentValues(const void* speedometer, const void* tachometer, bool metric, InstrumentReading& out) {
    const auto* speed = static_cast<const std::uint8_t*>(speedometer);
    const auto* tach = static_cast<const std::uint8_t*>(tachometer);
    float metersPerSecond, rpm, redline, maximumRpm;
    int gear, shiftPotential;
    std::memcpy(&metersPerSecond, speed + 0x48, sizeof(float));
    std::memcpy(&rpm, tach + 0x54, sizeof(float));
    std::memcpy(&redline, tach + 0x58, sizeof(float));
    std::memcpy(&maximumRpm, tach + 0x5c, sizeof(float));
    std::memcpy(&gear, tach + 0x60, sizeof(int));
    std::memcpy(&shiftPotential, tach + 0x68, sizeof(int));
    if (!std::isfinite(metersPerSecond) || !std::isfinite(rpm) || !std::isfinite(redline) ||
        !std::isfinite(maximumRpm) || maximumRpm < 200.0f || gear < 0 || gear > 9) return false;
    const float displayedSpeed = std::max(0.0f, std::min(999.0f, std::fabs(metersPerSecond) * (metric ? 3.6f : 2.2369363f)));
    out.speed = static_cast<unsigned>(displayedSpeed);
    out.gear = "RN12345678"[gear];
    out.revs = std::clamp(rpm / maximumRpm, 0.0f, 1.0f);
    out.redline = std::clamp(redline / maximumRpm, 0.0f, 1.0f);
    out.metric = metric;
    out.shift = shiftPotential > 1;
    out.sampledAt = GetTickCount64();
    return true;
}

bool HudElementActive(void* element) {
    return (HudField<unsigned>(element, 0x18) & HudField<unsigned>(element, 0x20)) ||
           (HudField<unsigned>(element, 0x1c) & HudField<unsigned>(element, 0x24));
}

void CaptureInstrumentReading(std::uint8_t* hud) {
    // The same package can be renamed on non-retail builds. Check the exact
    // instrument object vtables before reading the PC 1.3 fields.
    const auto* package = HudField<const char*>(hud, 0x24);
    if (!redesignedHudEnabled || !package || _stricmp(package, instrumentHudPackage) != 0) return;
    liveInstruments.sampledAt = 0;
    auto* speed = HudField<void*>(hud, 0x2c0);
    auto* tach = HudField<void*>(hud, 0x2c4);
    const auto base = reinterpret_cast<std::uintptr_t>(engine);
    if (!speed || !tach || HudField<std::uintptr_t>(speed, 0) != base + 0x4a21d4 ||
        HudField<std::uintptr_t>(tach, 0) != base + 0x4a2260 ||
        !HudElementActive(speed) || !HudElementActive(tach)) return;
    auto* database = GameValue<void*>(0x51cf90);
    auto* settings = database ? HudField<void*>(database, 0x10) : nullptr;
    if (settings) ReadInstrumentValues(speed, tach, HudField<std::uint8_t>(settings, 0x3b) == 1, liveInstruments);
}

struct InstrumentVertex {
    float x, y, depth, rhw;
    D3DCOLOR color;
};

struct InstrumentPosition {
    float x, y, scale;
};

constexpr float instrumentWidth = 146;
constexpr float instrumentHeight = 94;

InstrumentPosition PlaceInstruments(const D3DVIEWPORT9& view, const HudBounds& oldBounds) {
    const float scale = std::min(view.Width / 640.0f, view.Height / 480.0f);
    const float inset = 12.0f * scale;
    const float minX = static_cast<float>(view.X) + inset;
    const float minY = static_cast<float>(view.Y) + inset;
    const float maxX = view.X + static_cast<float>(view.Width) - inset - instrumentWidth * scale;
    const float maxY = view.Y + static_cast<float>(view.Height) - inset - instrumentHeight * scale;
    return {
        std::clamp(view.X + view.Width * 0.5f + (oldBounds.right - instrumentWidth) * scale, minX, maxX),
        std::clamp(view.Y + view.Height * 0.5f + (oldBounds.bottom - instrumentHeight) * scale, minY, maxY),
        scale
    };
}

struct InstrumentGeometry {
    explicit InstrumentGeometry(InstrumentPosition position) : position(position) {}

    InstrumentPosition position;
    std::array<InstrumentVertex, 3072> vertices;
    std::size_t count = 0;

    void Triangle(HudPoint a, HudPoint b, HudPoint c, D3DCOLOR color) {
        for (auto vertex : {a, b, c}) {
            vertices[count++] = {
                position.x + vertex.x * position.scale - 0.5f,
                position.y + vertex.y * position.scale - 0.5f, 0, 1, color
            };
        }
    }

    void Rect(float x, float y, float width, float height, D3DCOLOR color) {
        if (width <= 0 || height <= 0) return;
        const HudPoint a{x, y}, b{x + width, y}, c{x + width, y + height}, d{x, y + height};
        Triangle(a, b, c, color);
        Triangle(a, c, d, color);
    }
};

using InstrumentGlyph = std::array<std::uint8_t, 7>;

const InstrumentGlyph& InstrumentGlyphFor(char glyph) {
    static constexpr InstrumentGlyph digits[] = {
        {14,19,19,21,25,25,14}, {4,12,4,4,4,4,14}, {14,17,1,6,8,16,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
        {7,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14}, {14,17,17,15,1,2,28}
    };
    static constexpr InstrumentGlyph r{30,17,17,30,20,18,17}, n{17,25,25,21,19,19,17};
    static constexpr InstrumentGlyph k{17,18,20,24,20,18,17}, m{17,27,21,21,17,17,17};
    static constexpr InstrumentGlyph p{30,17,17,30,16,16,16}, h{17,17,17,31,17,17,17};
    static constexpr InstrumentGlyph slash{1,1,2,4,8,16,16}, blank{};
    if (glyph >= '0' && glyph <= '9') return digits[glyph - '0'];
    switch (glyph) {
    case 'R': return r;
    case 'N': return n;
    case 'K': return k;
    case 'M': return m;
    case 'P': return p;
    case 'H': return h;
    case '/': return slash;
    default: return blank;
    }
}

void AddInstrumentText(InstrumentGeometry& g, const char* string, float x, float y, float scale, D3DCOLOR color) {
    for (const char* glyph = string; *glyph; ++glyph, x += 6.0f * scale) {
        const auto& rows = InstrumentGlyphFor(*glyph);
        for (unsigned row = 0; row < rows.size(); ++row) {
            for (unsigned col = 0; col < 5; ++col) {
                if (rows[row] & (16u >> col)) g.Rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void BuildInstrumentGeometry(InstrumentGeometry& g, const InstrumentReading& in) {
    constexpr D3DCOLOR panel = 0xf6161c26, inset = 0xff26323d, track = 0xff34444f;
    constexpr D3DCOLOR white = 0xfff4f7fa, secondary = 0xffb6c8d4, teal = 0xff65e5ca, coral = 0xffff836b;
    constexpr D3DCOLOR redlineTrack = 0xff744642;
    const D3DCOLOR rpmColor = in.shift || in.revs >= in.redline ? coral : teal;
    g.count = 0;
    // A clipped corner, horizontal rev segments and an inset gear indicator
    // keep the silhouette distinct from the stock circular instruments.
    g.Rect(9, 0, 137, 94, panel);
    g.Rect(0, 9, 9, 85, panel);
    g.Triangle({0, 9}, {9, 0}, {9, 9}, panel);
    g.Rect(9, 0, 26, 2, rpmColor);
    g.Rect(103, 12, 32, 38, inset);
    g.Rect(11, 57, 124, 1, track);

    char speed[4]{};
    std::snprintf(speed, sizeof(speed), "%u", std::min(999u, in.speed));
    const char gear[] = {in.gear, 0};
    AddInstrumentText(g, speed, 11.0f, 10.0f, 4.4f, white);
    AddInstrumentText(g, in.metric ? "KM/H" : "MPH", 12.0f, 45.0f, 1.25f, secondary);
    AddInstrumentText(g, gear, 119.0f - 8.5f, 19.0f, 3.4f, in.shift ? coral : white);
    AddInstrumentText(g, "RPM", 11.0f, 80.0f, 1.35f, secondary);
    if (in.shift) g.Triangle({121, 89}, {129, 79}, {137, 89}, coral);

    constexpr unsigned ticks = 14;
    for (unsigned index = 0; index < ticks; ++index) {
        const float x = 11.0f + index * 9.0f;
        const float position = static_cast<float>(index) / ticks;
        const float fill = std::clamp(in.revs * ticks - index, 0.0f, 1.0f);
        g.Rect(x, 66, 7, 8, position >= in.redline ? redlineTrack : track);
        g.Rect(x, 66, 7 * fill, 8, position >= in.redline ? coral : rpmColor);
    }
}

bool DrawInstrumentGeometry(IDirect3DDevice9* device, const InstrumentGeometry& g) {
    IDirect3DStateBlock9* restore = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &restore))) return false;
    const auto set = [device](D3DRENDERSTATETYPE state, DWORD value) {
        return SUCCEEDED(device->SetRenderState(state, value));
    };
    const auto stage = [device](DWORD index, D3DTEXTURESTAGESTATETYPE state, DWORD value) {
        return SUCCEEDED(device->SetTextureStageState(index, state, value));
    };
    const bool drawn =
        SUCCEEDED(device->SetVertexShader(nullptr)) && SUCCEEDED(device->SetPixelShader(nullptr)) &&
        SUCCEEDED(device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE)) &&
        set(D3DRS_ZENABLE, FALSE) && set(D3DRS_ZWRITEENABLE, FALSE) && set(D3DRS_STENCILENABLE, FALSE) &&
        set(D3DRS_ALPHATESTENABLE, FALSE) && set(D3DRS_ALPHABLENDENABLE, TRUE) &&
        set(D3DRS_SEPARATEALPHABLENDENABLE, FALSE) && set(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA) &&
        set(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA) && set(D3DRS_BLENDOP, D3DBLENDOP_ADD) &&
        set(D3DRS_FOGENABLE, FALSE) && set(D3DRS_CULLMODE, D3DCULL_NONE) && set(D3DRS_LIGHTING, FALSE) &&
        set(D3DRS_SCISSORTESTENABLE, FALSE) && set(D3DRS_FILLMODE, D3DFILL_SOLID) &&
        set(D3DRS_COLORWRITEENABLE, 0xf) && set(D3DRS_SRGBWRITEENABLE, FALSE) &&
        stage(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1) && stage(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE) &&
        stage(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1) && stage(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE) &&
        stage(1, D3DTSS_COLOROP, D3DTOP_DISABLE) &&
        SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(g.count / 3),
                                          g.vertices.data(), sizeof(InstrumentVertex)));
    const bool restored = SUCCEEDED(restore->Apply());
    restore->Release();
    return drawn && restored;
}

void __fastcall RenderWithRedesignedInstruments(void* manager, void*) {
    const auto native = GameFunction<void (__thiscall*)(void*)>(0x116f70);
    if (!redesignedHudEnabled || GameValue<std::uint32_t>(gameFlowStateRva) != 6 || !liveInstruments.sampledAt ||
        GetTickCount64() - liveInstruments.sampledAt > 160 ||
        !GameValue<int>(0x4f374c)) return native(manager);
    auto* eng = GameValue<void*>(0x51cadc);
    const auto pushed = GameFunction<bool (__thiscall*)(void*, const char*)>(0x116b50);
    if (!eng || !pushed(eng, instrumentHudPackage) || pushed(eng, "Pause_Main.fng") ||
        pushed(eng, "MC_Main_GC.fng")) return native(manager);

    // Prefer the active right-hand HUD variant. Its drawing coordinates
    // already account for the game camera and Unlimited's aspect anchors.
    void* speed = nullptr;
    void* rpm = nullptr;
    HudBounds oldBounds{};
    for (const auto hash : {0x5d0101f1u, 0x2fe4ff9cu}) {
        auto* group = FindHudObject(instrumentHudPackage, hash);
        auto* content = FindHudChild(group, 0x5c7a5c8a);
        if (!content || (HudField<unsigned>(group, 0x1c) & 0x80000001u) ||
            (HudField<unsigned>(content, 0x1c) & 0x80000001u)) continue;
        speed = FindHudChild(content, nativeSpeedGroup);
        rpm = FindHudChild(content, nativeRpmGroup);
        if (!speed || !rpm || (HudField<unsigned>(speed, 0x1c) & 0x80000001u) ||
            (HudField<unsigned>(rpm, 0x1c) & 0x80000001u)) continue;
        const auto parent = AddHudPoints(ReadHudPosition(group), ReadHudPosition(content));
        unsigned remaining = 160;
        MeasureHudBounds(rpm, parent, 0, remaining, oldBounds);
        if (oldBounds.Valid()) break;
    }
    if (!oldBounds.Valid()) return native(manager);
    auto* speedData = HudField<void*>(speed, 0x2c);
    auto* rpmData = HudField<void*>(rpm, 0x2c);
    auto* device = GameValue<IDirect3DDevice9*>(0x582bdc);
    D3DVIEWPORT9 viewport{};
    if (!speedData || !rpmData || !device || FAILED(device->GetViewport(&viewport)) ||
        !viewport.Width || !viewport.Height) return native(manager);
    auto& speedAlpha = HudField<int>(speedData, 0xc);
    auto& rpmAlpha = HudField<int>(rpmData, 0xc);
    if (speedAlpha < 254 || rpmAlpha < 254) return native(manager);

    InstrumentGeometry geometry(PlaceInstruments(viewport, oldBounds));
    BuildInstrumentGeometry(geometry, liveInstruments);
    const bool modern = nativePassesActive && nativeInstrumentPass.Draw(device, geometry.vertices.data(),
                                                                       geometry.count, sizeof(InstrumentVertex));
    if (!modern && !DrawInstrumentGeometry(device, geometry)) return native(manager);
    const int nativeSpeedAlpha = speedAlpha;
    const int nativeRpmAlpha = rpmAlpha;
    speedAlpha = rpmAlpha = 0;
    native(manager);
    speedAlpha = nativeSpeedAlpha;
    rpmAlpha = nativeRpmAlpha;
}

bool InstallRedesignedHud() {
    redesignedHudEnabled = !settingsPath[0] || GetPrivateProfileIntW(L"Display", L"RedesignedHud", 1, settingsPath) != 0;
    constexpr std::uint8_t speedRead[] = {0xd9, 0x46, 0x48, 0x8a, 0x41, 0x3b};
    constexpr std::uint8_t tachRead[] = {0xd9, 0x46, 0x54, 0xd8, 0x76, 0x5c};
    if (!MatchGameBytes(0x17a54f, speedRead, sizeof(speedRead)) ||
        !MatchGameBytes(0x17a708, tachRead, sizeof(tachRead)) ||
        !RedirectCall(0x2e71d0, 0x116f70, reinterpret_cast<std::uintptr_t>(&RenderWithRedesignedInstruments))) return false;
    captureHudInstruments = &CaptureInstrumentReading;
    redesignedHudAvailable = true;
    return true;
}

void SetOptionText(std::uint8_t* option, const char* title, const char* value);
void AddUnlimitedVideoOption(std::uint8_t* screen, std::array<void*, 15>& vtable,
                             void (__fastcall* change)(std::uint8_t*, void*, int, std::int32_t),
                             void (__fastcall* draw)(std::uint8_t*));

void __fastcall DrawRedesignedHudOption(std::uint8_t* option) {
    const bool spanish = GameValue<int>(0x4f41c0) == 4;
    SetOptionText(option, spanish ? "Cuadro de instrumentos" : "Instrument cluster",
                  redesignedHudEnabled ? (spanish ? u8"Rediseñado" : "Redesigned")
                                       : (spanish ? u8"Clásico" : "Classic"));
}

void __fastcall ChangeRedesignedHudOption(std::uint8_t* option, void*, int, std::int32_t message) {
    constexpr std::int32_t left = -1860157282, right = -1248388111;
    if ((message != left && message != right) || !redesignedHudAvailable || !settingsPath[0] ||
        !WritePrivateProfileStringW(L"Display", L"RedesignedHud",
                                    redesignedHudEnabled ? L"0" : L"1", settingsPath)) return;
    redesignedHudEnabled = !redesignedHudEnabled;
    liveInstruments.sampledAt = 0;
    option[0x2a] = 1;
    DrawRedesignedHudOption(option);
}

void AddRedesignedHudVideoOption(std::uint8_t* screen) {
    const auto* package = HudField<const char*>(screen, 0x10);
    // The retail basic video screen has two stock rows. The FPS and Xbox
    // switches take rows three and four. The advanced screen uses its own
    // capacity, so keep this control in the verified basic package.
    if (!redesignedHudAvailable || !settingsPath[0] || screen[0x154] ||
        !package || _stricmp(package, "Options.fng") != 0 || HudField<int>(screen, 0x104) != 5) return;
    using FindImage = void* (__thiscall*)(std::uint8_t*, const char*);
    const auto image = GameFunction<FindImage>(0x173d10);
    if (!image(screen, "LEFT_ARROW_") || !image(screen, "RIGHT_ARROW_")) return;
    static std::array<void*, 15> vtable{};
    AddUnlimitedVideoOption(screen, vtable, &ChangeRedesignedHudOption, &DrawRedesignedHudOption);
}

} // namespace

