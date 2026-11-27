#pragma once

#include <atomic>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "third-party/discx-codec/codec.hpp"

namespace unlimited_discx {

struct Track {
    std::filesystem::path path;
    discx_codec::Metadata metadata;
};
using Tracks = std::vector<Track>;
constexpr unsigned coverEdge = 128;
using Cover = std::array<std::uint32_t, coverEdge * coverEdge>;

struct Catalog {
    std::shared_ptr<const Tracks> tracks;
    unsigned errors = 0;
    bool scanning = false;
    bool codecAvailable = false;
};

struct Playback {
    std::wstring title, artist, album;
    std::filesystem::path path;
    double position = 0, duration = 0;
    bool playing = false, paused = false;
    bool failed = false;
    std::uint64_t sequence = 0;
    std::shared_ptr<const Cover> cover;
};

enum class Repeat { Off, All, One };

void StartScan();
Catalog Library();
Playback NowPlaying();
bool MusicActive();
bool Selected();
bool Enabled();
bool PlayingInGameMix();
void SetEnabled(bool enabled);
void SelectStockMusic();
std::uint64_t TrackSequence();
void SetGameMix(unsigned volumePercent, unsigned cutoffHz);
void SetPreviewGain(float gain);
void SetNativeMixer(unsigned rate);
void SetOutputActive(bool active);
void MixNative(std::int16_t* interleaved, std::size_t frames, unsigned channels, unsigned rate);
void Play(std::shared_ptr<const Tracks> library, std::vector<size_t> order, size_t index);
void SetShuffle(bool enabled);
void SetRepeat(Repeat mode);
void Previous();
void Next();
void Pause();
void Stop();
void Seek(double seconds);

}
