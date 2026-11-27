#pragma once

#include <algorithm>
#include <array>
#include <audioclient.h>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <new>

namespace {
namespace unlimited_audio {

enum class Output { Automatic, Surround51, Surround71, Stereo };
constexpr unsigned outputCount = 4;
Output preference = Output::Automatic;
Output startupPreference = Output::Automatic;
unsigned endpointChannels = 0;
unsigned desiredChannels = 0;
constexpr DWORD surroundMask = 0x63f;
constexpr GUID pcmSubtype = {1, 0, 0x0010, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
constexpr GUID bufferIid = {0x279afa85, 0x4981, 0x11ce, {0xa5, 0x21, 0, 0x20, 0xaf, 0x0b, 0xe5, 0x60}};
constexpr GUID unknownIid = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

constexpr unsigned SelectChannels(Output output, unsigned available) {
    if (output == Output::Stereo) return 2;
    return available >= 8 && output != Output::Surround51 ? 8 : available >= 6 ? 6 : 0;
}

unsigned DetectEndpointChannels() {
    constexpr GUID enumeratorClass = {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
    using Init = HRESULT (WINAPI*)(LPVOID, DWORD);
    using Uninit = void (WINAPI*)();
    using Create = HRESULT (WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    using Free = void (WINAPI*)(LPVOID);
    const auto library = LoadSystemLibrary(L"ole32.dll");
    if (!library) return 0;
    const auto init = reinterpret_cast<Init>(GetProcAddress(library, "CoInitializeEx"));
    const auto uninit = reinterpret_cast<Uninit>(GetProcAddress(library, "CoUninitialize"));
    const auto create = reinterpret_cast<Create>(GetProcAddress(library, "CoCreateInstance"));
    const auto free = reinterpret_cast<Free>(GetProcAddress(library, "CoTaskMemFree"));
    unsigned channels = 0;
    if (init && uninit && create && free) {
        const auto apartment = init(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE) {
            IMMDeviceEnumerator* enumerator = nullptr;
            IMMDevice* endpoint = nullptr;
            IAudioClient* audioClient = nullptr;
            WAVEFORMATEX* format = nullptr;
            if (SUCCEEDED(create(enumeratorClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))) &&
                SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint)) &&
                SUCCEEDED(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER,
                                             nullptr, reinterpret_cast<void**>(&audioClient))) &&
                SUCCEEDED(audioClient->GetMixFormat(&format)) && format) {
                channels = format->nChannels;
            }
            if (format) free(format);
            if (audioClient) audioClient->Release();
            if (endpoint) endpoint->Release();
            if (enumerator) enumerator->Release();
            if (SUCCEEDED(apartment)) uninit();
        }
    }
    FreeLibrary(library);
    return channels;
}

class PlaybackTone {
public:
    explicit PlaybackTone(unsigned rate)
        : lowBand_(1.0f - std::exp(-6.2831853f * 80.0f / rate)),
          presenceBand_(1.0f - std::exp(-6.2831853f * 2500.0f / rate)) {}

    void Reset() { low_.fill(0.0f); presence_.fill(0.0f); }

    void Convert(const std::int16_t* input, std::int16_t* output, std::size_t frames,
                 unsigned sourceChannels, unsigned outputChannels) {
        for (; frames; --frames, input += sourceChannels, output += outputChannels) {
            for (unsigned ch = 0; ch < sourceChannels; ++ch) {
                const float x = input[ch];
                if (sourceChannels == 6 && ch == 3) {
                    output[ch] = input[ch];  // Preserve the native LFE channel.
                    continue;
                }
                low_[ch] += lowBand_ * (x - low_[ch]);
                presence_[ch] += presenceBand_ * (x - presence_[ch]);
                const float shaped = 0.91f * x - 0.07f * low_[ch] + 0.065f * (x - presence_[ch]);
                output[ch] = static_cast<std::int16_t>(std::clamp(shaped, -32768.0f, 32767.0f));
            }
            if (sourceChannels == 6 && outputChannels == 8) {
                // Keep lateral energy centered on the game's original side
                // direction. A smaller rear copy adds coverage without pulling
                // the surround image behind the listener.
                for (unsigned ch = 4; ch < 6; ++ch) {
                    const float surround = output[ch];
                    output[ch + 2] = static_cast<std::int16_t>(surround * 0.85f);
                    output[ch] = static_cast<std::int16_t>(surround * 0.5267827f);
                }
            }
        }
    }

private:
    std::array<float, 6> low_{}, presence_{};
    float lowBand_, presenceBand_;
};

WAVEFORMATEXTENSIBLE ExpandedFormat(DWORD sampleRate) {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 8;
    format.Format.nSamplesPerSec = sampleRate;
    format.Format.nBlockAlign = 8 * sizeof(std::int16_t);
    format.Format.nAvgBytesPerSec = format.Format.nBlockAlign * sampleRate;
    format.Format.wBitsPerSample = 16;
    format.Format.cbSize = sizeof(format) - sizeof(format.Format);
    format.Samples.wValidBitsPerSample = 16;
    format.dwChannelMask = surroundMask;
    format.SubFormat = pcmSubtype;
    return format;
}

// Keep the native mixer ring geometry while processing the samples committed to DirectSound.
class PlaybackBuffer final : public IDirectSoundBuffer {
public:
    PlaybackBuffer(IDirectSoundBuffer* target, DWORD virtualBytes, unsigned rate,
                   unsigned sourceChannels, unsigned channels)
        : target_(target), virtualBytes_(virtualBytes), sourceChannels_(sourceChannels), channels_(channels),
          sourceStride(sourceChannels * sizeof(std::int16_t)), outputStride(channels * sizeof(std::int16_t)),
          rate_(rate), tone_(rate),
          pcm_(new (std::nothrow) std::int16_t[virtualBytes / sizeof(std::int16_t)]) {}

    bool Ready() const { return pcm_ != nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (!IsEqualGUID(iid, unknownIid) && !IsEqualGUID(iid, bufferIid)) return E_NOINTERFACE;
        *object = static_cast<IDirectSoundBuffer*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS caps) noexcept override {
        const auto result = target_->GetCaps(caps);
        if (SUCCEEDED(result)) caps->dwBufferBytes = virtualBytes_;
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD play, LPDWORD write) noexcept override {
        const auto result = target_->GetCurrentPosition(play, write);
        if (SUCCEEDED(result)) {
            if (play) *play = VirtualOffset(*play);
            if (write) *write = VirtualOffset(*write);
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX format, DWORD bytes, LPDWORD written) noexcept override {
        if (sourceChannels_ == channels_) return target_->GetFormat(format, bytes, written);
        if (!format && !written) return DSERR_INVALIDPARAM;
        auto copy = ExpandedFormat(rate_);
        copy.Format.nChannels = 6;
        copy.Format.nBlockAlign = sourceStride;
        copy.Format.nAvgBytesPerSec = sourceStride * copy.Format.nSamplesPerSec;
        copy.dwChannelMask = 0x3f;
        if (format) {
            if (bytes < sizeof(copy)) return DSERR_INVALIDPARAM;
            std::memcpy(format, &copy, sizeof(copy));
        }
        if (written) *written = sizeof(copy);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG volume) noexcept override { return target_->GetVolume(volume); }
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG pan) noexcept override { return target_->GetPan(pan); }
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD frequency) noexcept override { return target_->GetFrequency(frequency); }
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD status) noexcept override { return target_->GetStatus(status); }
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) noexcept override { return DSERR_ALREADYINITIALIZED; }
    HRESULT STDMETHODCALLTYPE Lock(DWORD offset, DWORD bytes, LPVOID* first, LPDWORD firstBytes,
                                   LPVOID* second, LPDWORD secondBytes, DWORD flags) noexcept override {
        if (locked_) return DSERR_INVALIDCALL;
        if (!first || !firstBytes || ((second == nullptr) != (secondBytes == nullptr))) return DSERR_INVALIDPARAM;
        if (flags & DSBLOCK_ENTIREBUFFER) bytes = virtualBytes_;
        if (offset >= virtualBytes_ || offset % sourceStride || !bytes ||
            bytes > virtualBytes_ || bytes % sourceStride) return DSERR_INVALIDPARAM;
        const auto result = target_->Lock(OutputOffset(offset), OutputOffset(bytes),
                                           &firstTarget_, &firstOutputBytes_,
                                           &secondTarget_, &secondOutputBytes_, flags);
        if (FAILED(result)) return result;
        if (firstOutputBytes_ % outputStride || secondOutputBytes_ % outputStride ||
            firstOutputBytes_ > OutputOffset(virtualBytes_) ||
            secondOutputBytes_ > OutputOffset(virtualBytes_) - firstOutputBytes_ ||
            (secondOutputBytes_ && !second)) {
            target_->Unlock(firstTarget_, firstOutputBytes_, secondTarget_, secondOutputBytes_);
            return DSERR_INVALIDPARAM;
        }
        *first = pcm_;
        *firstBytes = VirtualOffset(firstOutputBytes_);
        if (second) {
            *second = secondOutputBytes_ ? pcm_ + *firstBytes / sizeof(std::int16_t) : nullptr;
            *secondBytes = VirtualOffset(secondOutputBytes_);
        }
        locked_ = true;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Play(DWORD reserved, DWORD priority, DWORD flags) noexcept override {
        return target_->Play(reserved, priority, flags);
    }
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD bytes) noexcept override {
        if (bytes % sourceStride) return DSERR_INVALIDPARAM;
        const auto result = target_->SetCurrentPosition(OutputOffset(bytes));
        if (SUCCEEDED(result)) tone_.Reset();
        return result;
    }
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) noexcept override { return DSERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetVolume(LONG volume) noexcept override { return target_->SetVolume(volume); }
    HRESULT STDMETHODCALLTYPE SetPan(LONG pan) noexcept override { return target_->SetPan(pan); }
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD frequency) noexcept override { return target_->SetFrequency(frequency); }
    HRESULT STDMETHODCALLTYPE Stop() noexcept override { return target_->Stop(); }
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID first, DWORD firstBytes, LPVOID second, DWORD secondBytes) noexcept override {
        if (!locked_ || first != pcm_ || firstBytes != VirtualOffset(firstOutputBytes_) ||
            secondBytes != VirtualOffset(secondOutputBytes_) ||
            (secondBytes && second != pcm_ + firstBytes / sizeof(std::int16_t))) return DSERR_INVALIDPARAM;
        unlimited_discx::MixNative(pcm_, firstBytes / sourceStride, sourceChannels_, rate_);
        tone_.Convert(pcm_, static_cast<std::int16_t*>(firstTarget_), firstBytes / sourceStride, sourceChannels_, channels_);
        if (secondBytes) {
            unlimited_discx::MixNative(static_cast<std::int16_t*>(second), secondBytes / sourceStride, sourceChannels_, rate_);
            tone_.Convert(static_cast<std::int16_t*>(second), static_cast<std::int16_t*>(secondTarget_),
                          secondBytes / sourceStride, sourceChannels_, channels_);
        }
        locked_ = false;
        return target_->Unlock(firstTarget_, firstOutputBytes_, secondTarget_, secondOutputBytes_);
    }
    HRESULT STDMETHODCALLTYPE Restore() noexcept override {
        tone_.Reset();
        return target_->Restore();
    }

private:
    DWORD VirtualOffset(DWORD bytes) const { return bytes / outputStride * sourceStride; }
    DWORD OutputOffset(DWORD bytes) const { return bytes / sourceStride * outputStride; }
    ~PlaybackBuffer() {
        if (locked_) target_->Unlock(firstTarget_, firstOutputBytes_, secondTarget_, secondOutputBytes_);
        target_->Release();
        delete[] pcm_;
    }

    IDirectSoundBuffer* const target_;
    const DWORD virtualBytes_;
    const unsigned sourceChannels_, channels_;
    const DWORD sourceStride, outputStride;
    const unsigned rate_;
    PlaybackTone tone_;
    std::int16_t* const pcm_;
    LONG references_ = 1;
    bool locked_ = false;
    void* firstTarget_ = nullptr;
    void* secondTarget_ = nullptr;
    DWORD firstOutputBytes_ = 0;
    DWORD secondOutputBytes_ = 0;
};

int __cdecl RouteNativeMixer(int nativeMode) {
    using SetOutputMode = int (__cdecl*)(int);
    const auto setMode = GameFunction<SetOutputMode>(0x41b4ac);
    const int override = desiredChannels == 2 ? 1 : desiredChannels >= 6 ? 3 : nativeMode;
    if (override != nativeMode && setMode(override) >= 0) return 0;
    return setMode(nativeMode);
}

HRESULT __cdecl CreatePlaybackBuffer(int channels, int rate) {
    using SetPcmFormat = HRESULT (__cdecl*)(int, int);
    const auto createNative = GameFunction<SetPcmFormat>(0x42007e);
    const HRESULT result = createNative(channels, rate);
    if (FAILED(result)) {
        unlimited_discx::SetNativeMixer(0);
        return result;
    }
    auto*& original = GameValue<IDirectSoundBuffer*>(0x5c22b0);
    const DWORD ringBytes = GameValue<DWORD>(0x5c22b4);
    const DWORD inputStride = channels * sizeof(std::int16_t);
    if (!original || (channels != 2 && channels != 6) || rate <= 0 || !ringBytes || ringBytes % inputStride) {
        unlimited_discx::SetNativeMixer(0);
        return result;
    }

    unsigned playbackChannels = channels;
    IDirectSoundBuffer* backing = original;
    if (desiredChannels == 8 && channels == 6) {
        auto format = ExpandedFormat(rate);
        DSBUFFERDESC desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_LOCSOFTWARE | DSBCAPS_GETCURRENTPOSITION2;
        desc.dwBufferBytes = (ringBytes / inputStride) * format.Format.nBlockAlign;
        desc.lpwfxFormat = &format.Format;
        IDirectSoundBuffer* expanded = nullptr;
        if (SUCCEEDED(GameValue<IDirectSound*>(0x5c22a8)->CreateSoundBuffer(&desc, &expanded, nullptr)) && expanded) {
            backing = expanded;
            playbackChannels = 8;
        }
    }

    // Give the adapter its own reference. The original remains available if
    // constructing the adapter or allocating its staging buffer fails.
    if (backing == original) backing->AddRef();
    auto* adapter = new (std::nothrow) PlaybackBuffer(backing, ringBytes, rate, channels, playbackChannels);
    const bool processed = adapter && adapter->Ready();
    if (processed) {
        original->Release();
        original = adapter;
        unlimited_discx::SetNativeMixer(rate);
    } else {
        unlimited_discx::SetNativeMixer(0);
        if (adapter) adapter->Release();
        else backing->Release();
        playbackChannels = channels;
    }
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Audio: endpoint=%u desired=%u engine_mix=%i playback=%u tone=%u\n",
                     endpointChannels, desiredChannels, channels, playbackChannels, unsigned(processed));
    }
    return result;
}

void __fastcall DrawOutput(std::uint8_t* option) {
    const bool spanish = GameValue<int>(0x4f41c0) == 4;
    using SetText = void (__thiscall*)(std::uint8_t*, const char*);
    const auto writeText = GameFunction<SetText>(0x1bcc10);
    const auto draw = [&](std::size_t offset, const char* text) {
        auto* object = *reinterpret_cast<std::uint8_t**>(option + offset);
        if (!object) return;
        writeText(object, text);
        *reinterpret_cast<std::uint32_t*>(object + 0x1c) |= 0x400000u;
    };
    const bool restart = preference != startupPreference;
    constexpr const char* spanishCurrent[] = {u8"Automático", "5.1", "7.1", u8"Estéreo"};
    constexpr const char* englishCurrent[] = {"Automatic", "5.1", "7.1", "Stereo"};
    constexpr const char* spanishPending[] = {"Auto (reiniciar)", "5.1 (reiniciar)", "7.1 (reiniciar)", u8"Estéreo (reiniciar)"};
    constexpr const char* englishPending[] = {"Auto (restart)", "5.1 (restart)", "7.1 (restart)", "Stereo (restart)"};
    const auto index = static_cast<unsigned>(preference);
    draw(0x2c, spanish ? "Salida de audio" : "Audio output");
    draw(0x30, (restart ? (spanish ? spanishPending : englishPending)
                        : (spanish ? spanishCurrent : englishCurrent))[index]);
}

void __fastcall ChangeOutput(std::uint8_t* option, void*, const char*, std::uint32_t message) {
    constexpr std::uint32_t left = 0x9120409e, right = 0xb5971bf1;
    if ((message != left && message != right) || !settingsPath[0]) return;
    const unsigned next = (static_cast<unsigned>(preference) + (message == left ? outputCount - 1 : 1)) % outputCount;
    const wchar_t value[] = {static_cast<wchar_t>(L'0' + next), L'\0'};
    if (WritePrivateProfileStringW(L"Audio", L"Output", value, settingsPath)) {
        preference = static_cast<Output>(next);
        option[0x2a] = 1;
        DrawOutput(option);
    }
}

// Use the stock switch's input, widget placement and localized Yes/No labels.
void __fastcall DrawMusicSource(std::uint8_t* option) {
    const auto draw = GameFunction<void (__thiscall*)(void*)>(0x11b060);
    auto* database = GameValue<std::uint8_t*>(0x51cf90);
    auto* profile = database ? *reinterpret_cast<std::uint8_t**>(database + 0x10) : nullptr;
    if (!profile || !unlimited_discx::Selected()) {
        draw(option);
        return;
    }

    auto& traxMode = *reinterpret_cast<int*>(profile + 0x78);
    const int saved = traxMode;
    traxMode = unlimited_discx::Enabled();
    draw(option);
    traxMode = saved;

    if (auto* title = *reinterpret_cast<std::uint8_t**>(option + 0x2c)) {
        GameFunction<void (__thiscall*)(void*, const char*)>(0x1bcc10)(title, "EAX Discx");
        *reinterpret_cast<std::uint32_t*>(title + 0x1c) |= 0x400000u;
    }
}

void __fastcall ChangeMusicSource(std::uint8_t* option, void*, const char* package, std::uint32_t message) {
    if (!unlimited_discx::Selected()) {
        GameFunction<void (__thiscall*)(void*, const char*, std::uint32_t)>(0x10f2d0)(option, package, message);
        return;
    }
    if (message == 0x9120409e || message == 0xb5971bf1)
        unlimited_discx::SetEnabled(!unlimited_discx::Enabled());
    option[0x2a] = 1;
    using Notify = void (__thiscall*)(void*, std::uint32_t);
    auto** table = *reinterpret_cast<void***>(option);
    reinterpret_cast<Notify>(table[14])(option, message);
    DrawMusicSource(option);
}

void __fastcall SetupAudio(std::uint8_t* screen, void*) {
    using Setup = void (__thiscall*)(std::uint8_t*);
    GameFunction<Setup>(0x129310)(screen);
    const auto nativeTable = reinterpret_cast<void**>(&GameValue<std::uint8_t>(0x49bac8));
    const auto musicNative = reinterpret_cast<void**>(&GameValue<std::uint8_t>(0x49b98c));
    static std::array<void*, 15> musicTable{};
    if (!musicTable[0]) {
        std::copy_n(musicNative, musicTable.size(), musicTable.begin());
        musicTable[1] = reinterpret_cast<void*>(&ChangeMusicSource);
        musicTable[3] = reinterpret_cast<void*>(&DrawMusicSource);
    }
    static std::array<void*, 15> table{};
    if (!table[0]) {
        std::copy_n(nativeTable, table.size(), table.begin());
        table[1] = reinterpret_cast<void*>(&ChangeOutput);
        table[3] = reinterpret_cast<void*>(&DrawOutput);
    }

    auto* root = screen + 0x2c;
    auto* link = *reinterpret_cast<std::uint8_t**>(root);
    for (int i = 0; link && link != root && i < 10; ++i) {
        auto* option = link - 4;
        auto& vtable = *reinterpret_cast<void***>(option);
        if (vtable == musicNative) {
            vtable = musicTable.data();
            DrawMusicSource(option);
        }
        if (vtable == nativeTable) {
            vtable = table.data();
            DrawOutput(option);
            return;
        }
        link = *reinterpret_cast<std::uint8_t**>(link);
    }

    // The stock pause screen omits its speaker row. Register an equivalent
    // toggle there if the real FEng package has an unused row.
    using FindRow = void* (__thiscall*)(std::uint8_t*, const char*);
    const auto find = GameFunction<FindRow>(0x173cc0);
    const auto* prefixes = reinterpret_cast<const char* const*>(screen + 0xb0);
    if (*reinterpret_cast<int*>(screen + 0x104) >= 10 || !prefixes[0] || !prefixes[1] ||
        !find(screen, prefixes[0]) || !find(screen, prefixes[1])) return;
    using Allocate = void* (__cdecl*)(std::size_t);
    using Construct = void (__thiscall*)(void*, bool);
    using Add = void (__thiscall*)(void*, void*, int);
    auto* option = GameFunction<Allocate>(0x252ad0)(0x5c);
    if (!option) return;
    GameFunction<Construct>(0x11a7b0)(option, true);
    *static_cast<void***>(option) = table.data();
    GameFunction<Add>(0x188570)(screen, option, 1);
}

} // namespace unlimited_audio

bool InstallSurroundAudio() {
    using namespace unlimited_audio;
    struct Hook {
        std::uintptr_t address;
        std::uintptr_t original;
        std::uintptr_t replacement;
    };
    const Hook hooks[] = {
        {0x0b0b2f, 0x41b4ac, reinterpret_cast<std::uintptr_t>(&RouteNativeMixer)},
        {0x0b0b3f, 0x41b4ac, reinterpret_cast<std::uintptr_t>(&RouteNativeMixer)},
        {0x0b0b4b, 0x41b4ac, reinterpret_cast<std::uintptr_t>(&RouteNativeMixer)},
        {0x42088c, 0x42007e, reinterpret_cast<std::uintptr_t>(&CreatePlaybackBuffer)},
        {0x145d83, 0x129310, reinterpret_cast<std::uintptr_t>(&SetupAudio)},
    };
    for (const auto& h : hooks) {
        const auto displacement = h.original - (h.address + 5);
        if (GameValue<std::uint8_t>(h.address) != 0xe8 ||
            !MatchGameBytes(h.address + 1, &displacement, sizeof(displacement))) return false;
    }
    constexpr auto nativeRingFlags = 0x42015f;
    constexpr DWORD foregroundFlags = DSBCAPS_GETCURRENTPOSITION2;
    constexpr DWORD originalFlags = foregroundFlags | DSBCAPS_GLOBALFOCUS;
    if (!MatchGameBytes(nativeRingFlags, &originalFlags, sizeof(originalFlags)) ||
        !WriteGameBytes(nativeRingFlags, &foregroundFlags, sizeof(foregroundFlags))) return false;

    const unsigned saved = settingsPath[0] ? GetPrivateProfileIntW(L"Audio", L"Output", 0, settingsPath) : 0;
    preference = startupPreference = saved < outputCount ? static_cast<Output>(saved) : Output::Automatic;
    endpointChannels = DetectEndpointChannels();
    desiredChannels = SelectChannels(preference, endpointChannels);
    const auto image = reinterpret_cast<std::uintptr_t>(engine);
    for (std::size_t i = 0; i < std::size(hooks); ++i) {
        const auto& h = hooks[i];
        if (RedirectCall(h.address, h.original, h.replacement)) continue;
        while (i--) {
            const auto& rollback = hooks[i];
            RedirectCall(rollback.address, rollback.replacement - image, image + rollback.original);
        }
        WriteGameBytes(nativeRingFlags, &originalFlags, sizeof(originalFlags));
        return false;
    }
    if (LogFile log; log.file) {
        std::fprintf(log.file, "Audio: preference=%u endpoint=%u desired=%u\n", saved, endpointChannels, desiredChannels);
    }
    return true;
}
} // namespace
