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
#include <deque>
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
        module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module && GetLastError() == ERROR_INVALID_PARAMETER)
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
        // Only a transport reset can contend here. The decoder publishes PCM
        // through the ring cursors without taking the mixer's lock.
        std::unique_lock<std::mutex> guard(transportLock, std::try_to_lock);
        if (!guard || !outputActive.load(std::memory_order_acquire) ||
            musicPaused.load(std::memory_order_acquire) || queueRate != rate || !Enabled()) return;
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
        auto cursor = readFrame.load(std::memory_order_relaxed);
        const auto ready = writeFrame.load(std::memory_order_acquire) - cursor;
        const auto mixed = std::min<std::size_t>(frames, ready);
        for (std::size_t frame = 0; frame < mixed; ++frame) {
            const auto offset = (cursor & (musicFrames - 1)) * 2;
            for (unsigned channel = 0; channel < 2; ++channel) {
                const float input = pcm[offset + channel];
                auto& history = filterHistory[channel];
                const float output = filtering
                    ? filterB0 * (input + 2.f * history[0] + history[1])
                        - filterA1 * history[2] - filterA2 * history[3]
                    : input;
                history = {input, history[0], output, history[2]};
                const auto value = samples[channel] + output * gain;
                samples[channel] = static_cast<std::int16_t>(std::clamp(value, -32768.f, 32767.f));
            }
            ++cursor;
            samples += channels;
        }
        readFrame.store(cursor, std::memory_order_release);
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
    static constexpr unsigned musicFrames = 131072;
    std::mutex transportLock;
    std::array<float, musicFrames * 2> pcm{};
    std::atomic<std::uint32_t> readFrame{0}, writeFrame{0};
    unsigned queueRate = 0;
    std::atomic<bool> musicPaused{true};
    unsigned filterCutoff = 0;
    float filterB0 = 0.f, filterA1 = 0.f, filterA2 = 0.f;
    std::array<std::array<float, 4>, 2> filterHistory{};

    void ResetMusic(unsigned rate, bool paused) {
        std::lock_guard<std::mutex> guard(transportLock);
        readFrame.store(0, std::memory_order_relaxed);
        writeFrame.store(0, std::memory_order_relaxed);
        queueRate = rate;
        musicPaused.store(paused, std::memory_order_release);
        filterHistory = {};
        filterCutoff = 0;
    }

    unsigned Queued() const {
        return writeFrame.load(std::memory_order_relaxed) - readFrame.load(std::memory_order_acquire);
    }

    void Enqueue(const float* samples, unsigned frames) {
        const auto cursor = writeFrame.load(std::memory_order_relaxed);
        const auto offset = cursor & (musicFrames - 1);
        const auto first = std::min(frames, musicFrames - offset);
        std::copy_n(samples, first * 2, pcm.begin() + offset * 2);
        std::copy_n(samples + first * 2, (frames - first) * 2, pcm.begin());
        writeFrame.store(cursor + frames, std::memory_order_release);
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
        struct BufferedTrack {
            std::uint32_t start;
            size_t index;
            Playback status;
        };
        std::deque<BufferedTrack> timeline;
        std::uint64_t sequence = 0;
        bool paused = false, exhausted = false, finished = false, failed = false, shuffle = false;
        Repeat repeat = Repeat::All;
        unsigned rate = 0;
        std::mt19937 random(GetTickCount() ^ GetCurrentProcessId());
        std::array<float, 8192> buffer{};

        auto close = [&](bool error) {
            musicActive.store(false, std::memory_order_release);
            ResetMusic(0, true);
            if (stream) decoder.close(stream);
            stream = nullptr;
            timeline.clear();
            paused = exhausted = finished = failed = false;
            Playback stopped;
            stopped.failed = error;
            SetStatus(std::move(stopped));
        };

        constexpr unsigned decodeBlock = std::tuple_size<decltype(buffer)>::value / 2;
        auto reserve = [&] { return std::min(musicFrames - decodeBlock, rate + rate / 2); };
        auto fill = [&](unsigned target) {
            for (unsigned ready; !exhausted && (ready = Queued()) < target;) {
                const int frames = decoder.read(stream, buffer.data(), std::min(decodeBlock, musicFrames - ready));
                if (frames < 0) return false;
                if (frames == 0) {
                    exhausted = true;
                    break;
                }
                Enqueue(buffer.data(), frames);
            }
            return true;
        };

        auto start = [&](bool append, double position = -1.) {
            const auto outputRate = mixerRate.load(std::memory_order_acquire);
            if (!decoder.ready || !decoder.openMixer || !outputRate || (append && outputRate != rate) ||
                !library || current >= queue.size() || queue[current] >= library->size()) return false;
            const auto index = queue[current];
            const auto& track = (*library)[index];
            discx_codec::Metadata metadata;
            auto* opened = decoder.openMixer(track.path.c_str(), &metadata, outputRate);
            if (!opened) return false;
            if (position > 0. && !decoder.seek(opened, position)) {
                decoder.close(opened);
                return false;
            }
            if (stream) decoder.close(stream);
            stream = opened;
            if (!append) {
                rate = outputRate;
                paused = position >= 0. && paused;
                ResetMusic(rate, true);
                timeline.clear();
            }
            exhausted = finished = failed = false;
            const auto first = writeFrame.load(std::memory_order_relaxed);
            if (!fill(append ? reserve() : rate / 4) ||
                first == writeFrame.load(std::memory_order_relaxed)) return false;
            std::shared_ptr<const Cover> cover;
            try { cover = ReadCover(decoder, stream); } catch (...) {}
            timeline.push_back({first, index, {track.metadata.title, track.metadata.artist, track.metadata.album, track.path,
                std::max(0., position), metadata.duration, true, paused, false, ++sequence, std::move(cover)}});
            musicActive.store(true, std::memory_order_release);
            musicPaused.store(paused, std::memory_order_release);
            return true;
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

        auto positionOf = [&](size_t index) {
            const auto found = std::find(queue.begin(), queue.end(), index);
            return found == queue.end() ? size_t{} : static_cast<size_t>(found - queue.begin());
        };
        auto advanceTimeline = [&] {
            const auto frame = readFrame.load(std::memory_order_acquire);
            while (timeline.size() > 1 && frame - timeline[1].start < 0x80000000u) timeline.pop_front();
            return frame;
        };
        auto open = [&](bool append) {
            if (start(append)) return;
            if (append) {
                finished = failed = true;
            } else {
                close(true);
            }
        };
        auto next = [&](bool automatic) {
            if (queue.empty()) return;
            if (!automatic && !timeline.empty()) current = positionOf(timeline.front().index);
            if (automatic && repeat == Repeat::One) { open(true); return; }
            if (current + 1 < queue.size()) { ++current; open(automatic); return; }
            if (!automatic || repeat == Repeat::All) {
                if (shuffle && queue.size() > 1) {
                    const auto old = queue[current];
                    std::shuffle(queue.begin(), queue.end(), random);
                    if (queue[0] == old) std::swap(queue[0], queue[1]);
                }
                current = 0;
                open(automatic);
            } else {
                finished = true;
            }
        };

        for (;;) {
            std::vector<Request> requests;
            {
                std::unique_lock<std::mutex> guard(lock);
                if (!timeline.empty() && !paused) wake.wait_for(guard, std::chrono::milliseconds(8), [&] { return !pending.empty(); });
                else wake.wait(guard, [&] { return !pending.empty(); });
                requests.swap(pending);
            }
            for (auto& request : requests) {
                advanceTimeline();
                switch (request.action) {
                case Action::Play:
                    library = std::move(request.library);
                    authored = std::move(request.order);
                    if (authored.empty()) { close(false); break; }
                    rebuild(static_cast<size_t>(request.value));
                    open(false);
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
                    if (!queue.empty()) {
                        if (!timeline.empty()) current = positionOf(timeline.front().index);
                        current = current == 0 ? queue.size() - 1 : current - 1;
                        open(false);
                    }
                    break;
                case Action::Next: next(false); break;
                case Action::Pause:
                    if (stream) musicPaused.store(paused = !paused, std::memory_order_release);
                    break;
                case Action::Stop: close(false); break;
                case Action::Seek:
                    if (!timeline.empty() && std::isfinite(request.value) && request.value >= 0) {
                        const auto saved = current;
                        current = positionOf(timeline.front().index);
                        if (!start(false, request.value)) {
                            current = saved;
                            if (timeline.empty()) close(true);
                        }
                    }
                    break;
                }
            }
            if (timeline.empty()) continue;
            const auto outputRate = mixerRate.load(std::memory_order_acquire);
            if (outputRate && outputRate != rate) {
                // Reopen the audible track at the same position after a rate change.
                const auto frame = advanceTimeline();
                const auto& active = timeline.front();
                current = positionOf(active.index);
                if (!start(false, active.status.position + double(frame - active.start) / rate)) close(true);
                continue;
            }
            if (!paused && outputRate && !finished) {
                if (!exhausted && Queued() < reserve() * 2 / 3 && !fill(reserve())) finished = failed = true;
                // Append the next track while the current track's tail is still
                // queued. Its metadata becomes current when the mixer reaches it.
                if (exhausted && !finished && timeline.size() < 2) next(true);
            }
            if (finished && Queued() == 0) {
                close(failed);
                continue;
            }
            const auto frame = advanceTimeline();
            const auto& active = timeline.front();
            auto playback = active.status;
            playback.paused = paused;
            playback.position += double(frame - active.start) / rate;
            SetStatus(std::move(playback));
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
