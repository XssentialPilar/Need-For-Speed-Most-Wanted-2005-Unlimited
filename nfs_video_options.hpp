#pragma once

constexpr std::uintptr_t visualTreatment = 0x501828;
constexpr std::uintptr_t motionBlurEnable = 0x5017dc;
constexpr unsigned minRenderScale = 4;
constexpr unsigned maxRenderScale = 20;
constexpr int motionBlurMax = 200;
int filtersPreference = -1;
int blurPreference = -1;
int blurStrength = 100;
float blurMultiplier = 1.1f;
unsigned renderScaleTenths = 10;
bool effectsPageInstalled = false;
bool blurIntensityInstalled = false;
wchar_t videoPreferences[MAX_PATH]{};

void LoadVideoEffectsSettings() {
    if (!settingsPath[0]) return;
    unlimited_settings::UserFile(videoPreferences, L"Video");
    auto read = [](const wchar_t* key, int fallback) {
        const auto installed = static_cast<int>(GetPrivateProfileIntW(L"Graphics", key, fallback, settingsPath));
        return videoPreferences[0]
            ? static_cast<int>(GetPrivateProfileIntW(L"Graphics", key, installed, videoPreferences)) : installed;
    };
    filtersPreference = std::clamp(read(L"GameFilters", -1), -1, 1);
    blurPreference = std::clamp(read(L"MotionBlur", -1), -1, 1);
    blurStrength = std::clamp(read(L"MotionBlurIntensity", 100), 10, motionBlurMax);
    blurMultiplier = 1.1f * blurStrength / 100.f;
    renderScaleTenths = std::clamp(read(L"RenderScaleTenths", LowThreadBudget() ? 8 : 10), int(minRenderScale), int(maxRenderScale));
    rearviewEffects = static_cast<RearviewEffects>(std::clamp(read(rearviewEffectsKey, 0), 0, 2));
    unlimited_textures::Open(settingsPath);
    using Quality = unlimited_textures::Quality;
    unlimited_textures::preference = unlimited_textures::ReadQuality(read(unlimited_textures::preferenceKey, 0));
    unlimited_textures::startup = unlimited_textures::remaster.count ? unlimited_textures::preference : Quality::Original;
    unlimited_split_screen::SetRenderScale(renderScaleTenths);
}

void ApplyPostPreferences() {
    if (filtersPreference >= 0) GameValue<int>(visualTreatment) = filtersPreference;
    if (blurPreference >= 0) GameValue<int>(motionBlurEnable) = blurPreference;
    unlimited_split_screen::SetPostPreferences(filtersPreference, blurPreference);
}

bool SaveVideoSetting(const wchar_t* key, int setting) {
    if (!settingsPath[0]) return false;
    wchar_t value[16]{};
    std::swprintf(value, std::size(value), L"%d", setting);
    return WritePrivateProfileStringW(L"Graphics", key, value, videoPreferences[0] ? videoPreferences : settingsPath);
}

struct VideoEffectsLabels {
    const char *page, *filters, *motionBlur, *intensity, *scale, *on, *off;
};

VideoEffectsLabels VideoEffectsText(int language) {
    switch (language) {
    case 1: return {u8"Effets avancés", "Filtres du jeu", "Flou de mouvement", u8"Intensité du flou", u8"Échelle de rendu", u8"Activé", u8"Désactivé"};
    case 2: return {"Erweiterte Effekte", "Spielfilter", u8"Bewegungsunschärfe", u8"Stärke der Unschärfe", u8"Renderauflösung", "Ein", "Aus"};
    case 3: return {"Effetti avanzati", "Filtri di gioco", "Sfocatura di movimento", u8"Intensità sfocatura", "Scala di rendering", "Attivo", "Disattivo"};
    case 4: return {"Efectos avanzados", "Filtros del juego", "Desenfoque de movimiento", "Intensidad del desenfoque", "Escala de resolución", "Activado", "Desactivado"};
    case 5: return {"Geavanceerde effecten", "Spelfilters", "Bewegingsonscherpte", "Sterkte bewegingsonscherpte", "Renderschaal", "Aan", "Uit"};
    case 6: return {"Avancerade effekter", "Spelfilter", u8"Rörelseoskärpa", u8"Oskärpans styrka", "Renderingsskala", u8"På", "Av"};
    case 7: return {"Avancerede effekter", "Spilfiltre", u8"Bevægelsessløring", u8"Sløringsstyrke", "Gengivelsesskala", "Til", "Fra"};
    case 8: return {u8"고급 효과", u8"게임 필터", u8"모션 블러", u8"모션 블러 강도", u8"렌더링 배율", u8"켜기", u8"끄기"};
    case 9: return {u8"進階效果", u8"遊戲濾鏡", u8"動態模糊", u8"模糊強度", u8"渲染比例", u8"開啟", u8"關閉"};
    case 10: return {u8"詳細エフェクト", u8"ゲームフィルター", u8"モーションブラー", u8"ブラーの強度", u8"解像度スケール", u8"オン", u8"オフ"};
    case 11: return {u8"เอฟเฟกต์ขั้นสูง", u8"ฟิลเตอร์เกม", u8"ภาพเบลอขณะเคลื่อนไหว", u8"ความเข้มของภาพเบลอ", u8"สเกลความละเอียด", u8"เปิด", u8"ปิด"};
    case 12: return {"Efekty zaawansowane", "Filtry gry", "Rozmycie ruchu", u8"Intensywność rozmycia", "Skala renderowania", u8"Wł.", u8"Wył."};
    case 13: return {u8"Tehosteiden lisäasetukset", "Pelisuodattimet", u8"Liike-epäterävyys", u8"Sumennuksen voimakkuus", "Renderöintiskaala", u8"Päällä", "Pois"};
    default: return {"Advanced effects", "Game filters", "Motion blur", "Blur intensity", "Resolution scale", "On", "Off"};
    }
}

int VideoOptionDirection(std::int32_t message) {
    return message == -1860157282 ? -1 : message == -1248388111 ? 1 : 0;
}

struct EATraxPlaylistEntry {
    int index;
    std::uint8_t playability;
    std::uint8_t padding[3];
};
static_assert(sizeof(EATraxPlaylistEntry) == 8);

EATraxPlaylistEntry* ActiveEATraxPlaylist(int& count) {
    count = GameValue<int>(0x4f42c4);
    // PathFinder uses a 28-bit mask. The original bank has 26 licensed entries.
    if (count <= 0 || count > 28) return nullptr;
    auto* database = GameValue<void*>(0x51cf90);
    auto* profile = database ? HudField<std::uint8_t*>(database, 0x10) : nullptr;
    auto** songs = GameValue<std::uint8_t**>(0x5123d0);
    if (!profile || !songs || GameValue<std::uint8_t**>(0x5123d4) != songs + count) return nullptr;
    for (int id = 0; id < count; ++id) {
        if (!songs[id] || !HudField<std::uint32_t>(songs[id], 0x10)) return nullptr;
    }
    auto* playlist = reinterpret_cast<EATraxPlaylistEntry*>(profile + 0x324);
    std::uint32_t indices = 0;
    for (int row = 0; row < count; ++row) {
        const auto id = playlist[row].index;
        if (id < 0 || id >= count || (indices & (1u << id))) return nullptr;
        indices |= 1u << id;
    }
    return playlist;
}

bool EATraxFullyActive() {
    int count;
    const auto* playlist = ActiveEATraxPlaylist(count);
    return playlist && std::all_of(playlist, playlist + count, [](auto entry) { return entry.playability == 3; });
}

bool RestoreEATraxRotation() {
    int count;
    auto* playlist = ActiveEATraxPlaylist(count);
    if (!playlist) return false;
    for (int row = 0; row < count; ++row) playlist[row].playability = 3;
    return true;
}

void __cdecl InitializeNewEATraxPlaylist(bool reset) {
    RestoreEATraxRotation();
    GameFunction<void (__cdecl*)(bool)>(0x0df330)(reset);
}

bool InstallEATraxDefaults() {
    return RedirectCall(0x1ab84d, 0x0df330, reinterpret_cast<std::uintptr_t>(&InitializeNewEATraxPlaylist));
}

struct EATraxLabels { const char* restore; const char* active; };
EATraxLabels EATraxText(int language) {
    switch (language) {
    case 1: return {"Tout activer", "Tout est actif"};
    case 2: return {"Alle aktivieren", "Alle aktiv"};
    case 3: return {"Attiva tutti", "Tutti attivi"};
    case 4: return {"Activar todas", "Todas activas"};
    case 5: return {"Alles inschakelen", "Alles actief"};
    case 6: return {"Aktivera alla", "Alla aktiva"};
    case 7: return {u8"Aktivér alle", "Alle aktive"};
    case 8: return {u8"모두 켜기", u8"모두 활성화됨"};
    case 9: return {u8"全部啟用", u8"全部已啟用"};
    case 10: return {u8"すべて有効にする", u8"すべて有効"};
    case 11: return {u8"เปิดเพลงทั้งหมด", u8"เปิดทั้งหมดแล้ว"};
    case 12: return {u8"Włącz wszystkie", "Wszystkie aktywne"};
    case 13: return {u8"Ota kaikki käyttöön", u8"Kaikki käytössä"};
    default: return {"Enable all", "All enabled"};
    }
}

// Language IDs match the game's language table.
struct RearviewLabels { const char* title; const char* lights; const char* particles; };

RearviewLabels RearviewEffectsText(int language) {
    switch (language) {
    case 1: return {u8"Effets du rétroviseur", u8"Lumières", u8"Lumières et particules"};
    case 2: return {u8"Rückspiegeleffekte", "Lichter", "Lichter und Partikel"};
    case 3: return {"Effetti retrovisore", "Luci", "Luci e particelle"};
    case 4: return {"Efectos del retrovisor", "Luces", u8"Luces y partículas"};
    case 5: return {"Spiegeleffecten", "Lichten", "Lichten en deeltjes"};
    case 6: return {u8"Backspegeleffekter", "Ljus", "Ljus och partiklar"};
    case 7: return {"Bakspejlseffekter", "Lys", "Lys og partikler"};
    case 8: return {u8"백미러 효과", u8"조명", u8"조명 및 파티클"};
    case 9: return {u8"後視鏡特效", u8"燈光", u8"燈光與粒子"};
    case 10: return {u8"バックミラー効果", u8"ライト", u8"ライトと粒子"};
    case 11: return {u8"เอฟเฟกต์กระจกมองหลัง", u8"แสงไฟ", u8"แสงไฟและอนุภาค"};
    case 12: return {u8"Efekty w lusterku", u8"Światła", u8"Światła i cząsteczki"};
    case 13: return {u8"Taustapeilin tehosteet", "Valot", "Valot ja hiukkaset"};
    default: return {"Rearview effects", "Lights", "Lights and particles"};
    }
}

struct TextureQualityLabels {
    const char* title;
    const char* restart;
    const char* values[std::size(unlimited_textures::choices)];
};

TextureQualityLabels TextureQualityText(int language) {
    switch (language) {
    case 1: return {"Textures", u8"Redémarrer", {u8"Très faible", "Faible", "Moyenne", u8"Élevée", u8"Très élevée", "Ultra", "Originales"}};
    case 2: return {"Texturen", "Neustart", {"Sehr niedrig", "Niedrig", "Mittel", "Hoch", "Sehr hoch", "Ultra", "Original"}};
    case 3: return {"Texture", "Riavvio", {"Molto bassa", "Bassa", "Media", "Alta", "Molto alta", "Ultra", "Originali"}};
    case 4: return {"Texturas", "Reiniciar", {"Muy bajo", "Bajo", "Medio", "Alto", "Muy alta", "Ultra", "Original"}};
    case 5: return {"Texturen", "Herstarten", {"Zeer laag", "Laag", "Gemiddeld", "Hoog", "Zeer hoog", "Ultra", "Origineel"}};
    case 6: return {"Texturer", "Starta om", {u8"Mycket låg", u8"Låg", "Medel", u8"Hög", u8"Mycket hög", "Ultra", "Original"}};
    case 7: return {"Teksturer", "Genstart", {"Meget lav", "Lav", "Mellem", u8"Høj", u8"Meget høj", "Ultra", "Original"}};
    case 8: return {u8"텍스처", u8"재시작", {u8"매우 낮음", u8"낮음", u8"중간", u8"높음", u8"매우 높음", "Ultra", u8"원본"}};
    case 9: return {u8"紋理", u8"重新啟動", {u8"非常低", u8"低", u8"中", u8"高", u8"非常高", "Ultra", u8"原始"}};
    case 10: return {u8"テクスチャ", u8"再起動", {u8"最低", u8"低", u8"中", u8"高", u8"最高", "Ultra", u8"オリジナル"}};
    case 11: return {u8"พื้นผิว", u8"เริ่มใหม่", {u8"ต่ำมาก", u8"ต่ำ", u8"กลาง", u8"สูง", u8"สูงมาก", "Ultra", u8"ดั้งเดิม"}};
    case 12: return {"Tekstury", u8"Uruchom ponownie", {"Bardzo niska", "Niska", u8"Średnia", "Wysoka", "Bardzo wysoka", "Ultra", "Oryginalne"}};
    case 13: return {"Tekstuurit", u8"Käynnistä uudelleen", {"Erittäin matala", "Matala", "Keskitaso", "Korkea", "Erittäin korkea", "Ultra", u8"Alkuperäinen"}};
    default: return {"Textures", "Restart", {"Very Low", "Low", "Medium", "High", "Very High", "Ultra", "Original"}};
    }
}

enum class VideoEffect { Filters, Blur, Strength, Scale, Rearview, Tracks, Textures };

bool PostEffectEnabled(VideoEffect setting) {
    const bool filters = setting == VideoEffect::Filters;
    const auto preference = filters ? filtersPreference : blurPreference;
    return preference >= 0 ? preference != 0 : GameValue<int>(filters ? visualTreatment : motionBlurEnable) != 0;
}

template<VideoEffect setting>
void __fastcall DrawVideoEffect(std::uint8_t* option) {
    const auto language = GameValue<int>(0x4f41c0);
    const auto text = VideoEffectsText(language);
    if constexpr (setting == VideoEffect::Tracks) {
        const auto songs = EATraxText(language);
        using FindString = const char* (__fastcall*)(void*, std::uint32_t);
        const auto* title = GameFunction<FindString>(0x16bb80)(nullptr, 0xdcfb6b36);
        SetOptionText(option, title ? title : "EA Trax", EATraxFullyActive() ? songs.active : songs.restore);
    } else if constexpr (setting == VideoEffect::Rearview) {
        const auto mirror = RearviewEffectsText(language);
        const char* values[]{text.off, mirror.lights, mirror.particles};
        SetOptionText(option, mirror.title, values[static_cast<unsigned>(rearviewEffects)]);
    } else if constexpr (setting == VideoEffect::Textures) {
        const auto textures = TextureQualityText(language);
        const auto* value = textures.values[unlimited_textures::QualityIndex(unlimited_textures::preference)];
        char pending[96]{};
        if (unlimited_textures::startup != unlimited_textures::preference) {
            std::snprintf(pending, sizeof(pending), "%s (%s)", value, textures.restart);
            value = pending;
        }
        SetOptionText(option, textures.title, value);
    } else if constexpr (setting == VideoEffect::Filters || setting == VideoEffect::Blur) {
        SetOptionText(option, setting == VideoEffect::Filters ? text.filters : text.motionBlur,
                      PostEffectEnabled(setting) ? text.on : text.off);
    } else {
        char value[24]{};
        if constexpr (setting == VideoEffect::Strength) {
            std::snprintf(value, sizeof(value), "%d%%", blurStrength);
            SetOptionText(option, text.intensity, PostEffectEnabled(VideoEffect::Blur) ? value : "-");
        } else {
            const auto lang = GameValue<int>(0x4f41c0);
            const char decimal = lang == 0 || lang == 8 || lang == 9 ? '.' : ',';
            std::snprintf(value, sizeof(value), "%u%c%u", renderScaleTenths / 10, decimal, renderScaleTenths % 10);
            SetOptionText(option, text.scale, value);
        }
    }
}

// The sibling widget is recreated by the game on every page transition.
std::uint8_t* blurIntensityOption = nullptr;

template<VideoEffect setting>
void __fastcall ChangeVideoEffect(std::uint8_t* option, void*, const char*, std::int32_t message) {
    const int direction = VideoOptionDirection(message);
    if (!direction) return;
    if constexpr (setting == VideoEffect::Tracks) {
        if (EATraxFullyActive() || !RestoreEATraxRotation()) return;
        GameFunction<void (__cdecl*)(bool)>(0x0df330)(true);
    } else if constexpr (setting == VideoEffect::Rearview) {
        const int next = (static_cast<int>(rearviewEffects) + direction + 3) % 3;
        if (!SaveVideoSetting(rearviewEffectsKey, next)) return;
        rearviewEffects = static_cast<RearviewEffects>(next);
    } else if constexpr (setting == VideoEffect::Textures) {
        const auto next = unlimited_textures::ChangeQuality(unlimited_textures::preference, direction);
        if (!SaveVideoSetting(unlimited_textures::preferenceKey, static_cast<int>(next))) return;
        unlimited_textures::preference = next;
    } else if constexpr (setting == VideoEffect::Filters || setting == VideoEffect::Blur) {
        const bool filters = setting == VideoEffect::Filters;
        const int next = !PostEffectEnabled(setting);
        if (!SaveVideoSetting(filters ? L"GameFilters" : L"MotionBlur", next)) return;
        (filters ? filtersPreference : blurPreference) = next;
        ApplyPostPreferences();
        if (!filters && blurIntensityOption) DrawVideoEffect<VideoEffect::Strength>(blurIntensityOption);
    } else if constexpr (setting == VideoEffect::Strength) {
        if (!PostEffectEnabled(VideoEffect::Blur)) return;
        const int next = std::clamp(blurStrength + direction * 10, 10, motionBlurMax);
        if (next == blurStrength || !SaveVideoSetting(L"MotionBlurIntensity", next)) return;
        blurStrength = next;
        blurMultiplier = 1.1f * blurStrength / 100.f;
    } else {
        const int next = std::clamp(int(renderScaleTenths) + direction, int(minRenderScale), int(maxRenderScale));
        if (next == int(renderScaleTenths) || !SaveVideoSetting(L"RenderScaleTenths", next)) return;
        renderScaleTenths = next;
        unlimited_split_screen::SetRenderScale(renderScaleTenths);
    }
    option[0x2a] = 1;
    DrawVideoEffect<setting>(option);
}

bool InstallMotionBlurIntensity() {
    constexpr std::uintptr_t operands[]{0x2dbd46, 0x2dbd56, 0x2dbd66, 0x2dbd74};
    constexpr std::uint32_t native = 0x8910c4;
    constexpr std::uint8_t opcode[]{0xd8, 0x0d};
    for (const auto operand : operands)
        if (!MatchGameBytes(operand - 2, opcode, sizeof(opcode)) || !MatchGameBytes(operand, &native, sizeof(native)))
            return false;
    const auto replacement = reinterpret_cast<std::uintptr_t>(&blurMultiplier);
    std::size_t i = 0;
    for (; i < std::size(operands); ++i)
        if (!WriteGameBytes(operands[i], &replacement, sizeof(replacement))) break;
    if (i == std::size(operands)) return true;
    while (i) WriteGameBytes(operands[--i], &native, sizeof(native));
    return false;
}

void BuildVideoEffectsPage(std::uint8_t* screen) {
    static std::array<void*,15> filters{}, blur{}, intensity{}, scale{}, mirror{}, tracks{}, textures{};
    if (unlimited_textures::installed)
        AddUnlimitedVideoOption(screen, textures, &ChangeVideoEffect<VideoEffect::Textures>, &DrawVideoEffect<VideoEffect::Textures>);
    AddUnlimitedVideoOption(screen, filters, &ChangeVideoEffect<VideoEffect::Filters>, &DrawVideoEffect<VideoEffect::Filters>);
    AddUnlimitedVideoOption(screen, blur, &ChangeVideoEffect<VideoEffect::Blur>, &DrawVideoEffect<VideoEffect::Blur>);
    if (blurIntensityInstalled)
        blurIntensityOption = AddUnlimitedVideoOption(screen, intensity, &ChangeVideoEffect<VideoEffect::Strength>, &DrawVideoEffect<VideoEffect::Strength>);
    AddUnlimitedVideoOption(screen, scale, &ChangeVideoEffect<VideoEffect::Scale>, &DrawVideoEffect<VideoEffect::Scale>);
    if (rearviewEffectsInstalled)
        AddUnlimitedVideoOption(screen, mirror, &ChangeVideoEffect<VideoEffect::Rearview>, &DrawVideoEffect<VideoEffect::Rearview>);
    AddUnlimitedVideoOption(screen, tracks, &ChangeVideoEffect<VideoEffect::Tracks>, &DrawVideoEffect<VideoEffect::Tracks>);
}

void LabelVideoPageButton(const char* package, std::uint8_t page) {
    if (page != 1 || !package) return;
    if (auto* label = FindHudObject(package, 0xce757c8a)) {
        if (HudField<int>(label, 0x18) != 2) return;
        GameFunction<void (__thiscall*)(void*, const char*)>(0x1bcc10)(label, VideoEffectsText(GameValue<int>(0x4f41c0)).page);
        HudField<std::uint32_t>(label, 0x1c) |= 0x400000u;
    }
}

constexpr const char* videoOptionPackages[]{"OptionsPCDisplay.fng", "Pause_PCDisplayOptions.fng"};
bool videoNavigationInstalled = false;

bool IsVideoOptionsPackage(const char* name) {
    if (!name) return false;
    return std::any_of(std::begin(videoOptionPackages), std::end(videoOptionPackages),
                       [name](const char* package) { return _stricmp(name, package) == 0; });
}

int VideoOptionIndex(std::uint8_t* screen, void* option) {
    return GameFunction<int (__thiscall*)(void*, void*)>(0x188d60)(screen, option);
}

void KeepVideoSelectionInView(std::uint8_t* screen) {
    const int total = HudField<int>(screen, 0x104) - 1;
    const int visible = HudField<int>(screen, 0x10c);
    const int top = VideoOptionIndex(screen, HudField<void*>(screen, 0x38));
    if (top < 1 || top > total || total > 32 || visible < 1 || visible > 32) return;
    const int bottom = std::min(total, top + visible - 1);
    auto* selected = HudField<std::uint8_t*>(screen, 0x34);
    int index = VideoOptionIndex(screen, selected);
    if (index < top || index > bottom || !selected || !selected[0x28]) {
        const int preferred = std::clamp(index ? index : HudField<int>(screen, 0x108), top, bottom);
        const auto itemAt = GameFunction<std::uint8_t* (__thiscall*)(void*, int)>(0x5d380);
        int closest = bottom - top + 1;
        selected = nullptr;
        for (int row = top; row <= bottom; ++row) {
            auto* node = itemAt(screen + 0x2c, row - 1);
            const int distance = std::abs(row - preferred);
            if (node && (node - 4)[0x28] && distance < closest) {
                closest = distance;
                index = row;
                selected = node - 4;
            }
        }
        if (!selected) return;
        GameFunction<void (__thiscall*)(void*, void*)>(0x19bfb0)(screen, selected);
    }
    HudField<int>(screen, 0x108) = index;
    screen[0x11f] = 0;
    if (screen[0x11e]) {
        using UpdateBar = void (__thiscall*)(void*, int, int, int, int);
        GameFunction<UpdateBar>(0x189870)(screen + 0x54, visible, total, top, index);
    }
}

void __fastcall OnVideoWidgetMessage(std::uint8_t* screen, void*, std::uint32_t message,
                                    void* object, unsigned first, unsigned second) {
    constexpr std::uint32_t up = 0x72619778, down = 0x911c0a4b;
    constexpr std::uint32_t left = 0x9120409e, right = 0xb5971bf1;
    constexpr std::uint32_t nativeMouse = 0x9803f6e2, scrollbar = 0xc98356ba;
    const bool video = IsVideoOptionsPackage(HudField<const char*>(screen, 0x10));
    const bool navigation = message == up || message == down || message == left || message == right;
    if (video && navigation && screen[0x120] && (screen[0x11f] || !HudField<void*>(screen, 0x34)))
        KeepVideoSelectionInView(screen);

    using Notify = void (__thiscall*)(void*, std::uint32_t, void*, unsigned, unsigned);
    GameFunction<Notify>(0x1a96c0)(screen, message, object, first, second);

    // The stock scrollbar moves the view independently. Its next directional
    // input would otherwise snap the view back to the now hidden selection.
    if (video && (message == nativeMouse || message == scrollbar) && screen[0x120] && screen[0x11f])
        KeepVideoSelectionInView(screen);
}

bool InstallVideoNavigation() {
    videoNavigationInstalled = RedirectCall(0x145780, 0x1a96c0,
                                            reinterpret_cast<std::uintptr_t>(&OnVideoWidgetMessage));
    return videoNavigationInstalled;
}

extern "C" bool UnlimitedScrollVideoOptions(int delta) {
    if (!videoNavigationInstalled) return false;
    auto* frontend = GameValue<void*>(0x51cadc);
    const auto isTop = GameFunction<bool (__thiscall*)(void*, const char*)>(0x116b90);
    const auto lookup = GameFunction<std::uint8_t* (__cdecl*)(const char*)>(0x191e90);
    auto* screen = static_cast<std::uint8_t*>(nullptr);
    if (frontend) {
        for (const auto* name : videoOptionPackages)
            if (isTop(frontend, name)) { screen = lookup(name); break; }
    }
    static std::uint8_t* previous = nullptr;
    static unsigned page = 0;
    static int remainder = 0;
    if (!screen || !screen[0x120]) {
        previous = nullptr;
        remainder = 0;
        return false;
    }
    if (previous != screen || page != screen[0x154]) remainder = 0;
    previous = screen;
    page = screen[0x154];
    remainder += delta;
    const int steps = remainder / WHEEL_DELTA;
    remainder %= WHEEL_DELTA;
    if (steps) {
        const int total = HudField<int>(screen, 0x104) - 1;
        const int visible = HudField<int>(screen, 0x10c);
        if (visible > 0 && total > visible && HudField<void*>(screen, 0x38)) {
            GameFunction<void (__thiscall*)(void*, int)>(0x1a5d40)(screen, -std::clamp(steps, -15, 15));
            KeepVideoSelectionInView(screen);
        } else {
            // The short pages still let users move between options with the wheel.
            constexpr std::uintptr_t scroll = 0x1a58c0;
            for (int i=0; i<std::min(std::abs(steps), 15); ++i) {
                const int current = VideoOptionIndex(screen, HudField<void*>(screen, 0x34));
                if (current && (steps > 0 ? current <= 1 : current >= total)) break;
                GameFunction<void (__thiscall*)(void*, int)>(scroll)(screen, steps > 0 ? -1 : 1);
            }
        }
    }
    return true;
}

void __fastcall SelectFirstVideoOption(std::uint8_t* screen, void*, int) {
    // Passing zero selects the package footer when it has one. After a page
    // rebuild, pick the first available list row and reset its visible range.
    GameFunction<void (__thiscall*)(void*, int)>(0x1a5ea0)(screen, 1);
}

void __fastcall NextNativeVideoPage(std::uint8_t* screen, void*) {
    auto& page = screen[0x154];
    page = (page + 1) % 3;
    const auto rebuild = HudField<void (__thiscall**)(std::uint8_t*)>(screen, 0)[3];
    rebuild(screen);
    const auto* name = HudField<const char*>(screen, 0x10);
    auto* frontend = GameValue<void*>(0x51cadc);
    using FindPackage = std::uint8_t* (__thiscall*)(void*, const char*);
    // The page button can disable FEng input before its notification runs.
    // The added page is rebuilt in place, so no new package acquires focus.
    if (auto* package = frontend && name ? GameFunction<FindPackage>(0x116af0)(frontend, name) : nullptr)
        HudField<std::uint8_t>(package, 0x28) = 1;
}

__attribute__((naked)) void BranchVideoPage() {
    __asm__ volatile(
        "movb 0x154(%ebx), %al\n"
        "cmpb $2, %al\n"
        "je 1f\n"
        "testb %al, %al\n"
        "jne 2f\n"
        "pushl $0x529748\n"
        "retl\n"
        "1: pushl $0x529bcc\n"
        "retl\n"
        "2: pushl $0x5297dc\n"
        "retl\n");
}

bool InstallVideoEffectsPage() {
    // The stock button rebuilds the same native FEng package. Add a third
    // page and keep the 13 factory controls on their original page.
    constexpr std::uintptr_t nextSite = 0x145a91, branchSite = 0x12973a;
    constexpr std::uint8_t nextStock[]{0x8a,0x83,0x54,0x01,0,0,0x8b,0x13,0x84,0xc0,0x0f,0x94,0xc0,
                                       0x8b,0xcb,0x88,0x83,0x54,0x01,0,0,0xff,0x52,0x0c};
    constexpr std::uint8_t branchStock[]{0x8a,0x83,0x54,0x01,0,0,0x84,0xc0,0x0f,0x85,0x94,0,0,0};
    if (!MatchGameBytes(nextSite, nextStock, sizeof(nextStock)) ||
        !MatchGameBytes(branchSite, branchStock, sizeof(branchStock))) return false;
    std::uint8_t next[sizeof(nextStock)], branch[sizeof(branchStock)];
    std::fill_n(next, sizeof(next), 0x90);
    std::fill_n(branch, sizeof(branch), 0x90);
    next[0] = 0x8b; next[1] = 0xcb; next[2] = 0xe8;
    branch[0] = 0xe9;
    const auto base = reinterpret_cast<std::uintptr_t>(engine);
    const auto nextDelta = reinterpret_cast<std::uintptr_t>(&NextNativeVideoPage) - (base + nextSite + 7);
    const auto branchDelta = reinterpret_cast<std::uintptr_t>(&BranchVideoPage) - (base + branchSite + 5);
    std::memcpy(next + 3, &nextDelta, sizeof(nextDelta));
    std::memcpy(branch + 1, &branchDelta, sizeof(branchDelta));
    if (!WriteGameBytes(branchSite, branch, sizeof(branch))) return false;
    if (WriteGameBytes(nextSite, next, sizeof(next))) return true;
    WriteGameBytes(branchSite, branchStock, sizeof(branchStock));
    return false;
}
