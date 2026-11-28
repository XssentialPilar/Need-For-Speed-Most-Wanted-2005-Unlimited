#pragma once

// The installed resource is indexed by PC TextureInfo hash. Keep the index
// and one open handle, and read mip data only when the game creates a texture.
namespace unlimited_textures {

// Keep the serialized IDs from the initial three-profile installer.
enum class Quality { Original, VeryHigh, Ultra, VeryLow, Low, Medium, High };
constexpr Quality choices[]{Quality::VeryLow, Quality::Low, Quality::Medium, Quality::High,
                            Quality::VeryHigh, Quality::Ultra, Quality::Original};
constexpr unsigned edges[]{256, 512, 1024, 2048, 4096, 8192, 0};

unsigned QualityIndex(Quality value) {
    return static_cast<unsigned>(std::find(std::begin(choices), std::end(choices), value) - std::begin(choices));
}

Quality ReadQuality(int id) {
    return id >= 0 && id < static_cast<int>(std::size(choices)) ? static_cast<Quality>(id) : Quality::Original;
}

Quality ChangeQuality(Quality value, int direction) {
    const int count = static_cast<int>(std::size(choices));
    return choices[(static_cast<int>(QualityIndex(value)) + count + direction) % count];
}
constexpr wchar_t preferenceKey[] = L"TextureQuality";
constexpr wchar_t resourceName[] = L"GLOBAL\\UnlimitedTextures.bin";
constexpr wchar_t xboxResourceName[] = L"GLOBAL\\UnlimitedXboxTextures.bin";

struct Header {
    char magic[4];
    std::uint32_t version, count, bytes;
};

struct Entry {
    std::uint32_t hash, offset, bytes, format;
    std::uint16_t width, height;
    std::uint8_t levels, reserved[3];
};

static_assert(sizeof(Header) == 16 && sizeof(Entry) == 24);

struct Mip {
    unsigned pitch, rows;
    unsigned Bytes() const { return pitch * rows; }
};

Mip Layout(D3DFORMAT format, unsigned width, unsigned height) {
    if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8)
        return {width * 4, height};

    unsigned block = 0;
    if (format == D3DFMT_DXT1) block = 8;
    if (format == D3DFMT_DXT3 || format == D3DFMT_DXT5) block = 16;
    return {((width + 3) / 4) * block, (height + 3) / 4};
}

struct TexturePack {
    HANDLE file = INVALID_HANDLE_VALUE;
    SRWLOCK lock = SRWLOCK_INIT;
    std::array<Entry, 2048> entries{};
    unsigned count = 0;
};

TexturePack remaster;
TexturePack console;
Quality preference = Quality::Original;
Quality startup = Quality::Original;
bool installed = false;
volatile LONG allocated = 0;
volatile LONG traceCount = 0;
volatile LONG consoleTraceCount = 0;
const std::uint8_t* xboxImage = nullptr;
const Entry* xboxEntries = nullptr;
unsigned xboxCount = 0;

bool ReadExact(HANDLE file, void* data, DWORD bytes) {
    DWORD read = 0;
    return ReadFile(file, data, bytes, &read, nullptr) && read == bytes;
}

bool Valid(const Entry& entry, unsigned firstPixel, unsigned fileBytes) {
    if (!entry.hash || entry.offset < firstPixel || entry.offset >= fileBytes ||
        !entry.bytes || entry.bytes > fileBytes - entry.offset ||
        !entry.width || entry.width > 8192 || !entry.height || entry.height > 8192 ||
        !entry.levels || entry.levels > 14) return false;

    unsigned maxLevels = 0;
    for (unsigned edge = std::max(entry.width, entry.height); edge; edge /= 2) ++maxLevels;
    if (entry.levels > maxLevels) return false;

    unsigned width = entry.width, height = entry.height;
    unsigned bytes = 0;
    for (unsigned mip = 0; mip < entry.levels; ++mip) {
        const auto rows = Layout(static_cast<D3DFORMAT>(entry.format), width, height);
        if (!rows.pitch || rows.Bytes() > entry.bytes - bytes) return false;
        bytes += rows.Bytes();
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    return bytes == entry.bytes;
}

template<std::size_t N>
void OpenPack(TexturePack& pack, const wchar_t* settings, const wchar_t (&name)[N]) {
    if (!settings[0] || pack.file != INVALID_HANDLE_VALUE) return;
    const auto* separator = std::wcsrchr(settings, L'\\');
    if (!separator || separator - settings + N >= MAX_PATH) return;
    wchar_t path[MAX_PATH]{};
    std::wmemcpy(path, settings, separator - settings + 1);
    std::wcscat(path, name);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER length{};
    Header header{};
    if (GetFileSizeEx(file, &length) && ReadExact(file, &header, sizeof(header)) &&
        std::memcmp(header.magic, "ULTX", sizeof(header.magic)) == 0 && header.version == 1 &&
        header.count && header.count <= pack.entries.size() && length.QuadPart == header.bytes &&
        ReadExact(file, pack.entries.data(), header.count * sizeof(Entry))) {
        const unsigned firstPixel = sizeof(header) + header.count * sizeof(Entry);
        unsigned previous = 0;
        for (unsigned i = 0; i < header.count; ++i) {
            if (pack.entries[i].hash <= previous || !Valid(pack.entries[i], firstPixel, header.bytes)) break;
            previous = pack.entries[i].hash;
            if (i + 1 == header.count) {
                pack.count = header.count;
                pack.file = file;
                return;
            }
        }
    }
    CloseHandle(file);
}

void Open(const wchar_t* settings) { OpenPack(remaster, settings, resourceName); }

void OpenXbox(const wchar_t* settings) {
    OpenPack(console, settings, xboxResourceName);
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&OpenXbox), &module)) return;
    const auto resourceInfo = FindResourceW(module, MAKEINTRESOURCEW(201), MAKEINTRESOURCEW(10));
    if (!resourceInfo) return;
    const unsigned bytes = SizeofResource(module, resourceInfo);
    const auto* data = static_cast<const std::uint8_t*>(LockResource(LoadResource(module, resourceInfo)));
    if (!data || bytes < sizeof(Header)) return;
    const auto& header = *reinterpret_cast<const Header*>(data);
    if (std::memcmp(header.magic, "ULTX", sizeof(header.magic)) || header.version != 1 ||
        header.bytes != bytes || !header.count || header.count > 32) return;
    const unsigned firstPixel = sizeof(header) + header.count * sizeof(Entry);
    if (firstPixel > bytes) return;
    const auto* table = reinterpret_cast<const Entry*>(data + sizeof(header));
    unsigned previous = 0;
    for (unsigned i = 0; i < header.count; ++i) {
        if (table[i].hash <= previous || !Valid(table[i], firstPixel, bytes)) return;
        previous = table[i].hash;
    }
    xboxImage = data;
    xboxEntries = table;
    xboxCount = header.count;
}

const GUID allocationKey{0x7cf3b897, 0x9d26, 0x44a2, {0xa0, 0xc2, 0x62, 0x63, 0x5d, 0xc0, 0x3a, 0x50}};

class Allocation final : public IUnknown {
public:
    explicit Allocation(LONG bytes) : bytes_(bytes) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown) return E_NOINTERFACE;
        *result = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining) {
            InterlockedExchangeAdd(&allocated, -bytes_);
            delete this;
        }
        return remaining;
    }

private:
    volatile LONG references_ = 1;
    const LONG bytes_;
};

std::uint64_t TextureMemoryLimit(unsigned bytes) {
    // Multiple mips and many small textures arrive in the same streaming tick.
    // Include outstanding reservations against the snapshot so a burst of
    // uploads cannot spend the same headroom twice.
    struct Budget {
        DWORD sampledAt;
        const void* adapterAtSample;
        std::uint64_t limit;
        bool valid;
    };
    static thread_local Budget budget{};
    const auto now = GetTickCount();
    if (!budget.valid || budget.adapterAtSample != adapter || now - budget.sampledAt >= 100) {
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (!GlobalMemoryStatusEx(&memory)) return 0;
        auto headroom = std::min({memory.ullAvailVirtual / 4,
                                  memory.ullAvailPhys / 8, memory.ullAvailPageFile / 8});
        DXGI_QUERY_VIDEO_MEMORY_INFO gpu{};
        if (adapter && SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &gpu)) && gpu.Budget)
            headroom = std::min(headroom, gpu.CurrentUsage < gpu.Budget ? (gpu.Budget - gpu.CurrentUsage) / 2 : 0ull);
        const auto used = static_cast<unsigned>(InterlockedCompareExchange(&allocated, 0, 0));
        budget = {now, adapter, std::min({memory.ullTotalVirtual / 3, memory.ullTotalPhys / 8, used + headroom}), true};
    }
    return bytes <= budget.limit ? budget.limit : 0;
}

Allocation* Reserve(unsigned bytes) {
    const auto limit = TextureMemoryLimit(bytes);

    LONG current = InterlockedCompareExchange(&allocated, 0, 0);
    do {
        if (static_cast<unsigned>(current) + bytes > limit) return nullptr;
        const LONG previous = InterlockedCompareExchange(&allocated, current + bytes, current);
        if (previous == current) {
            auto* charge = new (std::nothrow) Allocation(bytes);
            if (!charge) InterlockedExchangeAdd(&allocated, -static_cast<LONG>(bytes));
            return charge;
        }
        current = previous;
    } while (true);
}

bool ReadMip(IDirect3DTexture9* texture, unsigned level, const Mip& mip, HANDLE file, const std::uint8_t* memory) {
    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(level, &locked, nullptr, 0))) return false;
    bool ok = locked.pBits && locked.Pitch >= static_cast<int>(mip.pitch);
    auto* row = static_cast<std::uint8_t*>(locked.pBits);
    if (ok && memory) {
        if (locked.Pitch == static_cast<int>(mip.pitch)) std::memcpy(row, memory, mip.Bytes());
        else for (unsigned i = 0; i < mip.rows; ++i)
            std::memcpy(row + i * locked.Pitch, memory + i * mip.pitch, mip.pitch);
    } else if (ok && locked.Pitch == static_cast<int>(mip.pitch)) {
        ok = ReadExact(file, row, mip.Bytes());
    } else if (ok) {
        // Padded pitches are common on small mip tails. Batch the file reads.
        std::array<std::uint8_t, 65536> buffer;
        const unsigned maxRows = static_cast<unsigned>(buffer.size()) / mip.pitch;
        for (unsigned pending = mip.rows; ok && pending; ) {
            const auto rows = std::min(pending, maxRows);
            ok = rows && ReadExact(file, buffer.data(), rows * mip.pitch);
            if (!ok) break;
            for (unsigned i = 0; i < rows; ++i) {
                std::memcpy(row, buffer.data() + i * mip.pitch, mip.pitch);
                row += locked.Pitch;
            }
            pending -= rows;
        }
    }
    return SUCCEEDED(texture->UnlockRect(level)) && ok;
}

bool Fill(IDirect3DTexture9* texture, unsigned offset, D3DFORMAT format,
          unsigned width, unsigned height, unsigned levels, TexturePack* pack, const std::uint8_t* memory) {
    // Small levels are contiguous in the archive. One read also releases the
    // archive lock before handing their pixels to the graphics driver.
    constexpr unsigned batchBytes = 128 * 1024;
    static thread_local std::array<std::uint8_t, batchBytes> batch;
    unsigned total = 0;
    if (!memory && Layout(format, width, height).Bytes() <= batchBytes) {
        unsigned w = width, h = height;
        for (unsigned i = 0; i < levels; ++i) {
            total += Layout(format, w, h).Bytes();
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        if (total <= batchBytes) {
            LARGE_INTEGER start{};
            start.QuadPart = offset;
            AcquireSRWLockExclusive(&pack->lock);
            const bool read = SetFilePointerEx(pack->file, start, nullptr, FILE_BEGIN) && ReadExact(pack->file, batch.data(), total);
            ReleaseSRWLockExclusive(&pack->lock);
            if (!read) return false;
            memory = batch.data();
            offset = 0;
        }
    }
    if (memory) {
        for (unsigned level = 0; level < levels; ++level) {
            const auto mip = Layout(format, width, height);
            if (!ReadMip(texture, level, mip, INVALID_HANDLE_VALUE, memory + offset)) return false;
            offset += mip.Bytes();
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        return true;
    }
    LARGE_INTEGER start{};
    start.QuadPart = offset;
    AcquireSRWLockExclusive(&pack->lock);
    bool ok = SetFilePointerEx(pack->file, start, nullptr, FILE_BEGIN) != 0;
    for (unsigned level = 0; ok && level < levels; ++level) {
        ok = ReadMip(texture, level, Layout(format, width, height), pack->file, nullptr);
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    ReleaseSRWLockExclusive(&pack->lock);
    return ok;
}

const Entry* Find(const Entry* table, unsigned count, std::uint32_t hash) {
    if (!count) return nullptr;
    const auto* end = table + count;
    const auto* found = std::lower_bound(table, end, hash,
        [](const Entry& entry, std::uint32_t key) { return entry.hash < key; });
    return found != end && found->hash == hash ? found : nullptr;
}

IDirect3DTexture9* CreateOverride(IDirect3DDevice9* device, const D3DSURFACE_DESC& native,
                                  const Entry* found, unsigned edgeLimit, TexturePack* pack,
                                  const std::uint8_t* memory = nullptr) {
    const auto hash = found->hash;
    D3DCAPS9 caps{};
    if (FAILED(device->GetDeviceCaps(&caps))) return nullptr;
    const unsigned nativeEdge = std::max(native.Width, native.Height);
    const auto format = static_cast<D3DFORMAT>(found->format);
    unsigned width = found->width, height = found->height;
    unsigned offset = found->offset, bytes = found->bytes;
    for (unsigned levels = found->levels; levels; --levels) {
        if (levels != found->levels && std::max(width, height) < std::min(edgeLimit, nativeEdge)) break;
        if (width <= std::min<unsigned>(caps.MaxTextureWidth, edgeLimit) &&
            height <= std::min<unsigned>(caps.MaxTextureHeight, edgeLimit)) {
            if (auto* charge = Reserve(bytes)) {
                IDirect3DTexture9* texture = nullptr;
                const auto result = device->CreateTexture(width, height, levels, 0, format,
                                                           native.Pool, &texture, nullptr);
                if (SUCCEEDED(result) && texture &&
                    SUCCEEDED(texture->SetPrivateData(allocationKey, charge, sizeof(IUnknown*), D3DSPD_IUNKNOWN)) &&
                    Fill(texture, offset, format, width, height, levels, pack, memory)) {
                    charge->Release();
                    if (logPath[0] && (memory || InterlockedIncrement(pack == &console ? &consoleTraceCount : &traceCount) <= 24 ||
                                       std::max(width, height) > 4096)) {
                        if (LogFile log; log.file)
                            std::fprintf(log.file, "Texture %08x: GPU=%ux%u mips=%u profile=%u bytes=%u%s\n",
                                         hash, width, height, levels, static_cast<unsigned>(startup), bytes,
                                         memory ? " Xbox 360 4.2" : pack == &console ? " Xbox 360 pack" : "");
                    }
                    return texture;
                }
                if (texture) texture->Release();
                charge->Release();
            }
        }
        const auto skipped = Layout(format, width, height).Bytes();
        offset += skipped;
        bytes -= skipped;
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    return nullptr;
}


// The game creates texture data for far more materials than it draws at once.
// Promote large textures at the renderer's actual bind sites. The original
// resource stays valid for effects, device resets, and other engine callers.
constexpr GUID demandKey{0xf150c6b7, 0xe406, 0x46c3, {0xbe, 0x49, 0x11, 0x7e, 0x68, 0x1b, 0x2f, 0xc2}};
constexpr DWORD residentTime = 8000;
constexpr unsigned eagerBytes = 512 * 1024;
using EffectBind = HRESULT (WINAPI*)(void*, const char*, IDirect3DBaseTexture9*);
using CreateEffect = HRESULT (WINAPI*)(IDirect3DDevice9*, HMODULE, const char*, const void*, void*,
                                       DWORD, void*, void**, void**);
EffectBind nativeEffectBind = nullptr;
CreateEffect nativeCreateEffect = nullptr;
std::atomic<bool> demandActive{false};
SRWLOCK demandLock = SRWLOCK_INIT;

class Demand final : public IUnknown {
public:
    Demand(const Entry* asset, const D3DSURFACE_DESC& description, unsigned limit, TexturePack* source)
        : entry(asset), native(description), edge(limit), pack(source) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown) return E_NOINTERFACE;
        *result = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining) delete this;
        return remaining;
    }

    bool HasNativeOwner() { return InterlockedCompareExchange(&references_, 0, 0) > 1; }

    const Entry* const entry;
    const D3DSURFACE_DESC native;
    const unsigned edge;
    TexturePack* const pack;
    IDirect3DTexture9* image = nullptr; // Owned while listed in the resident array.
    std::atomic<DWORD> lastUse{0};
    DWORD retryAt = 0;
    bool queued = false;

private:
    volatile LONG references_ = 1;
};

std::array<Demand*, 256> pending{};
std::array<Demand*, 1024> residents{};
unsigned pendingStart = 0;
unsigned pendingCount = 0;
unsigned residentCount = 0;

IDirect3DTexture9* FindDemandImage(IDirect3DBaseTexture9* texture) {
    if (!demandActive || !texture) return nullptr;

    IUnknown* tag = nullptr;
    DWORD bytes = sizeof(tag);
    if (FAILED(texture->GetPrivateData(demandKey, &tag, &bytes)) || bytes != sizeof(tag) || !tag)
        return nullptr;

    auto* demand = static_cast<Demand*>(tag);
    IDirect3DTexture9* image = nullptr;
    const DWORD now = GetTickCount();
    AcquireSRWLockExclusive(&demandLock);
    demand->lastUse = now;
    if (demand->image) {
        image = demand->image;
        image->AddRef();
    } else if (!demand->queued && pendingCount < pending.size() &&
               (!demand->retryAt || static_cast<LONG>(now - demand->retryAt) >= 0)) {
        demand->queued = true;
        demand->AddRef();
        pending[(pendingStart + pendingCount) % pending.size()] = demand;
        ++pendingCount;
    }
    ReleaseSRWLockExclusive(&demandLock);
    demand->Release();
    return image;
}

HRESULT WINAPI BindEffectTexture(void* effect, const char* parameter, IDirect3DBaseTexture9* texture) {
    auto* image = FindDemandImage(texture);
    auto result = nativeEffectBind(effect, parameter, image ? image : texture);
    if (image) {
        image->Release();
        if (FAILED(result)) result = nativeEffectBind(effect, parameter, texture);
    }
    return result;
}

void TrackEffectTextureBindings(IDirect3DDevice9* device, HRESULT result, void** effect) {
    if (FAILED(result) || !effect || !*effect || device != trackedDevice) return;
    auto** table = *reinterpret_cast<void***>(*effect);
    // Retail D3DX9_26 uses ID3DXBaseEffect::SetTexture at byte offset d0.
    constexpr unsigned bindingSlot = 0xd0 / sizeof(void*);
    const auto binding = reinterpret_cast<EffectBind>(table[bindingSlot]);
    if (!nativeEffectBind) nativeEffectBind = binding;
    if (binding == &BindEffectTexture ||
        (binding == nativeEffectBind && ReplaceMethod(table, bindingSlot, reinterpret_cast<void*>(&BindEffectTexture))))
        demandActive = true;
}

HRESULT WINAPI CreateEffectWithDemand(IDirect3DDevice9* device, HMODULE source, const char* resourceId,
                                     const void* defines, void* include, DWORD flags,
                                     void* pool, void** effect, void** errors) {
    const auto result = nativeCreateEffect(device, source, resourceId, defines, include, flags, pool, effect, errors);
    TrackEffectTextureBindings(device, result, effect);
    return result;
}

using CreateMemoryEffect = HRESULT (WINAPI*)(IDirect3DDevice9*, const void*, UINT, const void*, void*,
                                             DWORD, void*, void**, void**);
CreateMemoryEffect nativeMemoryEffect = nullptr;

HRESULT WINAPI CreateWorldEffect(IDirect3DDevice9* device, const void* data, UINT size, const void* defines,
                                 void* include, DWORD flags, void* pool, void** effect, void** errors) {
    const auto result = nativeMemoryEffect(device, data, size, defines, include, flags, pool, effect, errors);
    TrackEffectTextureBindings(device, result, effect);
    if (LogFile log; log.file) std::fprintf(log.file, "World effect: %u bytes, D3D9 result %08lx\n", size,
                                           static_cast<unsigned long>(result));
    return result;
}

bool InstallEffectDemandHook() {
    if (!console.count && (!remaster.count || edges[QualityIndex(startup)] < 2048)) return false;
    const auto module = GetModuleHandleW(L"d3dx9_26.dll");
    if (!module) return false;
    const auto native = reinterpret_cast<CreateEffect>(GetProcAddress(module, "D3DXCreateEffectFromResourceA"));
    constexpr std::uintptr_t import = 0x4904c0;
    if (!native || GameValue<CreateEffect>(import) != native) return false;
    nativeCreateEffect = native;
    const auto hook = &CreateEffectWithDemand;
    return WriteGameBytes(import, &hook, sizeof(hook));
}

void TrimDemand(DWORD now, bool clear) {
    // Unlink in one pass, then release outside the lock so the GPU driver
    // can call back without blocking texture binds.
    std::array<std::pair<Demand*, IDirect3DTexture9*>, 1024> expired;
    unsigned count = 0;
    AcquireSRWLockExclusive(&demandLock);
    for (unsigned i = 0; i < residentCount;) {
        auto* candidate = residents[i];
        if (!clear && candidate->HasNativeOwner() && now - candidate->lastUse < residentTime) {
            ++i;
            continue;
        }
        expired[count++] = {candidate, candidate->image};
        candidate->image = nullptr;
        residents[i] = residents[--residentCount];
    }
    ReleaseSRWLockExclusive(&demandLock);
    for (unsigned i = 0; i < count; ++i) {
        expired[i].second->Release();
        expired[i].first->Release();
    }
}

Demand* PopDemand() {
    AcquireSRWLockExclusive(&demandLock);
    Demand* demand = nullptr;
    if (pendingCount) {
        demand = pending[pendingStart];
        pendingStart = (pendingStart + 1) % pending.size();
        --pendingCount;
    }
    ReleaseSRWLockExclusive(&demandLock);
    return demand;
}

void ServiceDemand(IDirect3DDevice9* device) {
    if (!demandActive || device != trackedDevice) return;
    const DWORD now = GetTickCount();
    static DWORD lastTrim = 0;
    if (now - lastTrim >= 500) {
        TrimDemand(now, false);
        lastTrim = now;
    }

    // Limit work to one promotion per presented frame. Stale requests are
    // skipped so textures currently visible do not wait behind earlier views.
    for (unsigned i = 0; i < 8; ++i) {
        auto* demand = PopDemand();
        if (!demand) return;
        const bool current = demand->HasNativeOwner() && now - demand->lastUse < residentTime;
        auto* image = current ? CreateOverride(device, demand->native, demand->entry, demand->edge, demand->pack) : nullptr;
        AcquireSRWLockExclusive(&demandLock);
        demand->queued = false;
        if (image && residentCount < residents.size()) {
            demand->image = image;
            residents[residentCount++] = demand; // Take ownership of the pending reference.
            ReleaseSRWLockExclusive(&demandLock);
            return;
        }
        if (current) demand->retryAt = now + 2000;
        ReleaseSRWLockExclusive(&demandLock);
        if (image) image->Release();
        demand->Release();
        if (current) return;
    }
}

void ClearDemand() {
    demandActive = false;
    TrimDemand(GetTickCount(), true);
    while (auto* demand = PopDemand()) {
        AcquireSRWLockExclusive(&demandLock);
        demand->queued = false;
        ReleaseSRWLockExclusive(&demandLock);
        demand->Release();
    }
}

bool Upload(IDirect3DDevice9* device, IDirect3DTexture9** destination, std::uint32_t hash) {
    if (!device || !*destination) return false;
    auto* pack = &remaster;
    const auto qualityEdge = edges[QualityIndex(startup)];
    constexpr unsigned glossyWindow = 0x0bddf918, golfSandOpacity = 0xd97a7da0;
    const bool worldBinding = hash == glossyWindow || hash == golfSandOpacity;
    const auto* embedded = Find(xboxEntries, xboxCount, hash);
    const auto* found = embedded && !worldBinding ? embedded :
        (qualityEdge ? Find(remaster.entries.data(), remaster.count, hash) : nullptr);
    if (!found) found = embedded;
    const auto* memory = found && found == embedded ? xboxImage : nullptr;
    if (!found) {
        pack = &console;
        found = Find(console.entries.data(), console.count, hash);
    }
    if (!found) return false;
    D3DSURFACE_DESC native{};
    if (FAILED((*destination)->GetLevelDesc(0, &native))) return false;
    const auto edgeLimit = memory && !worldBinding ? 128u : (pack == &console || memory) ?
        std::min(qualityEdge ? qualityEdge : 1024u, 2048u) : qualityEdge;
    if (!memory && demandActive && (pack == &console || edgeLimit >= 2048) &&
        found->bytes > eagerBytes && native.Pool == D3DPOOL_MANAGED) {
        auto* demand = new (std::nothrow) Demand(found, native, edgeLimit, pack);
        if (demand) {
            (*destination)->SetPrivateData(demandKey, demand, sizeof(IUnknown*), D3DSPD_IUNKNOWN);
            demand->Release();
        }
        return false;
    }
    auto* texture = CreateOverride(device, native, found, edgeLimit, pack, memory);
    if (!texture) return false;
    (*destination)->Release();
    *destination = texture;
    return true;
}

}
