// The library uses the game's rendering and focus. Its Unicode text is rasterized
// as a cached layer so track names are independent of the current game language.
static bool g_discxOpen = false;
static bool g_discxRouteReady = false;
static bool g_discxShuffle = false;
static auto g_discxRepeat = unlimited_discx::Repeat::All;
static int g_discxVolume = 100;
static HWND g_discxWindow = nullptr;
static bool g_discxWindowActive = false;
static bool g_discxMinimized = false;
static bool g_discxClosing = false;
static bool g_discxMouseDown = false;
static bool g_discxBackPending = false;
static int g_discxWheel = 0;
static void* g_discxOwner = nullptr;
static MenuInput g_discxInput{};
static POINT g_discxClick{};
static bool g_discxClicked = false;
static unsigned g_discxRevision = 0;
static IDirect3DTexture9* g_discxTextLayer = nullptr;

enum class DiscFacet { Songs, Albums, Artists, Composers };
static DiscFacet g_discxFacet = DiscFacet::Songs;
static std::wstring g_discxGroup;
static bool g_discxInGroup = false;
static unlimited_discx::Catalog g_discxCatalog;
static std::vector<size_t> g_discxTracks;
static std::vector<std::wstring> g_discxGroups;
static size_t g_discxRow = 0, g_discxTop = 0;

struct DiscText {
    const wchar_t *songs, *albums, *artists, *composers, *unknown, *status, *empty,
                  *decoderMissing, *failed, *groupHelp, *songHelp, *play, *pause,
                  *previous, *next, *stop, *shuffle, *repeat, *off, *all, *one,
                  *refresh, *back, *volume, *scanning, *tracks;
};

static const DiscText& DiscStrings() {
    static constexpr DiscText labels[] = {{
        L"Songs", L"Albums", L"Artists", L"Composers", L"Unknown", L"Now playing",
        L"Add audio files to UnlimitedMusic and select Refresh.",
        L"The Discx decoder is missing. Reinstall Unlimited to restore it.",
        L"Could not play the selected track.", L"Enter to browse  •  Esc to go back",
        L"Enter to play  •  Space to pause  •  Esc to go back",
        L"Play", L"Pause", L"Previous", L"Next", L"Stop", L"Shuffle", L"Repeat",
        L"Off", L"All", L"One", L"Refresh", L"Back", L"Volume", L"Scanning library…", L"tracks"
    }, {
        L"Titres", L"Albums", L"Artistes", L"Compositeurs", L"Inconnu", L"En cours de lecture",
        L"Ajoutez des fichiers audio à UnlimitedMusic, puis choisissez Actualiser.",
        L"Le décodeur Discx est introuvable. Réinstallez Unlimited.",
        L"Impossible de lire le titre sélectionné.", L"Entrée pour parcourir  •  Échap pour revenir",
        L"Entrée pour lire  •  Espace pour mettre en pause  •  Échap pour revenir",
        L"Lire", L"Pause", L"Précédent", L"Suivant", L"Arrêter", L"Aléatoire", L"Répétition",
        L"Non", L"Tout", L"Un titre", L"Actualiser", L"Retour", L"Volume", L"Analyse de la bibliothèque…", L"titres"
    }, {
        L"Titel", L"Alben", L"Interpreten", L"Komponisten", L"Unbekannt", L"Aktuelle Wiedergabe",
        L"Audiodateien in UnlimitedMusic ablegen und Aktualisieren wählen.",
        L"Der Discx-Decoder fehlt. Installiere Unlimited erneut.",
        L"Der ausgewählte Titel konnte nicht abgespielt werden.", L"Enter: Öffnen  •  Esc: Zurück",
        L"Enter: Abspielen  •  Leertaste: Pause  •  Esc: Zurück",
        L"Abspielen", L"Pause", L"Zurück", L"Weiter", L"Stopp", L"Zufall", L"Schleife",
        L"Aus", L"Alle", L"Ein Titel", L"Aktualisieren", L"Zurück", L"Lautstärke", L"Bibliothek wird eingelesen…", L"Titel"
    }, {
        L"Brani", L"Album", L"Artisti", L"Compositori", L"Sconosciuto", L"In riproduzione",
        L"Aggiungi i file audio a UnlimitedMusic e seleziona Aggiorna.",
        L"Il decoder Discx è mancante. Reinstalla Unlimited.",
        L"Impossibile riprodurre il brano selezionato.", L"Invio per sfogliare  •  Esc per tornare",
        L"Invio per ascoltare  •  Spazio per la pausa  •  Esc per tornare",
        L"Riproduci", L"Pausa", L"Precedente", L"Successivo", L"Stop", L"Casuale", L"Ripeti",
        L"No", L"Tutti", L"Uno", L"Aggiorna", L"Indietro", L"Volume", L"Lettura della libreria…", L"brani"
    }, {
        L"Canciones", L"Álbumes", L"Artistas", L"Compositores", L"Desconocido", L"En reproducción",
        L"Añade canciones a UnlimitedMusic y selecciona Actualizar.",
        L"Falta el decodificador de Discx. Reinstala Unlimited para recuperarlo.",
        L"No se pudo reproducir la pista seleccionada.", L"Intro para explorar  •  Esc para volver",
        L"Intro para reproducir  •  Espacio para pausar  •  Esc para volver",
        L"Reproducir", L"Pausar", L"Anterior", L"Siguiente", L"Detener", L"Aleatorio", L"Repetir",
        L"No", L"Todo", L"Una", L"Actualizar", L"Atrás", L"Volumen", L"Leyendo biblioteca…", L"pistas"
    }, {
        L"Nummers", L"Albums", L"Artiesten", L"Componisten", L"Onbekend", L"Wordt afgespeeld",
        L"Zet audiobestanden in UnlimitedMusic en kies Vernieuwen.",
        L"De Discx-decoder ontbreekt. Installeer Unlimited opnieuw.",
        L"Het geselecteerde nummer kan niet worden afgespeeld.", L"Enter: Openen  •  Esc: Terug",
        L"Enter: Afspelen  •  Spatie: Pauze  •  Esc: Terug",
        L"Afspelen", L"Pauze", L"Vorige", L"Volgende", L"Stop", L"Willekeurig", L"Herhalen",
        L"Uit", L"Alles", L"Eén", L"Vernieuwen", L"Terug", L"Volume", L"Bibliotheek wordt ingelezen…", L"nummers"
    }, {
        L"Låtar", L"Album", L"Artister", L"Kompositörer", L"Okänd", L"Spelas nu",
        L"Lägg ljudfiler i UnlimitedMusic och välj Uppdatera.",
        L"Discx-avkodaren saknas. Installera om Unlimited.",
        L"Det gick inte att spela den valda låten.", L"Enter: Bläddra  •  Esc: Tillbaka",
        L"Enter: Spela  •  Mellanslag: Paus  •  Esc: Tillbaka",
        L"Spela", L"Paus", L"Föregående", L"Nästa", L"Stopp", L"Blanda", L"Repetera",
        L"Av", L"Alla", L"En", L"Uppdatera", L"Tillbaka", L"Volym", L"Läser in biblioteket…", L"låtar"
    }, {
        L"Numre", L"Album", L"Kunstnere", L"Komponister", L"Ukendt", L"Afspilles nu",
        L"Læg lydfiler i UnlimitedMusic, og vælg Opdater.",
        L"Discx-dekoderen mangler. Geninstaller Unlimited.",
        L"Det valgte nummer kunne ikke afspilles.", L"Enter: Gennemse  •  Esc: Tilbage",
        L"Enter: Afspil  •  Mellemrum: Pause  •  Esc: Tilbage",
        L"Afspil", L"Pause", L"Forrige", L"Næste", L"Stop", L"Bland", L"Gentag",
        L"Fra", L"Alle", L"Én", L"Opdater", L"Tilbage", L"Lydstyrke", L"Indlæser bibliotek…", L"numre"
    }, {
        L"곡", L"앨범", L"아티스트", L"작곡가", L"알 수 없음", L"재생 중",
        L"UnlimitedMusic에 오디오 파일을 넣고 새로 고침을 선택하세요.",
        L"Discx 디코더가 없습니다. Unlimited를 다시 설치하세요.",
        L"선택한 곡을 재생할 수 없습니다.", L"Enter: 탐색  •  Esc: 뒤로",
        L"Enter: 재생  •  Space: 일시 정지  •  Esc: 뒤로",
        L"재생", L"일시 정지", L"이전", L"다음", L"정지", L"셔플", L"반복",
        L"끄기", L"전체", L"한 곡", L"새로 고침", L"뒤로", L"음량", L"라이브러리 검색 중…", L"곡"
    }, {
        L"歌曲", L"專輯", L"演出者", L"作曲者", L"未知", L"正在播放",
        L"將音訊檔案放入 UnlimitedMusic，然後選擇重新整理。",
        L"缺少 Discx 解碼器。請重新安裝 Unlimited。",
        L"無法播放選取的歌曲。", L"Enter：瀏覽  •  Esc：返回",
        L"Enter：播放  •  空白鍵：暫停  •  Esc：返回",
        L"播放", L"暫停", L"上一首", L"下一首", L"停止", L"隨機", L"重複",
        L"關閉", L"全部", L"單曲", L"重新整理", L"返回", L"音量", L"正在掃描音樂庫…", L"首歌曲"
    }, {
        L"Utwory", L"Albumy", L"Wykonawcy", L"Kompozytorzy", L"Nieznany", L"Teraz odtwarzane",
        L"Dodaj pliki dźwiękowe do UnlimitedMusic i wybierz Odśwież.",
        L"Brakuje dekodera Discx. Zainstaluj Unlimited ponownie.",
        L"Nie można odtworzyć wybranego utworu.", L"Enter: Przeglądaj  •  Esc: Wróć",
        L"Enter: Odtwórz  •  Spacja: Wstrzymaj  •  Esc: Wróć",
        L"Odtwórz", L"Wstrzymaj", L"Poprzedni", L"Następny", L"Zatrzymaj", L"Losowo", L"Powtarzaj",
        L"Nie", L"Wszystkie", L"Jeden", L"Odśwież", L"Wróć", L"Głośność", L"Skanowanie biblioteki…", L"utw."
    }, {
        L"Kappaleet", L"Albumit", L"Esittäjät", L"Säveltäjät", L"Tuntematon", L"Nyt soi",
        L"Lisää äänitiedostot UnlimitedMusic-kansioon ja valitse Päivitä.",
        L"Discx-dekooderi puuttuu. Asenna Unlimited uudelleen.",
        L"Valittua kappaletta ei voitu toistaa.", L"Enter: Selaa  •  Esc: Takaisin",
        L"Enter: Toista  •  Välilyönti: Tauko  •  Esc: Takaisin",
        L"Toista", L"Tauko", L"Edellinen", L"Seuraava", L"Pysäytä", L"Satunnainen", L"Toisto",
        L"Pois", L"Kaikki", L"Yksi", L"Päivitä", L"Takaisin", L"Äänenvoimakkuus", L"Luetaan musiikkikirjastoa…", L"kpl"
    }};
    static_assert(std::size(labels) == std::size(kControlText));
    return labels[ControlLanguage()];
}

static const wchar_t* DiscGroupTag(const unlimited_discx::Track& track) {
    switch (g_discxFacet) {
    case DiscFacet::Albums: return track.metadata.album;
    case DiscFacet::Artists: return track.metadata.artist;
    case DiscFacet::Composers: return track.metadata.composer;
    default: return L"";
    }
}

static int CompareDiscText(const wchar_t* a, const wchar_t* b) {
    return CompareStringOrdinal(a, -1, b, -1, TRUE) - CSTR_EQUAL;
}

static bool DiscShowsGroups() { return g_discxFacet != DiscFacet::Songs && !g_discxInGroup; }
static size_t DiscRows() { return DiscShowsGroups() ? g_discxGroups.size() : g_discxTracks.size(); }
static constexpr unsigned discRowsOnScreen = 10;

static void RebuildDiscRows() {
    g_discxTracks.clear();
    g_discxGroups.clear();
    g_discxRow = g_discxTop = 0;
    ++g_discxRevision;
    if (!g_discxCatalog.tracks) return;
    const auto& tracks = *g_discxCatalog.tracks;
    if (DiscShowsGroups()) {
        for (const auto& track : tracks) g_discxGroups.emplace_back(DiscGroupTag(track));
        std::sort(g_discxGroups.begin(), g_discxGroups.end(),
            [](const auto& a, const auto& b) { return CompareDiscText(a.c_str(), b.c_str()) < 0; });
        const auto end = std::unique(g_discxGroups.begin(), g_discxGroups.end(),
            [](const auto& a, const auto& b) { return CompareDiscText(a.c_str(), b.c_str()) == 0; });
        g_discxGroups.erase(end, g_discxGroups.end());
        return;
    }
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (!g_discxInGroup || CompareDiscText(g_discxGroup.c_str(), DiscGroupTag(tracks[i])) == 0)
            g_discxTracks.push_back(i);
    }
    std::stable_sort(g_discxTracks.begin(), g_discxTracks.end(), [&](size_t a, size_t b) {
        const auto& left = tracks[a].metadata;
        const auto& right = tracks[b].metadata;
        int order = CompareDiscText(left.title, right.title);
        if (!order) order = CompareDiscText(left.artist, right.artist);
        return order < 0;
    });
}

static void DiscSetFacet(DiscFacet facet) {
    g_discxFacet = facet;
    g_discxInGroup = false;
    RebuildDiscRows();
}

static void DiscSetRow(ptrdiff_t row) {
    const auto length = DiscRows();
    if (!length) return;
    const auto index = static_cast<size_t>(std::clamp<ptrdiff_t>(row, 0, length - 1));
    if (index == g_discxRow) return;
    g_discxRow = index;
    if (index < g_discxTop) g_discxTop = index;
    if (index >= g_discxTop + discRowsOnScreen) g_discxTop = index + 1 - discRowsOnScreen;
    ++g_discxRevision;
}

static void DiscChooseRow() {
    if (g_discxRow >= DiscRows()) return;
    if (DiscShowsGroups()) {
        g_discxGroup = g_discxGroups[g_discxRow];
        g_discxInGroup = true;
        RebuildDiscRows();
    } else {
        unlimited_discx::Play(g_discxCatalog.tracks, g_discxTracks, g_discxTracks[g_discxRow]);
    }
}

static float NativeDiscGain(void* sound) {
    if (!sound) return 1.f;
    auto* data = At<uint8_t*>(static_cast<uint8_t*>(sound), 0x24);
    if (!data) return 1.f;
    const auto mode = At<int>(static_cast<uint8_t*>(sound), 0x70);
    const auto gain = At<float>(data, 0) * At<float>(data, mode == 1 ? 8 : 12);
    return std::isfinite(gain) ? gain : 1.f;
}

// Pathfinder is shared by licensed music and the interactive score. Keep its
// scene decisions, volume graph and events, and replace only the licensed output.
static thread_local bool g_muteDiscCarrier = false;

static int __cdecl Hook_DiscPathVolume(int track, int volume) {
    return ((int(__cdecl*)(int, int))0x82d560)(track, g_muteDiscCarrier ? 0 : volume);
}

static bool DiscLicensedBus(uint8_t* path) {
    const unsigned phase = At<unsigned>(path, 0x150);
    return At<int>(0x8f86fc) && phase <= 1 && (At<unsigned>(path, 0x114) & 0xc04) == 4 &&
           At<int>(path, 0x13c) == 0 && At<int>(path, 0x148) == 0 && At<int>(path, 0x60) != 3;
}

static void FASTCALL Hook_DiscProcessPathfinder(uint8_t* path, void*) {
    const auto process = (void(THISCALL*)(void*))0x4f6ea0;
    if (!unlimited_discx::Selected()) {
        process(path);
        return;
    }
    const bool previous = g_muteDiscCarrier;
    g_muteDiscCarrier = At<int>(path, 0x13c) == 0 && At<int>(path, 0x148) == 0;
    process(path);
    g_muteDiscCarrier = previous;

    // Read the logical bus on every update. PATH_volume only runs while the
    // silent stock track has a live voice, so it cannot clock Discx playback.
    const auto* movie = At<void*>(0x91cb10);
    const bool playingMovie = movie && ((bool(THISCALL*)(const void*))0x4df9d0)(movie);
    const auto* outputs = At<const unsigned*>(path, 0x10);
    if (!DiscLicensedBus(path) || !outputs || playingMovie ||
        NativeDiscGain(At<void*>(0x911fa8)) <= 0.f) {
        unlimited_discx::SetGameMix(0, 0);
        return;
    }
    const unsigned volume = (outputs[0] >> (At<unsigned>(path, 0x150) * 16)) & 0x7fff;
    const unsigned cutoff = (outputs[4] >> 16) & 0x7fff;
    unlimited_discx::SetGameMix((volume * 100 + 0x3fff) / 0x7fff, cutoff);
}

struct TraxPlaylist {
    unsigned mask, remaining, count;
    int last, current, order;
};

static void FASTCALL Hook_DiscUpdatePathfinder(uint8_t* path, void*, float elapsed) {
    const auto update = (void(THISCALL*)(void*, float))0x4e79a0;
    auto* sound = At<uint8_t*>(0x911fa8);
    auto* settings = sound ? At<uint8_t*>(sound, 0x24) : nullptr;
    if (!settings || !unlimited_discx::Selected()) {
        update(path, elapsed);
        return;
    }
    auto& mode = At<int>(settings, 0x2c);
    const auto originalMode = mode;
    mode = unlimited_discx::Enabled() ? 1 : 0;
    // A profile with every stock song disabled still has a licensed mix bus.
    // Supply a silent carrier for that audio context without changing its list.
    auto* playlists = reinterpret_cast<TraxPlaylist*>(0x8f2080);
    std::array<TraxPlaylist, 2> originals{};
    unsigned temporary = 0;
    for (unsigned i = 0; mode && i < originals.size(); ++i) {
        if (playlists[i].mask) continue;
        originals[i] = playlists[i];
        playlists[i].mask = playlists[i].remaining = playlists[i].count = 1;
        temporary |= 1u << i;
    }
    update(path, elapsed);
    // DMIX normally keys its inputs to the native stream's packet status.
    // Keep the licensed input active while our own stream supplies the music.
    if (mode && DiscLicensedBus(path)) {
        if (auto* inputs = At<unsigned*>(path, 0xc)) {
            inputs[6] = 0x7fff;
            inputs[7] = inputs[8] = 0;
        }
    }
    for (unsigned i = 0; i < originals.size(); ++i)
        if (temporary & (1u << i)) playlists[i] = originals[i];
    mode = originalMode;
}

static void FASTCALL Hook_DiscPathEvent(uint8_t* path, void*, const uint8_t* event) {
    const auto handle = (void(THISCALL*)(void*, const void*))0x4e7bc0;
    if (!unlimited_discx::Selected() || !event || *reinterpret_cast<const int*>(event + 0x14) != -1) {
        handle(path, event);
        return;
    }
    if (unlimited_discx::PlayingInGameMix()) unlimited_discx::Next();
}

static bool InstallDiscMusicRouting() {
    constexpr uintptr_t process = 0x899ba8, update = 0x899ba4;
    constexpr uintptr_t event = 0x4f88d7; // Pathfinder5 message handler pointer.
    if (At<uintptr_t>(process) != 0x4f6ea0 || At<uintptr_t>(update) != 0x4e79a0 ||
        At<uintptr_t>(event) != 0x4e7bc0 || !PatchCall(0x4f7040, (void*)Hook_DiscPathVolume, 0x82d560))
        return false;
    void* proc = (void*)Hook_DiscProcessPathfinder;
    void* tick = (void*)Hook_DiscUpdatePathfinder;
    void* skip = (void*)Hook_DiscPathEvent;
    WriteMem(process, &proc, sizeof(proc));
    WriteMem(update, &tick, sizeof(tick));
    WriteMem(event, &skip, sizeof(skip));
    return true;
}

static void ApplyDiscVolume() {
    const auto gain = std::clamp(NativeDiscGain(At<void*>(0x911fa8)), 0.f, 1.f);
    unlimited_discx::SetPreviewGain(g_discxOpen ? gain : -1.f);
    const auto percent = static_cast<int>(gain * 100.f + .5f);
    if (g_discxVolume != percent) {
        g_discxVolume = percent;
        ++g_discxRevision;
    }
}

static constexpr size_t carouselReactsToInput = 0x57;
static constexpr size_t carouselMousePressed = 0x128;
static constexpr size_t carouselPreviousMessage = 0x138;
static constexpr size_t packageInputEnabled = 0x28;

static void UpdateDiscWindowAudio() {
    unlimited_discx::SetOutputActive(g_discxWindowActive && !g_discxMinimized && !g_discxClosing);
}

extern "C" bool UnlimitedScrollVideoOptions(int delta);

static LRESULT CALLBACK DiscWindowEvents(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                         UINT_PTR id, DWORD_PTR) {
    switch (message) {
    case WM_ACTIVATEAPP:
        g_discxWindowActive = wparam != 0;
        if (!g_discxWindowActive) g_discxMouseDown = false;
        UpdateDiscWindowAudio();
        break;
    case WM_SIZE:
        g_discxMinimized = wparam == SIZE_MINIMIZED;
        if (g_discxMinimized) g_discxMouseDown = false;
        UpdateDiscWindowAudio();
        break;
    case WM_CLOSE:
        // The game may show a confirmation or otherwise keep the window alive.
        // Silence the ring while its close handler runs and stop when it is destroyed.
        if (!g_discxClosing) {
            g_discxClosing = true;
            UpdateDiscWindowAudio();
            const auto result = DefSubclassProc(window, message, wparam, lparam);
            if (g_discxWindow == window) {
                g_discxClosing = false;
                UpdateDiscWindowAudio();
            }
            return result;
        }
        break;
    case WM_DESTROY:
    case WM_NCDESTROY:
        if (g_discxWindow == window) {
            g_discxClosing = true;
            UpdateDiscWindowAudio();
            if (message == WM_DESTROY) unlimited_discx::Stop();
            else g_discxWindow = nullptr;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, DiscWindowEvents, id);
        break;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        g_discxMouseDown = false;
        break;
    case WM_MOUSEWHEEL:
        if (g_discxOpen) {
            g_discxWheel += GET_WHEEL_DELTA_WPARAM(wparam);
            return 0;
        }
        if (UnlimitedScrollVideoOptions(GET_WHEEL_DELTA_WPARAM(wparam))) return 0;
        break;
    case WM_LBUTTONDOWN:
        if (g_discxOpen) g_discxMouseDown = true;
        break;
    case WM_LBUTTONUP:
        if (g_discxOpen && g_discxMouseDown) {
            g_discxClick = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
            g_discxClicked = true;
        }
        g_discxMouseDown = false;
        break;
    case WM_KEYDOWN:
        if (g_discxOpen && wparam == VK_ESCAPE) {
            if ((lparam & (1L << 30)) == 0) g_discxInput.back = true;
            return 0;
        }
        break;
    }
    // Deliver both mouse edges to the native window procedure. Navigation is
    // intercepted at the Options notification handler while the library owns it.
    return DefSubclassProc(window, message, wparam, lparam);
}

static void SetDiscGameWindow(HWND window) {
    if (window == g_discxWindow) return;
    if (g_discxWindow) RemoveWindowSubclass(g_discxWindow, DiscWindowEvents, 1);
    g_discxWindow = window && SetWindowSubclass(window, DiscWindowEvents, 1, 0) ? window : nullptr;
    g_discxWindowActive = g_discxWindow && GameHasFocus();
    g_discxMinimized = g_discxWindow && IsIconic(g_discxWindow);
    g_discxClosing = false;
    UpdateDiscWindowAudio();
}

static bool OpenDiscScreen(void* owner) {
    if (*G::pGameFlowState != 3 || !Dev() || g_discxOpen || !g_discxWindow) return false;
    g_discxOpen = true;
    ApplyDiscVolume();
    g_discxOwner = owner;
    At<uint8_t>(static_cast<uint8_t*>(owner), carouselReactsToInput) = false;
    g_discxInput = {};
    g_discxMouseDown = g_discxClicked = g_discxBackPending = false;
    g_discxWheel = 0;
    ++g_discxRevision;
    PollPads();
    SyncInputState();
    const auto library = unlimited_discx::Library();
    if (!library.tracks) unlimited_discx::StartScan();
    return true;
}

static void CloseDiscScreen(bool restoreCarousel) {
    auto* owner = static_cast<uint8_t*>(g_discxOwner);
    if (restoreCarousel && owner) {
        // The library bypasses the stock Options exit animation. Discard any
        // deferred activation and restore the carousel's input state directly.
        for (size_t field = carouselPreviousMessage; field <= carouselPreviousMessage + 12; field += 4)
            At<uint32_t>(owner, field) = 0;
        At<uint8_t>(owner, carouselMousePressed) = false;
        At<uint8_t>(owner, carouselReactsToInput) = true;

        // The FNG button runs SetInputProcessing(false) before its notification
        // reaches the carousel. Our library owns the matching input handoff.
        auto* frontend = At<void*>(0x91cadc);
        const auto* name = At<const char*>(owner, 0x10);
        using FindPackage = uint8_t*(THISCALL*)(void*, const char*);
        if (auto* package = frontend && name ? ((FindPackage)0x516af0)(frontend, name) : nullptr)
            At<uint8_t>(package, packageInputEnabled) = true;
    }
    g_discxOpen = false;
    unlimited_discx::SetPreviewGain(-1.f);
    g_discxOwner = nullptr;
    g_discxInput = {};
    g_discxMouseDown = g_discxClicked = g_discxBackPending = false;
    g_discxWheel = 0;
    if (g_discxTextLayer) { g_discxTextLayer->Release(); g_discxTextLayer = nullptr; }
}

static bool RouteDiscMenuMessage(void* owner, uint32_t msg) {
    if (!g_discxOpen || owner != g_discxOwner) return false;
    switch (msg) {
    case 0x911ab364: g_discxInput.back = true; return true;
    case 0x406415e3: // Accept
    case 0xb5af2461: // Start
        g_discxInput.accept = true;
        return true;
    case 0x72619778: g_discxInput.up = true; return true;
    case 0x911c0a4b: g_discxInput.down = true; return true;
    case 0x9120409e: g_discxInput.left = true; return true;
    case 0xb5971bf1: g_discxInput.right = true; return true;
    case G::BUTTON_PRESSED:
    case 0xf459b307: // Mouse press and release belong to the library window.
    case 0x7eabca56:
    case 0x936a6a7f: // Icon release generated from the native mouse update.
    case 0xc12e9e27:
    case 0xc2f8fcc8:
    case 0xebfcda65:
        return true;
    default: return false;
    }
}

struct DiscBox {
    float x, y, w, h;
    bool Hits(float mx, float my) const { return mx >= x && mx < x + w && my >= y && my < y + h; }
};
struct DiscLayout {
    float scale;
    DiscBox panel, seek, volume;
    DiscBox Tab(int i) const { return Box(36 + 354.f * i, 134, 340, 48); }
    DiscBox Row(int i) const { return Box(36, 241 + 40.f * i, 1428, 40); }
    DiscBox Button(int i) const { return Box(36 + i * 155.f, 763, 145, 48); }
    DiscBox Box(float x, float y, float w, float h) const {
        return {panel.x + x * scale, panel.y + y * scale, w * scale, h * scale};
    }
};

static DiscLayout DiscScreenLayout(float width, float height) {
    const float scale = std::min(width / 1660.f, height / 990.f);
    DiscLayout layout{scale, {(width - 1500 * scale) * .5f, (height - 900 * scale) * .5f, 1500 * scale, 900 * scale}, {}, {}};
    layout.seek = layout.Box(36, 728, 1428, 20);
    layout.volume = layout.Box(1160, 775, 304, 28);
    return layout;
}

static bool DiscMouse(float width, float height, float& x, float& y) {
    RECT client{};
    POINT point{};
    if (!g_discxWindow || !GameHasFocus() || !GetClientRect(g_discxWindow, &client) ||
        !GetCursorPos(&point) || !ScreenToClient(g_discxWindow, &point) || !client.right || !client.bottom) return false;
    x = width * point.x / client.right;
    y = height * point.y / client.bottom;
    return x >= 0 && x < width && y >= 0 && y < height;
}

static void DiscAction(int action, const unlimited_discx::Playback& now) {
    switch (action) {
    case 0: unlimited_discx::Previous(); break;
    case 1:
        if (now.playing) unlimited_discx::Pause();
        else DiscChooseRow();
        break;
    case 2: unlimited_discx::Next(); break;
    case 3: unlimited_discx::Stop(); break;
    case 4:
        g_discxShuffle = !g_discxShuffle;
        unlimited_discx::SetShuffle(g_discxShuffle);
        ++g_discxRevision;
        break;
    case 5:
        g_discxRepeat = static_cast<unlimited_discx::Repeat>((static_cast<int>(g_discxRepeat) + 1) % 3);
        unlimited_discx::SetRepeat(g_discxRepeat);
        ++g_discxRevision;
        break;
    case 6: unlimited_discx::StartScan(); break;
    }
}

static void UpdateDiscScreen(const DiscLayout& layout, const unlimited_discx::Playback& now) {
    const auto catalog = unlimited_discx::Library();
    if (catalog.tracks != g_discxCatalog.tracks || catalog.scanning != g_discxCatalog.scanning ||
        catalog.errors != g_discxCatalog.errors || catalog.codecAvailable != g_discxCatalog.codecAvailable) {
        const bool updated = catalog.tracks != g_discxCatalog.tracks;
        g_discxCatalog = catalog;
        if (updated) RebuildDiscRows();
        else ++g_discxRevision;
    }
    PollPads();
    auto input = g_discxInput;
    g_discxInput = {};
    const auto direct = ReadMenuInput();
    input.up |= direct.up; input.down |= direct.down;
    input.left |= direct.left; input.right |= direct.right;
    input.accept |= direct.accept; input.back |= direct.back;
    input.clear = direct.clear;
    if (input.back || g_discxBackPending) {
        const bool backHeld = KeyDown(VK_ESCAPE) || std::any_of(std::begin(g_padNow), std::end(g_padNow),
            [](const XINPUT_STATE& pad) { return (pad.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0; });
        // Let the native event queue drain before handing it the next Back press.
        if (g_discxBackPending && !input.back && !backHeld) CloseDiscScreen(true);
        else g_discxBackPending = true;
        return;
    }
    if (input.left || input.right)
        DiscSetFacet(static_cast<DiscFacet>((static_cast<int>(g_discxFacet) + (input.right ? 1 : 3)) % 4));
    const int wheelSteps = g_discxWheel / WHEEL_DELTA;
    g_discxWheel %= WHEEL_DELTA;
    const int delta = 1 * input.down - 1 * input.up - 3 * wheelSteps -
        int(discRowsOnScreen) * KeyPressed(VK_PRIOR) + int(discRowsOnScreen) * KeyPressed(VK_NEXT);
    if (delta) DiscSetRow(static_cast<ptrdiff_t>(g_discxRow) + delta);
    if (input.accept) DiscChooseRow();
    if (KeyPressed(VK_SPACE)) DiscAction(1, now);
    if (input.clear) DiscAction(3, now);
    if (KeyPressed('S')) DiscAction(4, now);
    if (KeyPressed('R')) DiscAction(5, now);
    if (KeyPressed(VK_F5)) DiscAction(6, now);

    if (!g_discxClicked) return;
    g_discxClicked = false;
    RECT client{};
    if (!GetClientRect(g_discxWindow, &client) || !client.right || !client.bottom) return;
    const float x = g_uiW * g_discxClick.x / client.right;
    const float y = g_uiH * g_discxClick.y / client.bottom;
    if (layout.Box(1330, 38, 134, 46).Hits(x, y)) {
        CloseDiscScreen(true);
        return;
    }
    for (int i = 0; i < 4; ++i) if (layout.Tab(i).Hits(x, y)) { DiscSetFacet(static_cast<DiscFacet>(i)); return; }
    for (unsigned i = 0; i < discRowsOnScreen; ++i) {
        if (layout.Row(i).Hits(x, y) && g_discxTop + i < DiscRows()) {
            DiscSetRow(g_discxTop + i);
            DiscChooseRow();
            return;
        }
    }
    if (layout.seek.Hits(x, y) && now.playing && now.duration > 0)
        unlimited_discx::Seek(now.duration * (x - layout.seek.x) / layout.seek.w);
    for (int i = 0; i < 7; ++i) if (layout.Button(i).Hits(x, y)) { DiscAction(i, now); return; }
}

struct DiscTextSurface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    HFONT normal = nullptr, bold = nullptr, title = nullptr;
    uint32_t* bits = nullptr;
    int width = 0, height = 0;
    float factor = 1.f;

    DiscTextSurface(int w, int h) : width(w), height(h), factor(w / 1500.f) {
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* buffer = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &buffer, nullptr, 0);
        if (!bitmap || !buffer) return;
        bits = static_cast<uint32_t*>(buffer);
        previous = SelectObject(dc, bitmap);
        memset(bits, 0, size_t(width) * height * 4);
        SetBkMode(dc, TRANSPARENT);
        normal = MakeFont(20, FW_NORMAL);
        bold = MakeFont(22, FW_BOLD);
        title = MakeFont(49, FW_BOLD);
    }

    ~DiscTextSurface() {
        if (previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (title) DeleteObject(title);
        if (bold) DeleteObject(bold);
        if (normal) DeleteObject(normal);
        if (dc) DeleteDC(dc);
    }

    HFONT MakeFont(int size, int weight) const {
        const auto language = ControlLanguage();
        const auto* face = language == 8 ? L"Malgun Gothic" :
                           language == 9 ? L"Microsoft JhengHei" : L"Segoe UI";
        return CreateFontW(-std::max(7, static_cast<int>(size * factor)), 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    }

    void Label(int x, int y, int w, int h, const wchar_t* text, DWORD color,
               HFONT font = nullptr, UINT align = DT_LEFT) const {
        if (!text || !*text || !bits) return;
        RECT rect{LONG(x * factor), LONG(y * factor), LONG((x + w) * factor), LONG((y + h) * factor)};
        SetTextColor(dc, RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
        const auto old = SelectObject(dc, font ? font : normal);
        DrawTextW(dc, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | align);
        SelectObject(dc, old);
    }

    bool Upload() {
        if (!bits) return false;
        GdiFlush();
        if (g_discxTextLayer) {
            D3DSURFACE_DESC desc{};
            if (FAILED(g_discxTextLayer->GetLevelDesc(0, &desc)) || desc.Width != unsigned(width) || desc.Height != unsigned(height)) {
                g_discxTextLayer->Release();
                g_discxTextLayer = nullptr;
            }
        }
        if (!g_discxTextLayer && FAILED(Dev()->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                                          D3DPOOL_DEFAULT, &g_discxTextLayer, nullptr))) return false;
        D3DLOCKED_RECT surface{};
        if (FAILED(g_discxTextLayer->LockRect(0, &surface, nullptr, D3DLOCK_DISCARD))) return false;
        for (int y = 0; y < height; ++y) {
            const auto* source = bits + y * width;
            auto* dest = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface.pBits) + y * surface.Pitch);
            for (int x = 0; x < width; ++x) {
                // Preserve the submitted RGB and coverage on a transparent DIB.
                const auto rgb = source[x] & 0xffffff;
                const auto alpha = std::max({rgb & 255, (rgb >> 8) & 255, (rgb >> 16) & 255});
                dest[x] = (alpha << 24) | rgb;
            }
        }
        return SUCCEEDED(g_discxTextLayer->UnlockRect(0));
    }
};

static const wchar_t* DiscKnown(const wchar_t* value) {
    return value && *value ? value : DiscStrings().unknown;
}

static std::wstring DiscTime(double total) {
    const unsigned seconds = static_cast<unsigned>(std::clamp(total, 0., 864000.));
    wchar_t value[24]{};
    swprintf(value, std::size(value), L"%u:%02u", seconds / 60, seconds % 60);
    return value;
}

static void DrawDiscScreen(const DiscLayout& layout, const unlimited_discx::Playback& now) {
    const auto& words = DiscStrings();
    const auto& panel = layout.panel;
    const auto rect = [](const DiscBox& box, DWORD color) { UIRect(box.x, box.y, box.w, box.h, color); };
    UIRect(0, 0, g_uiW, g_uiH, 0x9d070708);
    rect(panel, 0xf5151412);
    rect(layout.Box(0, 0, 1500, 4), 0xffc28c34);
    rect(layout.Box(36, 121, 1428, 1), 0xff55442d);
    rect(layout.Box(36, 189, 1428, 51), 0xff262219);
    rect(layout.Box(36, 659, 1428, 64), 0xff242015);
    for (int i = 0; i < 4; ++i)
        rect(layout.Tab(i), static_cast<int>(g_discxFacet) == i ? 0xff6e4a13 : 0xff2a261e);
    for (unsigned i = 0; i < discRowsOnScreen && i + g_discxTop < DiscRows(); ++i) {
        const bool chosen = i + g_discxTop == g_discxRow;
        rect(layout.Row(i), chosen ? 0xff544223 : (i % 2 ? 0xff22211e : 0xff191815));
        if (chosen) rect(layout.Box(36, 241 + 40.f * i, 4, 40), 0xffe9a937);
    }
    if (DiscRows() > discRowsOnScreen) {
        const auto height = 400.f * discRowsOnScreen / DiscRows();
        const auto top = 241.f + (400.f - height) * g_discxTop / (DiscRows() - discRowsOnScreen);
        rect(layout.Box(1456, top, 8, height), 0xffc28c34);
    }
    rect(layout.seek, 0xff484031);
    if (now.playing && now.duration > 0)
        rect({layout.seek.x, layout.seek.y, layout.seek.w * static_cast<float>(std::clamp(now.position / now.duration, 0., 1.)), layout.seek.h}, 0xffe9a937);
    rect(layout.volume, 0xff484031);
    rect({layout.volume.x, layout.volume.y, layout.volume.w * g_discxVolume / 100, layout.volume.h}, 0xff996a23);
    for (int i = 0; i < 7; ++i)
        rect(layout.Button(i), i == 4 && g_discxShuffle ? 0xff6e4a13 : 0xff28231a);
    rect(layout.Box(1330, 38, 134, 46), 0xff28231a);

    static unsigned revision = ~0u;
    static std::filesystem::path lastTrack;
    static int second = -1, lastWidth = 0;
    static bool wasPaused = false, wasFailed = false, wasPlaying = false;
    const int width = std::clamp(static_cast<int>(panel.w), 640, 2048);
    const int seconds = static_cast<int>(std::clamp(now.position, 0., 864000.));
    if (!g_discxTextLayer || lastTrack != now.path || revision != g_discxRevision || lastWidth != width ||
        second != seconds || wasPaused != now.paused || wasFailed != now.failed || wasPlaying != now.playing) {
        DiscTextSurface surface(width, width * 3 / 5);
        if (surface.bits) {
            constexpr DWORD light = 0xfff1ede4, dim = 0xffc7b89f, accent = 0xffffc358;
            surface.Label(36, 26, 1000, 69, L"DISCX", accent, surface.title);
            surface.Label(1330, 38, 134, 46, words.back, light, surface.bold, DT_CENTER);
            std::wstring section = L"UnlimitedMusic";
            if (g_discxInGroup) section += std::wstring(L" / ") + DiscKnown(g_discxGroup.c_str());
            surface.Label(38, 94, 1090, 24, section.c_str(), dim);
            const wchar_t* tabs[] = {words.songs, words.albums, words.artists, words.composers};
            for (int i = 0; i < 4; ++i) surface.Label(36 + 354 * i, 134, 340, 48, tabs[i], light, surface.bold, DT_CENTER);
            auto count = std::to_wstring(DiscRows()) + L" " + words.tracks;
            if (g_discxCatalog.errors) count += L"  /  " + std::to_wstring(g_discxCatalog.errors) + L" !";
            surface.Label(1222, 94, 241, 24, count.c_str(), dim, nullptr, DT_RIGHT);
            if (DiscShowsGroups()) {
                surface.Label(52, 189, 1200, 51, tabs[static_cast<int>(g_discxFacet)], light, surface.bold);
            } else {
                surface.Label(52, 189, 519, 51, words.songs, light, surface.bold);
                surface.Label(580, 189, 295, 51, words.artists, light, surface.bold);
                surface.Label(892, 189, 285, 51, words.albums, light, surface.bold);
                surface.Label(1194, 189, 250, 51, words.composers, light, surface.bold);
            }
            for (unsigned i = 0; i < discRowsOnScreen && i + g_discxTop < DiscRows(); ++i) {
                const auto row = g_discxTop + i;
                const auto y = 241 + 40 * i;
                const auto color = row == g_discxRow ? accent : light;
                if (DiscShowsGroups()) surface.Label(52, y, 1390, 40, DiscKnown(g_discxGroups[row].c_str()), color);
                else {
                    const auto& track = (*g_discxCatalog.tracks)[g_discxTracks[row]];
                    surface.Label(52, y, 510, 40, track.metadata.title, track.path == now.path ? accent : color);
                    surface.Label(580, y, 295, 40, DiscKnown(track.metadata.artist), dim);
                    surface.Label(892, y, 285, 40, DiscKnown(track.metadata.album), dim);
                    surface.Label(1194, y, 250, 40, DiscKnown(track.metadata.composer), dim);
                }
            }
            if (g_discxCatalog.scanning) surface.Label(55, 243, 1380, 50, words.scanning, accent);
            else if (!g_discxCatalog.codecAvailable) surface.Label(55, 243, 1380, 50, words.decoderMissing, accent);
            else if (!DiscRows()) surface.Label(55, 243, 1380, 50, words.empty, dim);

            surface.Label(52, 663, 230, 26, words.status, dim);
            const auto track = now.artist.empty() ? now.title : now.title + L" — " + now.artist;
            surface.Label(52, 688, 1120, 33, now.failed ? words.failed : track.c_str(), accent, surface.bold);
            const auto time = DiscTime(now.position) + L" / " + DiscTime(now.duration);
            surface.Label(1170, 688, 277, 33, time.c_str(), light, nullptr, DT_RIGHT);
            const std::wstring repeat = std::wstring(words.repeat) + L" " +
                (g_discxRepeat == unlimited_discx::Repeat::All ? words.all :
                 g_discxRepeat == unlimited_discx::Repeat::One ? words.one : words.off);
            const wchar_t* buttons[] = {words.previous, now.playing && !now.paused ? words.pause : words.play,
                words.next, words.stop, words.shuffle, repeat.c_str(), words.refresh};
            for (int i = 0; i < 7; ++i)
                surface.Label(36 + i * 155, 763, 145, 48, buttons[i], light, nullptr, DT_CENTER);
            const auto volume = std::wstring(words.volume) + L" " + std::to_wstring(g_discxVolume) + L"%";
            surface.Label(1160, 775, 304, 28, volume.c_str(), light, nullptr, DT_CENTER);
            surface.Label(36, 835, 1430, 40, DiscShowsGroups() ? words.groupHelp : words.songHelp, dim);
            if (surface.Upload()) {
                revision = g_discxRevision;
                lastTrack = now.path;
                lastWidth = width;
                second = seconds;
                wasPaused = now.paused;
                wasPlaying = now.playing;
                wasFailed = now.failed;
            }
        }
    }

    if (g_discxTextLayer) {
        Dev()->SetTexture(0, g_discxTextLayer);
        Dev()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        const float x = panel.x - .5f, y = panel.y - .5f, right = x + panel.w, bottom = y + panel.h;
        constexpr DWORD color = 0xffffffff;
        const UIV vertices[] = {{x,y,0,1,color,0,0}, {right,y,0,1,color,1,0},
            {x,bottom,0,1,color,0,1}, {right,bottom,0,1,color,1,1}};
        Dev()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(UIV));
        Dev()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    }
    float x = 0, y = 0;
    if (DiscMouse(g_uiW, g_uiH, x, y) && panel.Hits(x, y)) {
        UIRect(x - 5, y - 1, 11, 3, 0xffffffff);
        UIRect(x - 1, y - 5, 3, 11, 0xffffffff);
    }
}
