#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <random>
#include <thread>

#include "nfs_discx.hpp"

namespace unlimited_discx {
namespace {

namespace fs = std::filesystem;

fs::path GameFolder() {
    std::wstring file(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, file.data(), static_cast<DWORD>(file.size()));
    if (!count || count >= file.size()) return {};
    file.resize(count);
    return fs::path(file).parent_path();
}

template<class Proc>
bool LoadProc(HMODULE module, Proc& proc, const char* name) {
    proc = reinterpret_cast<Proc>(GetProcAddress(module, name));
    return proc != nullptr;
}

struct Decoder {
    HMODULE module = nullptr;
    discx_codec::Open open = nullptr;
    discx_codec::OpenForMixer openMixer = nullptr;
    discx_codec::Artwork artwork = nullptr;
    discx_codec::Read read = nullptr;
    discx_codec::Seek seek = nullptr;
    discx_codec::Close close = nullptr;
    bool ready = false;

    explicit Decoder(const fs::path& folder) {
        if (folder.empty()) return;
        const auto dll = folder / L"UnlimitedCommunity" / L"Discx" / L"UnlimitedDiscx.dll";
        module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        ready = module && LoadProc(module, open, "DiscCX_Open") && LoadProc(module, read, "DiscCX_Read") &&
            LoadProc(module, seek, "DiscCX_Seek") && LoadProc(module, close, "DiscCX_Close");
        if (module) {
            LoadProc(module, openMixer, "DiscCX_OpenForMixer");
            LoadProc(module, artwork, "DiscCX_Artwork");
        }
    }
};

template<class T>
struct ComPtr {
    T* value = nullptr;
    ~ComPtr() { if (value) value->Release(); }
    T* operator->() const { return value; }
};

std::shared_ptr<const Cover> ReadCover(const Decoder& decoder, const discx_codec::Stream* stream) {
    unsigned bytes = 0;
    const auto* data = decoder.artwork ? decoder.artwork(stream, &bytes) : nullptr;
    if (!data || !bytes) return {};
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment)) return {};
    struct Apartment { ~Apartment() { CoUninitialize(); } } scope;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> encoded;
    ComPtr<IWICBitmapDecoder> image;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapClipper> crop;
    ComPtr<IWICBitmapScaler> scale;
    ComPtr<IWICFormatConverter> converter;
    UINT width = 0, height = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory.value))) ||
        FAILED(factory->CreateStream(&encoded.value)) ||
        FAILED(encoded->InitializeFromMemory(const_cast<BYTE*>(data), bytes)) ||
        FAILED(factory->CreateDecoderFromStream(encoded.value, nullptr, WICDecodeMetadataCacheOnDemand, &image.value)) ||
        FAILED(image->GetFrame(0, &frame.value)) || FAILED(frame->GetSize(&width, &height)) ||
        !width || !height || width > 8192 || height > 8192 ||
        static_cast<std::uint64_t>(width) * height > 32 * 1024 * 1024) return {};
    const auto edge = static_cast<INT>(std::min(width, height));
    const WICRect area{(static_cast<INT>(width) - edge) / 2, (static_cast<INT>(height) - edge) / 2, edge, edge};
    if (FAILED(factory->CreateBitmapClipper(&crop.value)) || FAILED(crop->Initialize(frame.value, &area)) ||
        FAILED(factory->CreateBitmapScaler(&scale.value)) ||
        FAILED(scale->Initialize(crop.value, coverEdge, coverEdge, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&converter.value)) ||
        FAILED(converter->Initialize(scale.value, GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    auto result = std::make_shared<Cover>();
    if (FAILED(converter->CopyPixels(nullptr, coverEdge * 4, sizeof(Cover),
                                     reinterpret_cast<BYTE*>(result->data())))) return {};
    constexpr float center = coverEdge * .5f;
    for (unsigned y = 0; y < coverEdge; ++y) for (unsigned x = 0; x < coverEdge; ++x) {
        const float dx = x + .5f - center, dy = y + .5f - center;
        const auto coverage = std::clamp(center - .5f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
        auto& pixel = (*result)[y * coverEdge + x];
        pixel = (pixel & 0xffffff) | (static_cast<std::uint32_t>((pixel >> 24) * coverage + .5f) << 24);
    }
    return result;
}

enum class Action { Play, Shuffle, Repeat, Previous, Next, Pause, Stop, Seek };
enum class MusicSource { Trax, DiscxDisabled, DiscxEnabled };
std::atomic<MusicSource> musicSource{MusicSource::Trax};
std::atomic<unsigned> gameMix{0};
std::atomic<float> previewGain{-1.f};
std::atomic<unsigned> mixerRate{0};
std::atomic<bool> outputActive{false};

struct Request {
    Action action;
    double value = 0;
    std::shared_ptr<const Tracks> library;
    std::vector<size_t> order;
};

bool SupportedFile(const fs::path& path) {
    constexpr const wchar_t* supported[] = {
        L".flac", L".wav", L".wave", L".pcm", L".mp3", L".opus", L".ogg", L".oga",
        L".m4a", L".mp4", L".aac", L".aif", L".aiff", L".ape", L".alac", L".au", L".wma", L".mka"
    };
    const auto suffix = path.extension().wstring();
    return std::any_of(std::begin(supported), std::end(supported),
        [&](const wchar_t* ext) { return _wcsicmp(ext, suffix.c_str()) == 0; });
}

class Player {
public:
    std::atomic<bool> musicActive{false};
    std::atomic<std::uint64_t> trackSequence{0};

    Player() : folder(GameFolder()) {}

    Catalog GetCatalog() {
        std::lock_guard<std::mutex> guard(lock);
        return catalog;
    }

    Playback GetPlayback() {
        std::lock_guard<std::mutex> guard(lock);
        return status;
    }

    void Mix(std::int16_t* samples, std::size_t frames, unsigned channels, unsigned rate) {
        std::unique_lock<std::mutex> guard(musicLock, std::try_to_lock);
        if (!guard || !outputActive.load(std::memory_order_acquire) || musicPaused || queueRate != rate || !Enabled()) return;
        const auto preview = previewGain.load(std::memory_order_relaxed);
        const auto mix = gameMix.load(std::memory_order_relaxed);
        const auto gain = (preview >= 0.f ? preview : (mix & 0xffff) / 100.f) * 32768.f;
        if (!(gain > 0.f)) return;
        const unsigned cutoff = preview >= 0.f ? 0 : mix >> 16;
        const bool filtering = cutoff != 0 && cutoff < rate / 2;
        if (filtering && cutoff != filterCutoff) {
            const float k = std::tan(3.14159265f * cutoff / rate);
            constexpr float rootTwo = 1.41421356f;
            const float norm = 1.f / (1.f + rootTwo * k + k * k);
            filterB0 = k * k * norm;
            filterA1 = 2.f * (k * k - 1.f) * norm;
            filterA2 = (1.f - rootTwo * k + k * k) * norm;
            filterCutoff = cutoff;
        }
        const auto mixed = std::min(frames, available);
        for (std::size_t frame = 0; frame < mixed; ++frame) {
            for (unsigned channel = 0; channel < 2; ++channel) {
                const float input = pcm[consumed * 2 + channel];
                auto& history = filterHistory[channel];
                const float output = filtering
                    ? filterB0 * (input + 2.f * history[0] + history[1])
                        - filterA1 * history[2] - filterA2 * history[3]
                    : input;
                history = {input, history[0], output, history[2]};
                const auto value = samples[channel] + output * gain;
                samples[channel] = static_cast<std::int16_t>(std::clamp(value, -32768.f, 32767.f));
            }
            consumed = (consumed + 1) % musicFrames;
            samples += channels;
        }
        available -= mixed;
    }

    void Scan() {
        {
            std::lock_guard<std::mutex> guard(lock);
            if (catalog.scanning) return;
            catalog.scanning = true;
        }
        try {
            std::thread([this] { ReadCatalog(); }).detach();
        } catch (...) {
            std::lock_guard<std::mutex> guard(lock);
            catalog.scanning = false;
        }
    }

    void Send(Request request) {
        std::lock_guard<std::mutex> guard(lock);
        if (!workerStarted) {
            try {
                std::thread([this] { Run(); }).detach();
                workerStarted = true;
            } catch (...) {
                status.failed = true;
                return;
            }
        }
        pending.push_back(std::move(request));
        wake.notify_one();
    }

private:
    fs::path folder;
    std::once_flag decoderInit;
    std::unique_ptr<Decoder> decoder;
    std::mutex lock;
    std::condition_variable wake;
    bool workerStarted = false;
    Catalog catalog;
    Playback status;
    std::vector<Request> pending;
    static constexpr std::size_t musicFrames = 32768;
    std::mutex musicLock;
    std::array<float, musicFrames * 2> pcm{};
    std::size_t consumed = 0, available = 0;
    unsigned queueRate = 0;
    bool musicPaused = true;
    unsigned filterCutoff = 0;
    float filterB0 = 0.f, filterA1 = 0.f, filterA2 = 0.f;
    std::array<std::array<float, 4>, 2> filterHistory{};

    void ResetMusic(unsigned rate, bool paused) {
        std::lock_guard<std::mutex> guard(musicLock);
        consumed = available = 0;
        queueRate = rate;
        musicPaused = paused;
        filterHistory = {};
        filterCutoff = 0;
    }

    void PauseMusic(bool paused) {
        std::lock_guard<std::mutex> guard(musicLock);
        musicPaused = paused;
    }

    std::size_t Queued() {
        std::lock_guard<std::mutex> guard(musicLock);
        return available;
    }

    void Enqueue(const float* samples, std::size_t frames) {
        std::lock_guard<std::mutex> guard(musicLock);
        const auto offset = (consumed + available) % musicFrames;
        const auto first = std::min(frames, musicFrames - offset);
        std::copy_n(samples, first * 2, pcm.begin() + offset * 2);
        std::copy_n(samples + first * 2, (frames - first) * 2, pcm.begin());
        available += frames;
    }

    Decoder& Codec() {
        std::call_once(decoderInit, [this] { decoder = std::make_unique<Decoder>(folder); });
        return *decoder;
    }

    void ReadCatalog() {
        try {
            auto& decoder = Codec();
            auto list = std::make_shared<Tracks>();
            if (!decoder.ready) {
                std::lock_guard<std::mutex> guard(lock);
                catalog = {list, 0, false, false};
                return;
            }
            unsigned errors = 0;
            std::error_code error;
            const auto source = folder / L"UnlimitedMusic";
            fs::recursive_directory_iterator item(source, fs::directory_options::skip_permission_denied, error), end;
            for (; !error && item != end && list->size() < 20000; item.increment(error)) {
                const auto& entry = *item;
                const DWORD attributes = GetFileAttributesW(entry.path().c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) || item.depth() >= 12) {
                    item.disable_recursion_pending();
                    continue;
                }
                if ((attributes & FILE_ATTRIBUTE_DIRECTORY) || !SupportedFile(entry.path())) continue;
                Track track;
                track.path = entry.path();
                auto* probe = decoder.open(track.path.c_str(), &track.metadata, true);
                if (!probe) { ++errors; continue; }
                decoder.close(probe);
                if (!track.metadata.title[0]) {
                    const auto name = track.path.stem().wstring();
                    std::wcsncpy(track.metadata.title, name.c_str(), std::size(track.metadata.title) - 1);
                }
                list->push_back(std::move(track));
            }
            std::lock_guard<std::mutex> guard(lock);
            catalog = {list, errors, false, decoder.ready};
        } catch (...) {
            std::lock_guard<std::mutex> guard(lock);
            catalog.scanning = false;
            ++catalog.errors;
        }
    }

    void SetStatus(Playback next) {
        const auto sequence = next.sequence;
        std::lock_guard<std::mutex> guard(lock);
        status = std::move(next);
        trackSequence.store(sequence, std::memory_order_release);
    }

    void Run() {
        auto& decoder = Codec();
        discx_codec::Stream* stream = nullptr;
        std::shared_ptr<const Tracks> library;
        std::vector<size_t> authored, queue;
        size_t current = 0;
        Playback currentStatus;
        std::uint64_t sequence = 0;
        bool paused = false, exhausted = false, shuffle = false;
        Repeat repeat = Repeat::All;
        double base = 0.;
        int64_t fed = 0;
        unsigned rate = 0;
        std::mt19937 random(GetTickCount() ^ GetCurrentProcessId());
        std::array<float, 8192> buffer{};

        auto close = [&](bool failed) {
            musicActive.store(false, std::memory_order_release);
            ResetMusic(0, true);
            if (stream) decoder.close(stream);
            stream = nullptr;
            paused = exhausted = false;
            currentStatus = {};
            currentStatus.failed = failed;
        };

        auto fill = [&]() {
            if (!stream || paused || exhausted) return true;
            const auto target = std::min<std::size_t>(musicFrames - buffer.size() / 2,
                std::max<unsigned>(8192, rate / 6));
            while (Queued() < target) {
                const int frames = decoder.read(stream, buffer.data(), static_cast<int>(buffer.size() / 2));
                if (frames < 0) return false;
                if (frames == 0) {
                    exhausted = true;
                    break;
                }
                fed += frames;
                Enqueue(buffer.data(), frames);
            }
            return true;
        };

        auto start = [&]() {
            rate = mixerRate.load(std::memory_order_acquire);
            if (!decoder.ready || !decoder.openMixer || !rate || !library || queue.empty() || current >= queue.size() ||
                queue[current] >= library->size()) { close(true); return; }
            const auto& track = (*library)[queue[current]];
            discx_codec::Metadata metadata;
            if (stream) decoder.close(stream);
            ResetMusic(rate, true);
            stream = decoder.openMixer(track.path.c_str(), &metadata, rate);
            if (!stream) { close(true); return; }
            paused = exhausted = false;
            fed = 0;
            base = 0.;
            if (!fill() || !fed) { close(true); return; }
            std::shared_ptr<const Cover> cover;
            try { cover = ReadCover(decoder, stream); } catch (...) {}
            musicActive.store(true, std::memory_order_release);
            PauseMusic(false);
            currentStatus = { track.metadata.title, track.metadata.artist, track.metadata.album, track.path,
                              0., metadata.duration, true, false, false, ++sequence, std::move(cover) };
        };

        auto rebuild = [&](size_t selected) {
            queue = authored;
            if (shuffle) std::shuffle(queue.begin(), queue.end(), random);
            const auto found = std::find(queue.begin(), queue.end(), selected);
            current = found == queue.end() ? 0 : static_cast<size_t>(found - queue.begin());
            if (shuffle && found != queue.end()) {
                std::swap(queue[0], queue[current]);
                current = 0;
            }
        };

        auto next = [&](bool automatic) {
            if (queue.empty()) return;
            if (automatic && repeat == Repeat::One) { start(); return; }
            if (current + 1 < queue.size()) { ++current; start(); return; }
            if (!automatic || repeat == Repeat::All) {
                if (shuffle && queue.size() > 1) {
                    const auto old = queue[current];
                    std::shuffle(queue.begin(), queue.end(), random);
                    if (queue[0] == old) std::swap(queue[0], queue[1]);
                }
                current = 0;
                start();
            } else close(false);
        };

        for (;;) {
            std::vector<Request> requests;
            {
                std::unique_lock<std::mutex> guard(lock);
                if (stream && !paused) wake.wait_for(guard, std::chrono::milliseconds(8), [&] { return !pending.empty(); });
                else wake.wait(guard, [&] { return !pending.empty(); });
                requests.swap(pending);
            }
            for (auto& request : requests) {
                switch (request.action) {
                case Action::Play:
                    library = std::move(request.library);
                    authored = std::move(request.order);
                    if (authored.empty()) { close(false); break; }
                    rebuild(static_cast<size_t>(request.value));
                    start();
                    break;
                case Action::Shuffle:
                    if (const auto wanted = request.value != 0; shuffle != wanted) {
                        const size_t active = queue.empty() ? size_t(-1) : queue[current];
                        shuffle = wanted;
                        rebuild(active);
                    }
                    break;
                case Action::Repeat: repeat = static_cast<Repeat>(int(request.value)); break;
                case Action::Previous:
                    if (!queue.empty()) { current = current == 0 ? queue.size() - 1 : current - 1; start(); }
                    break;
                case Action::Next: next(false); break;
                case Action::Pause:
                    if (stream) PauseMusic(paused = !paused);
                    break;
                case Action::Stop: close(false); break;
                case Action::Seek:
                    if (stream && std::isfinite(request.value) && request.value >= 0 &&
                        decoder.seek(stream, request.value)) {
                        ResetMusic(rate, paused);
                        base = request.value;
                        fed = 0;
                        exhausted = false;
                    }
                    break;
                }
            }
            if (stream && (mixerRate.load(std::memory_order_acquire) != rate || !fill())) close(true);
            if (stream) {
                const auto queued = Queued();
                if (exhausted && !paused && queued == 0) next(true);
                else {
                    currentStatus.paused = paused;
                    currentStatus.position = base + std::max(0., (fed - static_cast<double>(queued)) / rate);
                }
            }
            SetStatus(currentStatus);
        }
    }
};

// The audio threads and decoders have process lifetime. Avoid joining a
// loader-dependent thread while the game DLLs are being detached.
std::atomic<Player*> service{nullptr};
Player& GetPlayer() {
    static auto* instance = [] {
        auto* player = new Player;
        service.store(player, std::memory_order_release);
        return player;
    }();
    return *instance;
}

void Send(Action action, double value = 0) {
    if (auto* player = service.load(std::memory_order_acquire)) player->Send({action, value, {}, {}});
}

}

void StartScan() { GetPlayer().Scan(); }
Catalog Library() {
    if (auto* player = service.load(std::memory_order_acquire)) return player->GetCatalog();
    return {};
}
Playback NowPlaying() {
    if (auto* player = service.load(std::memory_order_acquire)) return player->GetPlayback();
    return {};
}
bool MusicActive() {
    const auto* player = service.load(std::memory_order_acquire);
    return player && player->musicActive.load(std::memory_order_acquire);
}
bool Selected() {
    return musicSource.load(std::memory_order_acquire) != MusicSource::Trax;
}
bool Enabled() {
    return musicSource.load(std::memory_order_acquire) == MusicSource::DiscxEnabled;
}
bool PlayingInGameMix() {
    return Enabled() && outputActive.load(std::memory_order_relaxed) &&
           previewGain.load(std::memory_order_relaxed) < 0.f &&
           (gameMix.load(std::memory_order_relaxed) & 0xffff) != 0 && MusicActive();
}
void SetEnabled(bool enabled) {
    auto was = musicSource.load(std::memory_order_acquire);
    if (was != MusicSource::Trax)
        musicSource.compare_exchange_strong(was, enabled ? MusicSource::DiscxEnabled : MusicSource::DiscxDisabled);
}
void SelectStockMusic() {
    if (musicSource.exchange(MusicSource::Trax) != MusicSource::Trax) {
        SetGameMix(0, 0);
        Stop();
    }
}
std::uint64_t TrackSequence() {
    const auto* player = service.load(std::memory_order_acquire);
    return player ? player->trackSequence.load(std::memory_order_acquire) : 0;
}
void SetGameMix(unsigned volumePercent, unsigned cutoffHz) {
    gameMix.store((std::min(cutoffHz, 0xffffu) << 16) | std::min(volumePercent, 100u), std::memory_order_relaxed);
}
void SetPreviewGain(float gain) {
    previewGain.store(gain, std::memory_order_relaxed);
}
void SetNativeMixer(unsigned rate) { mixerRate.store(rate, std::memory_order_release); }
void SetOutputActive(bool active) { outputActive.store(active, std::memory_order_release); }
void MixNative(std::int16_t* interleaved, std::size_t frames, unsigned channels, unsigned rate) {
    if (channels < 2 || !frames) return;
    if (auto* player = service.load(std::memory_order_acquire);
        player && player->musicActive.load(std::memory_order_acquire))
        player->Mix(interleaved, frames, channels, rate);
}
void Play(std::shared_ptr<const Tracks> library, std::vector<size_t> order, size_t index) {
    if (!library || index >= library->size() || order.empty() || !mixerRate.load(std::memory_order_acquire)) return;
    musicSource.store(MusicSource::DiscxEnabled, std::memory_order_release);
    GetPlayer().Send({Action::Play, double(index), std::move(library), std::move(order)});
}
void SetShuffle(bool enabled) { Send(Action::Shuffle, enabled); }
void SetRepeat(Repeat mode) { Send(Action::Repeat, static_cast<int>(mode)); }
void Previous() { Send(Action::Previous); }
void Next() { Send(Action::Next); }
void Pause() { Send(Action::Pause); }
void Stop() { Send(Action::Stop); }
void Seek(double seconds) { Send(Action::Seek, seconds); }

}
