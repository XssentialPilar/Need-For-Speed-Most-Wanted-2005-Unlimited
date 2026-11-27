// Split races can keep eight decompressed tiles live. A single tile can exceed
// 1 MiB, and the game's bottom-up pool aborts when a gap of that size is unavailable.
// Keep the engine's existing tile ownership, but allocate bounded split tiles
// outside that pool and pair each allocation with the engine's eviction/cleanup.
struct MapTile {
    int id;
    void* buffer;
    int length;
};
static_assert(sizeof(MapTile) == 12);

static constexpr int kMapSlots = MAX_PLAYERS * 4 + 1;
static constexpr int kMaxTileBytes = 8 * 1024 * 1024;
static void* mapBuffers[kMapSlots]{};
static bool minimapCacheInstalled = false;

static void FreeMapTile(void* buffer) {
    if (!buffer) return;
    for (auto& entry : mapBuffers) {
        if (entry != buffer) continue;
        VirtualFree(buffer, 0, MEM_RELEASE);
        entry = nullptr;
        return;
    }
    ((void(*)(void*))0x4655F0)(buffer);
}

static void LoadSplitMapTiles(uint8_t* manager, const short* requested, int count) {
    const int slotCount = At<int>(manager, 4);
    if (slotCount <= 0 || slotCount > kMapSlots) return;
    auto* tiles = reinterpret_cast<MapTile*>(manager + 0x148);
    short retained[kMapSlots];
    int retainedCount = 0;
    for (int slot = 0; slot < slotCount; ++slot) {
        const auto& tile = tiles[slot];
        if (tile.buffer && std::find(requested, requested + count, tile.id) != requested + count)
            retained[retainedCount++] = static_cast<short>(tile.id);
    }
    // With only resident tiles requested, the native routine evicts obsolete
    // entries and unregisters their chunks without making any pool allocations.
    ((UncompressFn)0x5672F0)(manager, retained, retainedCount);
    const int sourceCount = std::clamp(At<int>(manager, 0), 0, 64);
    for (int index = 0; index < count; ++index) {
        const int id = requested[index];
        if (id < 0 || id >= sourceCount) continue;
        MapTile* vacant = nullptr;
        bool loaded = false;
        for (int slot = 0; slot < slotCount; ++slot) {
            auto& tile = tiles[slot];
            if (!tile.buffer) vacant = &tile;
            else if (tile.id == id) loaded = true;
        }
        if (loaded || !vacant) continue;
        const auto* source = At<uint8_t*>(manager, 0x48 + 4 * id);
        if (!source) continue;
        const int length = *reinterpret_cast<const int*>(source + 8);
        if (length <= 0 || length > kMaxTileBytes) continue;
        auto allocatorSlot = std::find(std::begin(mapBuffers), std::end(mapBuffers), nullptr);
        if (allocatorSlot == std::end(mapBuffers)) continue;
        void* buffer = VirtualAlloc(nullptr, static_cast<SIZE_T>(length), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!buffer) continue;
        *allocatorSlot = buffer;
        *vacant = {id, buffer, length};
        ((void(*)(const void*, void*))0x650350)(source, buffer);
        ((void(*)(void*, int, const void*))0x65D530)(buffer, length, (const void*)0x8A04C4);
    }
}

static uint8_t* FASTCALL InitSplitMapCache(uint8_t* manager, void*, int) {
    ((void*(THISCALL*)(void*, int))0x5671A0)(manager, kMapSlots);
    auto* tiles = reinterpret_cast<MapTile*>(manager + 0x148);
    for (int slot = 9; slot < kMapSlots; ++slot) tiles[slot] = {};
    return manager;
}

static void InstallMinimapCache() {
    constexpr uintptr_t freeSites[] = {0x56734B, 0x56744C};
    for (const auto site : freeSites) {
        if (At<uint8_t>(site) != 0xE8 || site + 5 + At<int32_t>(site + 1) != 0x4655F0) return;
    }
    if (At<uint8_t>(0x579789) != 0x68 || At<int>(0x57978A) != 0x1B4 ||
        At<uint8_t>(0x57979E) != 0xE8 || 0x5797A3 + At<int32_t>(0x57979F) != 0x5671A0) return;
    // Validate the manager expansion before installing any allocation hooks.
    constexpr int bytes = 0x148 + kMapSlots * sizeof(MapTile);
    WriteMem(0x57978A, &bytes, sizeof(bytes));
    if (At<int>(0x57978A) != bytes) return;
    if (!PatchCall(0x57979E, reinterpret_cast<void*>(&InitSplitMapCache), 0x5671A0)) {
        constexpr int originalBytes = 0x1B4;
        WriteMem(0x57978A, &originalBytes, sizeof(originalBytes));
        return;
    }
    for (const auto site : freeSites) {
        if (!PatchCall(site, reinterpret_cast<void*>(&FreeMapTile), 0x4655F0)) return;
    }
    minimapCacheInstalled = true;
}
