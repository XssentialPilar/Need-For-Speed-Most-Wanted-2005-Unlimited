#pragma once

#include <algorithm>
#include <array>
#include <audioclient.h>
#include <audiopolicy.h>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <new>

namespace {
namespace unlimited_audio {

enum class Output { Automatic, Surround51, Surround71, Stereo };
constexpr unsigned outputCount = 4;
Output preference = Output::Automatic;
Output startupPreference = Output::Automatic;
bool matrixEnabled = false;
bool startupMatrixEnabled = false;
bool matrixOutput = false;
bool audioRouteInitialized = false;
wchar_t audioPreferences[MAX_PATH]{};
constexpr int stereoMode = 1, quadMode = 2, surroundMode = 3;
constexpr wchar_t matrixSetting[] = L"UnlimitedMatrix";
unsigned endpointChannels = 0;
unsigned desiredChannels = 0;
constexpr DWORD surroundMask = 0x63f;
constexpr GUID pcmSubtype = {1, 0, 0x0010, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
constexpr GUID bufferIid = {0x279afa85, 0x4981, 0x11ce, {0xa5, 0x21, 0, 0x20, 0xaf, 0x0b, 0xe5, 0x60}};
constexpr GUID unknownIid = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

template<std::size_t N>
bool HasConsoleMusicFile(const wchar_t (&relative)[N], DWORD size) {
    const auto* separator = std::wcsrchr(settingsPath, L'\\');
    if (!separator || separator - settingsPath + N >= MAX_PATH) return false;
    wchar_t path[MAX_PATH]{};
    std::wmemcpy(path, settingsPath, separator - settingsPath + 1);
    std::wcscat(path, relative);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    return GetFileAttributesExW(path, GetFileExInfoStandard, &info) &&
           !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !info.nFileSizeHigh && info.nFileSizeLow == size;
}

bool SelectConsoleMusicBank() {
    if (!settingsPath[0] || !GetPrivateProfileIntW(L"Audio", L"X360Music", 1, settingsPath) ||
        !HasConsoleMusicFile(L"SOUND\\PFDATA\\X3_Music.mpf", 136148) ||
        !HasConsoleMusicFile(L"SOUND\\PFDATA\\X3_Music.mus", 653261696)) return false;
    // Pathfinder requests the two names from this PC 1.3 string table.
    // Change them together so the PC index is never paired with console audio.
    constexpr unsigned filenames = 0x498894;
    constexpr char original[] = "MW_Music.mus\0\0\0\0MW_Music.mpf";
    constexpr char console[] = "X3_Music.mus\0\0\0\0X3_Music.mpf";
    static_assert(sizeof(original) == sizeof(console));
    return MatchGameBytes(filenames, original, sizeof(original)) &&
           WriteGameBytes(filenames, console, sizeof(console));
}

constexpr unsigned SelectChannels(Output output, unsigned available) {
    if (output == Output::Stereo) return 2;
    if (available >= 8 && output != Output::Surround51) return 8;
    if (available >= 6) return 6;
    if (available >= 4 && output == Output::Automatic) return 4;
    return available ? 2 : 0;
}

bool HasGameAudioSession(IMMDevice* endpoint) {
    IAudioSessionManager2* manager = nullptr;
    if (FAILED(endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER,
                                  nullptr, reinterpret_cast<void**>(&manager)))) return false;
    IAudioSessionEnumerator* sessions = nullptr;
    bool matches = false;
    if (SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
        int count = 0;
        sessions->GetCount(&count);
        for (int i = 0; i < count && !matches; ++i) {
            IAudioSessionControl* control = nullptr;
            if (FAILED(sessions->GetSession(i, &control))) continue;
            IAudioSessionControl2* process = nullptr;
            AudioSessionState state{};
            DWORD pid = 0;
            if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&process)))) {
                matches = SUCCEEDED(process->GetProcessId(&pid)) && pid == GetCurrentProcessId() &&
                          SUCCEEDED(process->GetState(&state)) && state != AudioSessionStateExpired;
                process->Release();
            }
            control->Release();
        }
        sessions->Release();
    }
    manager->Release();
    return matches;
}

IMMDevice* FindPlaybackEndpoint(IMMDeviceEnumerator* enumerator) {
    IMMDeviceCollection* devices = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* endpoint = nullptr;
            if (FAILED(devices->Item(i, &endpoint))) continue;
            if (HasGameAudioSession(endpoint)) {
                devices->Release();
                return endpoint;
            }
            endpoint->Release();
        }
        devices->Release();
    }
    IMMDevice* endpoint = nullptr;
    enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint);
    return endpoint;
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
                (endpoint = FindPlaybackEndpoint(enumerator)) &&
                SUCCEEDED(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER,
                                             nullptr, reinterpret_cast<void**>(&audioClient))) &&
                SUCCEEDED(audioClient->GetMixFormat(&format)) && format) {
                channels = format->nChannels;
                if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                    format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
                    const auto mask = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->dwChannelMask;
                    constexpr DWORD frontSurround = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
                        SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY;
                    constexpr DWORD back = SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
                    constexpr DWORD side = SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
                    const bool surround = (mask & back) == back || (mask & side) == side;
                    if (mask && channels >= 6 && ((mask & frontSurround) != frontSurround || !surround))
                        channels = 2;
                    else if (mask && channels >= 8 && (mask & (back | side)) != (back | side))
                        channels = 6;
                    else if (mask && channels == 4 &&
                             (mask & (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | back)) !=
                             (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | back))
                        channels = 2;
                }
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
          presenceBand_(1.0f - std::exp(-6.2831853f * 2500.0f / rate)),
          surroundHighPass_(1.0f - std::exp(-6.2831853f * 100.0f / rate)),
          surroundLowPass_(1.0f - std::exp(-6.2831853f * 7000.0f / rate)),
          phasePole_((1.0f - std::tan(3.1415927f * 700.0f / rate)) /
                     (1.0f + std::tan(3.1415927f * 700.0f / rate))) {}

    void Reset() {
        low_.fill(0.0f);
        presence_.fill(0.0f);
        surroundLow_ = surroundBand_ = previousSurround_ = previousPhase_ = 0;
    }

    void Convert(const std::int16_t* input, std::int16_t* output, std::size_t frames,
                 unsigned sourceChannels, unsigned outputChannels) {
        for (; frames; --frames, input += sourceChannels, output += outputChannels) {
            std::array<std::int16_t, 6> stereoMatrix{};
            const bool matrix = sourceChannels == 6 && outputChannels == 2;
            auto* samples = matrix ? stereoMatrix.data() : output;
            for (unsigned ch = 0; ch < sourceChannels; ++ch) {
                const float x = input[ch];
                if (sourceChannels == 6 && ch == 3) {
                    samples[ch] = input[ch];  // Preserve the native LFE channel.
                    continue;
                }
                low_[ch] += lowBand_ * (x - low_[ch]);
                presence_[ch] += presenceBand_ * (x - presence_[ch]);
                const float shaped = 0.91f * x - 0.07f * low_[ch] + 0.065f * (x - presence_[ch]);
                samples[ch] = static_cast<std::int16_t>(std::clamp(shaped, -32768.0f, 32767.0f));
            }
            if (matrix) {
                // Lt/Rt uses a shared center and opposite surround polarity.
                // The analog Pro Logic surround band is mono and band-limited.
                const float center = stereoMatrix[2] * .70710678f;
                const float surround = (stereoMatrix[4] + stereoMatrix[5]) * .5f;
                surroundLow_ += surroundHighPass_ * (surround - surroundLow_);
                surroundBand_ += surroundLowPass_ * (surround - surroundLow_ - surroundBand_);
                const float phase = previousSurround_ - phasePole_ * (surroundBand_ - previousPhase_);
                previousSurround_ = surroundBand_;
                previousPhase_ = phase;
                const float rear = phase * .70710678f;
                output[0] = static_cast<std::int16_t>(std::clamp(.5f * (stereoMatrix[0] + center - rear), -32768.0f, 32767.0f));
                output[1] = static_cast<std::int16_t>(std::clamp(.5f * (stereoMatrix[1] + center + rear), -32768.0f, 32767.0f));
            } else if (sourceChannels == 6 && outputChannels == 8) {
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
    float lowBand_, presenceBand_, surroundHighPass_, surroundLowPass_, phasePole_;
    float surroundLow_ = 0, surroundBand_ = 0, previousSurround_ = 0, previousPhase_ = 0;
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
        // Both DirectSound regions share one contiguous staging buffer.
        unlimited_discx::MixNative(pcm_, (firstBytes + secondBytes) / sourceStride, sourceChannels_, rate_);
        tone_.Convert(pcm_, static_cast<std::int16_t*>(firstTarget_), firstBytes / sourceStride, sourceChannels_, channels_);
        if (secondBytes) {
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
    // Let the native call initialize DirectSound before checking for a game session.
    const int stockResult = setMode(nativeMode);
    if (!audioRouteInitialized) {
        endpointChannels = startupPreference == Output::Stereo ? 0 : DetectEndpointChannels();
        desiredChannels = SelectChannels(startupPreference, endpointChannels);
        audioRouteInitialized = true;
    }
    // Unlimited Matrix takes the native 5.1 mix and encodes it as Lt/Rt.
    matrixOutput = startupPreference == Output::Stereo && startupMatrixEnabled &&
                   setMode(surroundMode) >= 0;
    if (matrixOutput) return 0;
    const int selected = desiredChannels >= 6 ? surroundMode : desiredChannels == 4 ? quadMode :
                         desiredChannels == 2 ? stereoMode : nativeMode;
    if (selected != nativeMode && setMode(selected) >= 0) return 0;
    return stockResult;
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
    if (!original || (channels != 2 && channels != 4 && channels != 6) ||
        rate <= 0 || !ringBytes || ringBytes % inputStride) {
        unlimited_discx::SetNativeMixer(0);
        return result;
    }

    unsigned playbackChannels = channels;
    IDirectSoundBuffer* backing = original;
    const bool encode = matrixOutput && channels == 6;
    if (channels == 6 && (encode || desiredChannels == 8)) {
        auto format = ExpandedFormat(rate);
        WAVEFORMATEX stereo{WAVE_FORMAT_PCM, 2, static_cast<DWORD>(rate), static_cast<DWORD>(rate) * 4, 4, 16, 0};
        auto* outputFormat = encode ? &stereo : &format.Format;
        DSBUFFERDESC desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_LOCSOFTWARE | DSBCAPS_GETCURRENTPOSITION2;
        desc.dwBufferBytes = (ringBytes / inputStride) * outputFormat->nBlockAlign;
        desc.lpwfxFormat = outputFormat;
        IDirectSoundBuffer* expanded = nullptr;
        if (SUCCEEDED(GameValue<IDirectSound*>(0x5c22a8)->CreateSoundBuffer(&desc, &expanded, nullptr)) && expanded) {
            backing = expanded;
            playbackChannels = outputFormat->nChannels;
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
        std::fprintf(log.file, "Audio: endpoint=%u desired=%u engine_mix=%i playback=%u tone=%u matrix=%u\n",
                     endpointChannels, desiredChannels, channels, playbackChannels, unsigned(processed),
                     unsigned(encode && playbackChannels == 2));
    }
    return result;
}

struct AudioLabels {
    const char* output;
    const char* automatic;
    const char* stereo;
    const char* restart;
};

AudioLabels AudioText(int language) {
    switch (language) {
    case 1: return {"Sortie audio", "Automatique", u8"Stéréo", u8"redémarrer"};
    case 2: return {"Audioausgabe", "Automatisch", "Stereo", "Neustart"};
    case 3: return {"Uscita audio", "Automatica", "Stereo", "riavvio"};
    case 4: return {"Salida de audio", u8"Automático", u8"Estéreo", "reiniciar"};
    case 5: return {"Audio-uitvoer", "Automatisch", "Stereo", "herstart"};
    case 6: return {u8"Ljudutgång", "Automatisk", "Stereo", "omstart"};
    case 7: return {"Lydudgang", "Automatisk", "Stereo", "genstart"};
    case 8: return {u8"오디오 출력", u8"자동", u8"스테레오", u8"재시작"};
    case 9: return {u8"音訊輸出", u8"自動", u8"立體聲", u8"重新啟動"};
    case 10: return {u8"音声出力", u8"自動", u8"ステレオ", u8"再起動"};
    case 11: return {u8"เอาต์พุตเสียง", u8"อัตโนมัติ", u8"สเตอริโอ", u8"เริ่มใหม่"};
    case 12: return {u8"Wyjście audio", "Automatyczne", "Stereo", "restart"};
    case 13: return {u8"Äänilähtö", "Automaattinen", "Stereo", u8"käynnistä uudelleen"};
    default: return {"Audio output", "Automatic", "Stereo", "restart"};
    }
}

void DrawAudioSetting(std::uint8_t* option, const char* label, const char* value, bool pending) {
    char text[96]{};
    if (pending) {
        const auto locale = AudioText(GameValue<int>(0x4f41c0));
        std::snprintf(text, sizeof(text), "%s (%s)", value, locale.restart);
        value = text;
    }
    SetOptionText(option, label, value);
}

void __fastcall DrawOutput(std::uint8_t* option) {
    const auto locale = AudioText(GameValue<int>(0x4f41c0));
    const char* values[]{locale.automatic, "5.1", "7.1", locale.stereo};
    DrawAudioSetting(option, locale.output, values[static_cast<unsigned>(preference)], preference != startupPreference);
}

void __fastcall SetupAudio(std::uint8_t* screen, void*);

void RefreshStereoOptions(const char* package, std::uint8_t* option) {
    if (!package) return;
    using FindScreen = std::uint8_t* (__cdecl*)(const char*);
    auto* screen = GameFunction<FindScreen>(0x191e90)(package);
    if (!screen || HudField<std::uint8_t*>(screen, 0x34) != option) return;
    const int selected = HudField<int>(screen, 0x108);
    auto*& backup = HudField<void*>(screen, 0x130);
    auto* before = backup;
    GameFunction<void (__thiscall*)(void*)>(0x188c90)(screen);
    SetupAudio(screen, nullptr);
    if (before && backup != before) {
        if (backup) GameFunction<void (__cdecl*)(void*)>(0x252b10)(backup);
        backup = before;
    }
    GameFunction<void (__thiscall*)(void*, int)>(0x1a5ea0)(screen, selected);
}

const wchar_t* AudioSettingsFile() {
    return audioPreferences[0] ? audioPreferences : settingsPath;
}

unsigned ReadAudioSetting(const wchar_t* key, const wchar_t* legacyKey = nullptr) {
    const auto read = [key, legacyKey](const wchar_t* file, unsigned fallback) {
        if (!file[0]) return fallback;
        // A user's old preference takes precedence over any installed default.
        if (legacyKey) fallback = GetPrivateProfileIntW(L"Audio", legacyKey, fallback, file);
        return GetPrivateProfileIntW(L"Audio", key, fallback, file);
    };
    return read(audioPreferences, read(settingsPath, 0));
}

void __fastcall ChangeOutput(std::uint8_t* option, void*, const char* package, std::int32_t message) {
    constexpr std::int32_t left = -1860157282, right = -1248388111;
    if ((message != left && message != right) || !settingsPath[0]) return;
    const unsigned next = (static_cast<unsigned>(preference) + (message == left ? outputCount - 1 : 1)) % outputCount;
    const wchar_t value[] = {static_cast<wchar_t>(L'0' + next), L'\0'};
    if (WritePrivateProfileStringW(L"Audio", L"Output", value, AudioSettingsFile())) {
        const bool wasStereo = preference == Output::Stereo;
        preference = static_cast<Output>(next);
        option[0x2a] = 1;
        DrawOutput(option);
        if (wasStereo != (preference == Output::Stereo)) RefreshStereoOptions(package, option);
    }
}

void __fastcall DrawUnlimitedMatrix(std::uint8_t* option) {
    const int language = GameValue<int>(0x4f41c0);
    const auto labels = VideoEffectsText(language);
    DrawAudioSetting(option, "Unlimited Matrix", matrixEnabled ? labels.on : labels.off,
                     startupPreference != Output::Stereo || matrixEnabled != startupMatrixEnabled);
}

void __fastcall ChangeUnlimitedMatrix(std::uint8_t* option, void*, const char*, std::int32_t message) {
    if (!VideoOptionDirection(message) || preference != Output::Stereo || !settingsPath[0]) return;
    if (!WritePrivateProfileStringW(L"Audio", matrixSetting, matrixEnabled ? L"0" : L"1", AudioSettingsFile())) return;
    matrixEnabled = !matrixEnabled;
    option[0x2a] = 1;
    DrawUnlimitedMatrix(option);
}

// Use the stock switch's input, widget placement and localized Yes/No labels.
void __fastcall DrawMusicSource(std::uint8_t* option) {
    const auto draw = GameFunction<void (__thiscall*)(void*)>(0x11b060);
    auto* database = GameValue<std::uint8_t*>(0x51cf90);
    auto* profile = database ? *reinterpret_cast<std::uint8_t**>(database + 0x10) : nullptr;
    if (profile && unlimited_discx::Selected()) {
        auto& traxMode = *reinterpret_cast<int*>(profile + 0x78);
        const int saved = traxMode;
        traxMode = unlimited_discx::Enabled();
        draw(option);
        traxMode = saved;
    } else {
        draw(option);
        return;
    }

    if (auto* title = *reinterpret_cast<std::uint8_t**>(option + 0x2c)) {
        GameFunction<void (__thiscall*)(void*, const char*)>(0x1bcc10)(title, "DISCX");
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

    bool hasOutput = false;
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
            hasOutput = true;
        }
        link = *reinterpret_cast<std::uint8_t**>(link);
    }

    // The stock pause screen omits its speaker row. Reuse its authored
    // toggle bindings for the output row and the Stereo-only sub-option.
    static std::array<void*, 15> outputTable{}, matrixTable{};
    if (!hasOutput) {
        auto* output = AddUnlimitedVideoOption(screen, outputTable,
            &ChangeOutput, &DrawOutput);
        hasOutput = output != nullptr;
    }
    if (hasOutput && preference == Output::Stereo)
        AddUnlimitedVideoOption(screen, matrixTable, &ChangeUnlimitedMatrix, &DrawUnlimitedMatrix);
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

    unlimited_settings::UserFile(audioPreferences, L"Audio");
    const unsigned saved = ReadAudioSetting(L"Output");
    preference = startupPreference = saved < outputCount ? static_cast<Output>(saved) : Output::Automatic;
    startupMatrixEnabled = matrixEnabled = ReadAudioSetting(matrixSetting, L"ProLogic") != 0;
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
        std::fprintf(log.file, "Audio: preference=%u unlimited_matrix=%u\n", saved, unsigned(startupMatrixEnabled));
    }
    return true;
}
} // namespace
