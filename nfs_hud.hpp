#pragma once

namespace {

namespace unlimited_language { void ServiceApply(); }
bool responsiveSongNoticeEnabled = false;

struct HudPoint {
    float x = 0;
    float y = 0;
};

struct HudBounds {
    float left = INFINITY;
    float top = INFINITY;
    float right = -INFINITY;
    float bottom = -INFINITY;

    bool Valid() const { return left < right && top < bottom; }
};

constexpr float HudTravel(float scale, float halfCanvas = 320.0f) {
    return scale >= 1.0f ? 0.0f : 0.85f * std::min(2.0f * halfCanvas, halfCanvas / scale - halfCanvas);
}

constexpr float FitHudAxis(float low, float high, float scale, float halfCanvas, float preferred) {
    const float halfViewport = halfCanvas / scale;
    const float margin = std::min(0.04f * halfViewport, 0.10f * halfCanvas);
    const float limit = halfViewport - margin;
    const float minimum = -limit - low;
    const float maximum = limit - high;
    return minimum <= maximum ? std::clamp(preferred, minimum, maximum) : -(low + high) * 0.5f;
}

template<class T>
T& HudField(void* object, unsigned offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

void* FindHudObject(const char* package, std::uint32_t hash) {
    return GameFunction<void* (__cdecl*)(const char*, std::uint32_t)>(0x124850)(package, hash);
}

void* FindHudChild(void* group, std::uint32_t hash) {
    constexpr unsigned typeGroup = 5;
    if (!group || HudField<unsigned>(group, 0x18) != typeGroup) return nullptr;
    auto* child = HudField<void*>(group, 0x64);
    for (unsigned i = 0, count = std::min(HudField<unsigned>(group, 0x60), 512U); child && i < count; ++i) {
        if (HudField<std::uint32_t>(child, 0x10) == hash) return child;
        child = HudField<void*>(child, 4);
    }
    return nullptr;
}

HudPoint ReadHudPosition(void* object) {
    if (!object) return {};
    auto* data = HudField<void*>(object, 0x2c);
    if (!data) return {};
    return {HudField<float>(data, 0x1c), HudField<float>(data, 0x20)};
}

HudPoint AddHudPoints(HudPoint first, HudPoint second) {
    return {first.x + second.x, first.y + second.y};
}

void MarkHudTransformDirty(void* object) {
    HudField<unsigned>(object, 0x1c) |= (1U << 24) | (1U << 25);
}

void MeasureHudBounds(void* object, HudPoint parent, unsigned depth, unsigned& remaining, HudBounds& bounds) {
    if (!object || depth > 12 || !remaining) return;
    --remaining;
    auto* data = HudField<void*>(object, 0x2c);
    if (!data || (HudField<unsigned>(object, 0x1c) & 1U) || !HudField<int>(data, 0xc)) return;

    const unsigned type = HudField<unsigned>(object, 0x18);
    if (type == 5) {
        const auto origin = AddHudPoints(parent, ReadHudPosition(object));
        auto* child = HudField<void*>(object, 0x64);
        for (unsigned i = 0, count = HudField<unsigned>(object, 0x60); child && i < count && remaining; ++i) {
            MeasureHudBounds(child, origin, depth + 1, remaining, bounds);
            child = HudField<void*>(child, 4);
        }
        return;
    }

    // FEng measures rendered text with its current font and justification.
    // Image dimensions can be negative when art is mirrored.
    if (type != 1 && type != 2 && type != 7 && type != 9 && type != 12) return;
    using Corner = void (__cdecl*)(void*, float*, float*);
    HudPoint first{}, last{};
    GameFunction<Corner>(0x114fd0)(object, &first.x, &first.y);
    GameFunction<Corner>(0x115140)(object, &last.x, &last.y);
    if (!std::isfinite(first.x) || !std::isfinite(first.y) ||
        !std::isfinite(last.x) || !std::isfinite(last.y) || first.x == last.x || first.y == last.y) return;
    bounds.left = std::min(bounds.left, parent.x + std::min(first.x, last.x));
    bounds.right = std::max(bounds.right, parent.x + std::max(first.x, last.x));
    bounds.top = std::min(bounds.top, parent.y + std::min(first.y, last.y));
    bounds.bottom = std::max(bounds.bottom, parent.y + std::max(first.y, last.y));
}

struct HudGroupPosition {
    void* object = nullptr;
    HudPoint applied{};
    HudPoint expected{};
};

HudPoint HudPositionWithoutOffset(HudGroupPosition& anchor, void* object) {
    if (object != anchor.object) anchor = {object};
    const auto position = ReadHudPosition(object);
    return {
        position.x - (std::fabs(position.x - anchor.expected.x) < 0.01f ? anchor.applied.x : 0.0f),
        position.y - (std::fabs(position.y - anchor.expected.y) < 0.01f ? anchor.applied.y : 0.0f)
    };
}

void SetHudGroupOffset(HudGroupPosition& anchor, void* object, HudPoint offset) {
    const auto base = HudPositionWithoutOffset(anchor, object);
    if (!object || !std::isfinite(base.x) || !std::isfinite(base.y)) return;
    auto* data = HudField<void*>(object, 0x2c);
    if (!data) return;
    anchor.applied = offset;
    anchor.expected = AddHudPoints(base, offset);
    if (std::fabs(anchor.expected.x - HudField<float>(data, 0x1c)) >= 0.01f ||
        std::fabs(anchor.expected.y - HudField<float>(data, 0x20)) >= 0.01f) {
        HudField<float>(data, 0x1c) = anchor.expected.x;
        HudField<float>(data, 0x20) = anchor.expected.y;
        MarkHudTransformDirty(object);
    }
}

// Use the actual viewport in FEng's centered coordinates. One margin
// measured on the short axis keeps equal pixel padding on both edges.
constexpr HudPoint HudCornerOffset(const HudBounds& bounds, float scaleX, float scaleY, bool right, bool bottom = true) {
    const float halfWidth = 320.0f / scaleX;
    const float halfHeight = 240.0f / scaleY;
    const float margin = 0.09f * std::min(halfWidth, halfHeight);
    return {
        right ? halfWidth - margin - bounds.right : -halfWidth + margin - bounds.left,
        bottom ? halfHeight - margin - bounds.bottom : -halfHeight + margin - bounds.top
    };
}

template<std::size_t N>
class HudCorner {
public:
    // Remove our translation before measuring the shared parent. The parent
    // also contains unrelated race information and still uses its stock layout.
    void FindChildren(void* group, const std::uint32_t (&hashes)[N]) {
        for (std::size_t i = 0; i < N; ++i) {
            objects[i] = FindHudChild(group, hashes[i]);
            SetHudGroupOffset(positions[i], objects[i], {});
        }
    }

    void Align(HudPoint origin, bool right, bool bottom = true) {
        HudBounds bounds{};
        unsigned remaining = 512;
        for (auto* object : objects) MeasureHudBounds(object, origin, 0, remaining, bounds);
        if (!bounds.Valid()) return;
        const auto offset = HudCornerOffset(bounds, activeLayout.hudX, activeLayout.hudY, right, bottom);
        for (std::size_t i = 0; i < N; ++i) SetHudGroupOffset(positions[i], objects[i], offset);
    }

private:
    std::array<void*, N> objects{};
    std::array<HudGroupPosition, N> positions{};
};

void AnchorHudGroup(HudGroupPosition& anchor, void* object, HudPoint origin, HudPoint preferred,
                    bool horizontal, bool vertical) {
    if (!object) {
        anchor = {};
        return;
    }
    if (object != anchor.object) anchor = {object};
    HudBounds bounds{};
    unsigned remaining = 512;
    MeasureHudBounds(object, origin, 0, remaining, bounds);
    if (!bounds.Valid()) return;
    const auto position = ReadHudPosition(object);
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) return;
    const auto priorX = std::fabs(position.x - anchor.expected.x) < 0.01f ? anchor.applied.x : 0.0f;
    const auto priorY = std::fabs(position.y - anchor.expected.y) < 0.01f ? anchor.applied.y : 0.0f;
    const auto x = horizontal ? FitHudAxis(bounds.left - priorX, bounds.right - priorX,
                                          activeLayout.hudX, 320.0f, preferred.x) : 0.0f;
    const auto y = vertical ? FitHudAxis(bounds.top - priorY, bounds.bottom - priorY,
                                        activeLayout.hudY, 240.0f, preferred.y) : 0.0f;
    anchor.applied = {x, y};
    anchor.expected = {position.x - priorX + x, position.y - priorY + y};
    if (std::fabs(anchor.expected.x - position.x) >= 0.01f || std::fabs(anchor.expected.y - position.y) >= 0.01f) {
        // The public SetCenter writes every script's position key. Apply the
        // aspect offset to the rendered transform only.
        auto* data = HudField<void*>(object, 0x2c);
        HudField<float>(data, 0x1c) = anchor.expected.x;
        HudField<float>(data, 0x20) = anchor.expected.y;
        MarkHudTransformDirty(object);
    }
}

bool ShiftMinimap(void* map, HudPoint delta) {
    if (!map) return false;
    constexpr unsigned offsets[] = {0x3c, 0x40, 0x44, 0x48, 0x90};
    std::array<void*, std::size(offsets)> nodes{};
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        nodes[i] = HudField<void*>(map, offsets[i]);
        if (!nodes[i] || !HudField<void*>(nodes[i], 0x2c)) return false;
    }
    // X follows the stock widescreen path. The Y offset belongs to the map's
    // default position because SetupMinimap reconstructs its layout every frame.
    HudField<float>(map, 0xb8) += delta.x;
    HudField<float>(map, 0xbc) += delta.y;
    HudField<float>(map, 0xa0) += delta.y;
    for (auto* node : nodes) {
        auto* data = HudField<void*>(node, 0x2c);
        HudField<float>(data, 0x1c) += delta.x;
        MarkHudTransformDirty(node);
    }
    HudField<float>(HudField<void*>(nodes.back(), 0x2c), 0x20) += delta.y;
    GameValue<float>(0x51cf04) += delta.x;
    return true;
}

constexpr std::uint32_t raceTableWidgets[] = {
    0xb1c5ed0d, 0x5ed609a1, // Race information and leaderboard.
    // These siblings make up the upper-right pursuit board. The timer and
    // bounty are in a different branch from the milestone rows and icons.
    0xde89e070, 0x2269d232, 0x9afce2b8
};

void LayoutUpperRaceHud(const char* package) {
    static std::array<std::array<HudGroupPosition, std::size(raceTableWidgets)>, 2> tables;
    static HudGroupPosition mirror, radar, radarBacking;
    const HudPoint offset{0, FitTopEdge(activeLayout.hudY)};
    for (unsigned i = 0; i < tables.size(); ++i) {
        const auto hash = i == 0 ? 0x5d0101f1u : 0x2fe4ff9cu;
        auto* right = FindHudObject(package, hash);
        auto* content = FindHudChild(right, 0x5c7a5c8a);
        // The table's layout is authored under the right HUD's 640x480
        // parent. Account for movement already applied to that parent.
        constexpr float authoredRight = 209.65f;
        const auto origin = AddHudPoints(ReadHudPosition(right), ReadHudPosition(content));
        const HudPoint tableOffset{authoredRight + 320.0f / activeLayout.hudX - 320.0f - origin.x, offset.y};
        // Translate the whole table, even when the next animation will show it.
        for (unsigned j = 0; j < std::size(raceTableWidgets); ++j)
            SetHudGroupOffset(tables[i][j], FindHudChild(content, raceTableWidgets[j]), tableOffset);
    }
    // Both halves of the native mirror bezel share this group. Its offset must
    // agree with the glass quad submitted by DrawResponsiveMirror.
    SetHudGroupOffset(mirror, FindHudObject(package, 0x7fac287b), offset);
    auto* radarRoot = FindHudObject(package, 0xab9c1d4d);
    // The shared root also contains the countdown and other race messages.
    SetHudGroupOffset(radar, FindHudChild(radarRoot, 0x062743f5), offset);
    SetHudGroupOffset(radarBacking, FindHudChild(radarRoot, 0x839e7d77), offset);
}

char raceHudPackage[64]{};

void __fastcall RenderResponsiveRaceTables(void* frontendLayer, void*) {
    // FEng can replace the authored positions after the HUD update hook.
    // Place the actual groups together immediately before FEng submits them.
    if (GameValue<std::uint32_t>(gameFlowStateRva) != 6) raceHudPackage[0] = 0;
    else if (raceHudPackage[0] && !unlimited_split_screen::RaceActive()) LayoutUpperRaceHud(raceHudPackage);
    GameFunction<void (__thiscall*)(void*)>(0x1be800)(frontendLayer);
}

void (*captureHudInstruments)(std::uint8_t*) = nullptr;

void __fastcall LayoutRaceHud(std::uint8_t* hud, void*) {
    const auto previousMode = hud[0x330];
    auto* map = HudField<void*>(hud, 0x320);
    GameFunction<void (__thiscall*)(void*)>(0x1696c0)(hud);
    if (unlimited_split_screen::RaceActive()) return;
    const auto* package = HudField<const char*>(hud, 0x24);
    if (!package || GameValue<std::uint32_t>(gameFlowStateRva) != 6) return;
    if (_stricmp(raceHudPackage, package) != 0)
        std::snprintf(raceHudPackage, sizeof(raceHudPackage), "%s", package);
    if (captureHudInstruments) captureHudInstruments(hud);

    static std::uint8_t* previousHud = nullptr;
    static void* previousMap = nullptr;
    static float previousScaleX = 0, previousScaleY = 0;
    static float mapOffset = 0;
    static std::array<HudGroupPosition, 4> anchors{};
    static std::array<HudGroupPosition, 3> independent{};
    constexpr std::uint32_t mapWidgets[] = {
        0x382d2fc9, 0xc46a80a9, 0xb0f63386, // Ring, pursuit heat and its counter.
        0x87c38e97, 0xedfb6d37 // Left tachometer and needle in drag events.
    };
    constexpr std::uint32_t speedWidgets[] = {
        0x5164d4ea, 0x941fff09, 0x87c38e97, 0xc5d551b7, 0x8991bd54, // Gauge groups.
        0xedfb6d37, 0x5df2f5b4 // Needles drawn as separate siblings.
    };
    static HudCorner<std::size(mapWidgets)> mapCorner;
    static std::array<HudCorner<std::size(speedWidgets)>, 2> speedCorners;
    static HudCorner<1> dragCorner;
    static unsigned updates = 0;
    static unsigned settlingFrames = 0;
    const bool newHud = previousHud != hud || previousMap != map;
    const bool modeChanged = previousMode != hud[0x330];
    const bool changed = newHud || modeChanged || previousScaleX != activeLayout.hudX || previousScaleY != activeLayout.hudY;
    // A native widescreen message changes object positions after this update.
    if (changed) settlingFrames = 3;
    if (settlingFrames) --settlingFrames;
    else if (++updates % 8 != 0) return;
    if (newHud) {
        anchors = {};
        independent = {};
        mapCorner = {};
        speedCorners = {};
        dragCorner = {};
        mapOffset = 0;
    }
    previousHud = hud;
    previousMap = map;
    previousScaleX = activeLayout.hudX;
    previousScaleY = activeLayout.hudY;

    auto* leftRoot = FindHudObject(package, 0x157c5b37);
    auto* left = FindHudChild(leftRoot, 0x1603009e);
    auto* right = FindHudObject(package, 0x5d0101f1);
    auto* alternateRight = FindHudObject(package, 0x2fe4ff9c);
    auto* ring = FindHudChild(left, 0x382d2fc9);
    const std::array<void*, 2> rightRoots{right, alternateRight};
    std::array<void*, 2> rightContent{};
    mapCorner.FindChildren(left, mapWidgets);
    for (std::size_t i = 0; i < rightRoots.size(); ++i) {
        rightContent[i] = FindHudChild(rightRoots[i], 0x5c7a5c8a);
        speedCorners[i].FindChildren(rightContent[i], speedWidgets);
    }
    constexpr std::uint32_t dragWidgets[] = {0x87c38e97};
    dragCorner.FindChildren(right, dragWidgets);
    const auto leftOrigin = ReadHudPosition(leftRoot);
    const auto x = HudTravel(activeLayout.hudX);
    const auto y = HudTravel(activeLayout.hudY, 240.0f);
    AnchorHudGroup(anchors[0], left, leftOrigin, {-x, 0}, true, false);
    AnchorHudGroup(anchors[1], right, {}, {x, 0}, true, false);
    AnchorHudGroup(anchors[2], alternateRight, {}, {x, 0}, true, false);

    const auto leftContent = AddHudPoints(leftOrigin, ReadHudPosition(left));
    AnchorHudGroup(anchors[3], FindHudChild(left, 0x3345911d), leftContent, {0, -y}, false, true);
    mapCorner.Align(leftContent, false);
    for (std::size_t i = 0; i < rightRoots.size(); ++i) {
        void* content = rightContent[i];
        const auto origin = AddHudPoints(ReadHudPosition(rightRoots[i]), ReadHudPosition(content));
        speedCorners[i].Align(origin, true);
    }
    dragCorner.Align(ReadHudPosition(right), true);
    LayoutUpperRaceHud(package);

    // These separate package roots contain the Speedbreaker display and
    // other transient race elements. Keep their horizontal animation and
    // slide the whole group along the vertical edge on taller viewports.
    constexpr std::uint32_t independentRoots[] = {0x82d60021, 0x0f365871, 0x00a1a862};
    for (std::size_t i = 0; i < std::size(independentRoots); ++i) {
        auto* object = FindHudObject(package, independentRoots[i]);
        HudBounds bounds{};
        unsigned remaining = 512;
        MeasureHudBounds(object, {}, 0, remaining, bounds);
        if (!bounds.Valid()) continue;
        const float center = (bounds.top + bounds.bottom) * 0.5f - independent[i].applied.y;
        const float travel = center < -80 ? -y : center > 80 ? y : 0;
        AnchorHudGroup(independent[i], object, {}, {0, travel}, true, true);
    }

    // Native widescreen resets the global X pivot before its script moves the
    // backdrop. Preserve the prior patch offset while the script catches up.
    if (map && ring) {
        if (modeChanged) GameValue<float>(0x51cf04) += mapOffset;
        const auto center = AddHudPoints(leftContent, ReadHudPosition(ring));
        const HudPoint delta{center.x - HudField<float>(map, 0xb8), center.y - HudField<float>(map, 0xbc)};
        if (std::isfinite(delta.x) && std::isfinite(delta.y) &&
            (std::fabs(delta.x) > 0.02f || std::fabs(delta.y) > 0.02f) && ShiftMinimap(map, delta)) {
            mapOffset += delta.x;
        }
    }
}

bool InstallResponsiveRaceHud() {
    if (!RedirectCall(0x18cad2, 0x1696c0, reinterpret_cast<std::uintptr_t>(&LayoutRaceHud))) return false;
    RedirectCall(0x116f80, 0x1be800, reinterpret_cast<std::uintptr_t>(&RenderResponsiveRaceTables));
    return true;
}

void LayoutSongNotice() {
    static HudGroupPosition anchor;
    auto* notice = FindHudObject("EA_Trax.fng", 0xa1341735);
    const auto baseline = HudPositionWithoutOffset(anchor, notice);
    if (!notice || HudField<unsigned>(notice,0x18) != 5) return;
    auto* transform = HudField<void*>(notice,0x2c);
    if (!transform) return;
    HudBounds bounds{};
    unsigned remaining = 64;
    MeasureHudBounds(notice, {}, 0, remaining, bounds);
    if (!bounds.Valid()) return;

    const float availableWidth = 640.0f / activeLayout.hudX;
    const float availableHeight = 480.0f / activeLayout.hudY;
    const bool split = unlimited_split_screen::RaceActive();
    // Leave room for the standings of the upper-left player on narrow displays.
    const float widthShare = split ? .30f : .40f;
    const float scale = std::min({.70f, widthShare * availableWidth / (bounds.right - bounds.left),
                                 .15f * availableHeight / (bounds.bottom - bounds.top)});
    const auto origin = ReadHudPosition(notice);
    bounds.left = baseline.x + (bounds.left - origin.x) * scale;
    bounds.right = baseline.x + (bounds.right - origin.x) * scale;
    bounds.top = baseline.y + (bounds.top - origin.y) * scale;
    bounds.bottom = baseline.y + (bounds.bottom - origin.y) * scale;
    auto& xScale = HudField<float>(transform,0x38);
    auto& yScale = HudField<float>(transform,0x3c);
    if (xScale != scale || yScale != scale) {
        xScale = yScale = scale;
        MarkHudTransformDirty(notice);
    }
    auto offset = HudCornerOffset(bounds, activeLayout.hudX, activeLayout.hudY, false, false);
    // Single-player races can draw the police radar at a fixed canvas position.
    // Reserve its space when the song banner would otherwise overlap it.
    constexpr HudBounds meter{-96.0f, -216.0f, 96.0f, -186.0f};
    if (GameValue<std::uint32_t>(gameFlowStateRva) == 6 && !split &&
        bounds.right + offset.x > meter.left && bounds.left + offset.x < meter.right &&
        bounds.bottom + offset.y > meter.top && bounds.top + offset.y < meter.bottom) {
        const float gap = 0.02f * std::min(320.0f / activeLayout.hudX, 240.0f / activeLayout.hudY);
        offset.y = meter.bottom + gap - bounds.top;
    }
    SetHudGroupOffset(anchor, notice, offset);
}

constexpr char songNoticePackage[] = "EA_Trax.fng";
constexpr std::uint32_t songOriginalBadge = 0x000004e5; // EA in the game's texture table.
constexpr std::uint32_t songOriginalTab = 0x575221d4;
constexpr std::uintptr_t showChyronRva = 0x195ed0;
using ShowChyron = void (__cdecl*)(const char*, const char*, const char*);
std::uint64_t discNoticeShown = 0, discNoticeRequested = 0;

void SetDiscNoticeBranding(bool custom, std::shared_ptr<const unlimited_discx::Cover> cover = {}) {
    const bool hasCover = custom && cover != nullptr;
    unlimited_split_screen::SetDiscCover(std::move(cover));
    const struct { std::uint32_t object, replacement; bool replace; } art[] = {
        {songOriginalTab, unlimited_split_screen::discxNoticeTabHash, custom},
        {songOriginalBadge, unlimited_split_screen::discxNoticeEmptyHash, hasCover},
        {0xc56abf24, unlimited_split_screen::discxNoticeEmptyHash, hasCover}, // Inner stripes.
        {unlimited_split_screen::discxCoverBase, unlimited_split_screen::discxNoticeCoverHash, hasCover},
    };
    for (const auto& item : art) {
        auto* image = FindHudObject(songNoticePackage, item.object);
        if (!image || HudField<unsigned>(image, 0x18) != 1) continue;
        auto& texture = HudField<std::uint32_t>(image, 0x24);
        const auto next = item.replace ? item.replacement : item.object;
        if ((texture == item.object || texture == item.replacement) && texture != next) {
            texture = next;
            MarkHudTransformDirty(image);
        }
    }
}

void __cdecl ShowStockSongNotice(const char* title, const char* artist, const char* album) {
    if (unlimited_discx::Selected()) return;
    SetDiscNoticeBranding(false);
    discNoticeShown = discNoticeRequested = 0;
    GameFunction<ShowChyron>(showChyronRva)(title, artist, album);
}

using NoticeStrings = std::array<std::wstring, 3>;
constexpr std::uintptr_t nativeNoticeWidth = 0x186210;
const NoticeStrings* measuringDiscTags = nullptr;
bool nativeDiscWidths = false;

template<unsigned index>
float __fastcall MeasureDiscNotice(void* font, void*, const wchar_t* stock, unsigned flags) {
    const auto* text = measuringDiscTags ? (*measuringDiscTags)[index].c_str() : stock;
    return GameFunction<float (__thiscall*)(void*, const wchar_t*, unsigned)>(nativeNoticeWidth)(font, text, flags);
}

std::wstring FitDiscNoticeText(void* widget, std::wstring text) {
    for (auto& ch : text) if (ch < L' ') ch = L' ';
    if (text.size() > 64) text.resize(64);
    const auto trimBrokenPair = [&] {
        if (!text.empty() && text.back() >= 0xd800 && text.back() <= 0xdbff) text.pop_back();
    };
    trimBrokenPair();
    auto* font = GameFunction<void* (__cdecl*)(std::uint32_t)>(0x185b50)(HudField<std::uint32_t>(widget, 0x24));
    if (font) {
        const auto width = GameFunction<float (__thiscall*)(void*, const wchar_t*, unsigned)>(nativeNoticeWidth);
        const auto flags = HudField<unsigned>(widget, 0x1c);
        if (width(font, text.c_str(), flags) > 275.0f) {
            do {
                text.pop_back();
                trimBrokenPair();
            } while (!text.empty() && width(font, (text + L"…").c_str(), flags) > 275.0f);
            text += L"…";
        }
    }
    return text;
}

void SetDiscNoticeText(void* widget, const wchar_t* text) {
    GameFunction<void (__thiscall*)(void*, const wchar_t*)>(0x1bcd20)(
        static_cast<std::uint8_t*>(widget) + 0x64, text);
    HudField<unsigned>(widget, 0x1c) |= 0x400000u;
}

void ServiceDiscSongNotice(void* frontend) {
    const auto sequence = unlimited_discx::Enabled() && unlimited_discx::MusicActive() ?
        unlimited_discx::TrackSequence() : 0;
    if (!sequence) {
        if (discNoticeShown || discNoticeRequested) {
            SetDiscNoticeBranding(false);
            if (frontend && FindHudObject(songNoticePackage, 0xa1341735))
                GameFunction<void (__thiscall*)(void*, const char*)>(0x1169b0)(frontend, songNoticePackage);
            discNoticeShown = discNoticeRequested = 0;
        }
        return;
    }
    if (!frontend || !unlimited_discx::PlayingInGameMix() ||
        sequence == discNoticeShown || GameValue<int>(0x4ee348) > 0) return;
    if (discNoticeRequested == sequence) return;
    discNoticeRequested = sequence;
    GameFunction<ShowChyron>(showChyronRva)(nullptr, nullptr, nullptr);
}

void __fastcall StartNativeSongNotice(void* card, void*) {
    const auto start = GameFunction<void (__thiscall*)(void*)>(0x18d670);
    if (!unlimited_discx::Selected()) {
        SetDiscNoticeBranding(false);
        start(card);
        return;
    }
    // This callback is dispatched by the native chyron at the end of its
    // gameplay delay, or immediately in the frontend. It also owns expiration.
    const auto sequence = discNoticeRequested;
    if (!sequence || sequence != unlimited_discx::TrackSequence()) return;
    if (!unlimited_discx::PlayingInGameMix()) {
        discNoticeRequested = 0;
        return;
    }
    auto* title = FindHudObject(songNoticePackage, 0xf5387816);
    auto* artist = FindHudObject(songNoticePackage, 0x11db9e17);
    auto* album = FindHudObject(songNoticePackage, 0x48097852);
    const std::array widgets{title, artist, album};
    for (auto* widget : widgets)
        if (!widget || HudField<unsigned>(widget, 0x18) != 2) return;
    const auto track = unlimited_discx::NowPlaying();
    if (track.sequence != sequence) return;

    const NoticeStrings tags{FitDiscNoticeText(title, track.title), FitDiscNoticeText(artist, track.artist),
                             FitDiscNoticeText(album, track.album)};
    // Start takes format strings. Keep the metadata in UTF-16 and give the
    // game's three native measurements the same text that FEng will draw.
    constexpr std::uintptr_t tagBase = 0x4f3c48;
    std::array<const char*, 3> original{};
    std::array<std::string, 3> placeholders{};
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (!nativeDiscWidths) placeholders[i].assign(std::min<std::size_t>(40, tags[i].size()), 'M');
        auto& native = GameValue<const char*>(tagBase + i * sizeof(const char*));
        original[i] = native;
        native = placeholders[i].c_str();
    }
    measuringDiscTags = nativeDiscWidths ? &tags : nullptr;
    start(card);
    measuringDiscTags = nullptr;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        GameValue<const char*>(tagBase + i * sizeof(const char*)) = original[i];
        SetDiscNoticeText(widgets[i], tags[i].c_str());
    }
    SetDiscNoticeBranding(true, track.cover);
    discNoticeShown = sequence;
}

void InstallDiscxSongRouting() {
    if (!RedirectCall(0x195eb9, 0x18d670, reinterpret_cast<std::uintptr_t>(&StartNativeSongNotice))) return;
    constexpr std::uintptr_t sites[] = {0xf5001, 0x1644bc, 0x1644e4};
    const auto replacement = reinterpret_cast<std::uintptr_t>(&ShowStockSongNotice);
    for (auto site : sites) RedirectCall(site, showChyronRva, replacement);

    constexpr std::uintptr_t widthSites[]{0x18d6ef, 0x18d725, 0x18d767};
    const std::uintptr_t widths[]{reinterpret_cast<std::uintptr_t>(&MeasureDiscNotice<0>),
                                  reinterpret_cast<std::uintptr_t>(&MeasureDiscNotice<1>),
                                  reinterpret_cast<std::uintptr_t>(&MeasureDiscNotice<2>)};
    nativeDiscWidths = true;
    for (unsigned i = 0; i < std::size(widthSites); ++i)
        nativeDiscWidths = RedirectCall(widthSites[i], nativeNoticeWidth, widths[i]) && nativeDiscWidths;
    // Partial installation is safe because inactive hooks always pass through.
}

void __fastcall UpdateFrontendSongNotice(void* frontend, void*, int ticks, unsigned lock) {
    GameFunction<void (__thiscall*)(void*, int, unsigned)>(0x1c53c0)(frontend, ticks, lock);
    unlimited_language::ServiceApply();
    ServiceDiscSongNotice(GameValue<void*>(0x51cadc));
    if (responsiveSongNoticeEnabled) LayoutSongNotice();
}

bool InstallResponsiveSongNotice(bool scaleSongNotice) {
    constexpr std::uintptr_t update = 0x1c53c0;
    constexpr std::uintptr_t first = 0x116a0d, second = 0x16441f;
    auto original = GameValue<std::array<std::uint8_t, 5>>(first);
    auto replacement = reinterpret_cast<std::uintptr_t>(&UpdateFrontendSongNotice);
    if (!RedirectCall(first, update, replacement)) return false;
    if (RedirectCall(second, update, replacement)) {
        responsiveSongNoticeEnabled = scaleSongNotice;
        InstallDiscxSongRouting();
        return true;
    }
    WriteGameBytes(first, original.data(), original.size());
    return false;
}

} // namespace
