#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#include "codec.hpp"

namespace discx_codec {

struct Stream {
    AVFormatContext* input = nullptr;
    AVCodecContext* decoder = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* converter = nullptr;
    int index = -1;
    int offset = 0;
    int outputRate = 0;
    bool draining = false;
    int64_t seekSample = -1;

    ~Stream() {
        swr_free(&converter);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&decoder);
        avformat_close_input(&input);
    }
};

static std::string Utf8(const wchar_t* input) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, nullptr, 0, nullptr, nullptr);
    if (!size) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, result.data(), size, nullptr, nullptr);
    result.resize(size - 1);
    return result;
}

static void ReadTag(const Stream& stream, const wchar_t* sidecar, const char* name,
                    const wchar_t* rawName, wchar_t (&text)[256]) {
    const auto* item = av_dict_get(stream.input->streams[stream.index]->metadata, name, nullptr, 0);
    if (!item || !item->value[0]) item = av_dict_get(stream.input->metadata, name, nullptr, 0);
    if (item && item->value[0]) {
        const auto value = std::string(item->value);
        // Keep a complete UTF-8 sequence if the display buffer would overflow.
        size_t length = std::min(value.size(), size_t(255));
        while (length && length < value.size() && (static_cast<unsigned char>(value[length]) & 0xc0) == 0x80) --length;
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(length), text, 255);
        if (!count) count = MultiByteToWideChar(1252, 0, value.data(), static_cast<int>(length), text, 255);
        text[count] = 0;
    }
    if (sidecar) GetPrivateProfileStringW(L"Tags", rawName, text, text, 256, sidecar);
    for (auto* c = text; *c; ++c) if (*c < 32) *c = L' ';
}

static Stream* OpenStream(const wchar_t* path, Metadata* metadata, bool scanOnly, int mixerRate) {
    if (!path || !metadata) return nullptr;
    try {
        auto url = Utf8(path);
        if (url.empty()) return nullptr;
        auto stream = std::make_unique<Stream>();
        static const bool loggingConfigured = [] { av_log_set_level(AV_LOG_QUIET); return true; }();
        (void)loggingConfigured;
        const AVInputFormat* raw = nullptr;
        std::wstring sidecar;
        AVDictionary* options = nullptr;
        av_dict_set(&options, "protocol_whitelist", "file", 0);
        av_dict_set(&options, "probesize", "524288", 0);
        av_dict_set(&options, "analyzeduration", "1500000", 0);
        if (const auto* extension = std::wcsrchr(path, L'.'); extension && !_wcsicmp(extension, L".pcm")) {
            sidecar = std::wstring(path) + L".ini";
            wchar_t format[16]{};
            GetPrivateProfileStringW(L"PCM", L"Format", L"s16le", format, 16, sidecar.c_str());
            if (wcscmp(format, L"s16le") && wcscmp(format, L"s24le") && wcscmp(format, L"s32le") && wcscmp(format, L"f32le")) {
                av_dict_free(&options);
                return nullptr;
            }
            raw = av_find_input_format(Utf8(format).c_str());
            const auto rate = GetPrivateProfileIntW(L"PCM", L"SampleRate", 44100, sidecar.c_str());
            const auto channels = GetPrivateProfileIntW(L"PCM", L"Channels", 2, sidecar.c_str());
            if (!raw || rate < 8000 || rate > 384000 || channels < 1 || channels > 8) {
                av_dict_free(&options);
                return nullptr;
            }
            char layout[16]{};
            std::snprintf(layout, sizeof layout, "%uc", channels);
            av_dict_set_int(&options, "sample_rate", rate, 0);
            av_dict_set(&options, "ch_layout", layout, 0);
        }

        const int opened = avformat_open_input(&stream->input, url.c_str(), raw, &options);
        av_dict_free(&options);
        if (opened < 0 || avformat_find_stream_info(stream->input, nullptr) < 0) return nullptr;
        const AVCodec* codec = nullptr;
        stream->index = av_find_best_stream(stream->input, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (stream->index < 0 || !codec) return nullptr;
        const auto* selected = stream->input->streams[stream->index];
        const int rate = selected->codecpar->sample_rate;
        if (rate < 8000 || rate > 384000 || selected->codecpar->ch_layout.nb_channels < 1 ||
            selected->codecpar->ch_layout.nb_channels > 16) return nullptr;
        *metadata = {};
        metadata->sampleRate = rate;
        metadata->duration = selected->duration > 0 ? selected->duration * av_q2d(selected->time_base) :
            stream->input->duration > 0 ? stream->input->duration / double(AV_TIME_BASE) : 0;
        const auto* tags = sidecar.empty() ? nullptr : sidecar.c_str();
        ReadTag(*stream, tags, "title", L"Title", metadata->title);
        ReadTag(*stream, tags, "artist", L"Artist", metadata->artist);
        if (!metadata->artist[0]) ReadTag(*stream, tags, "album_artist", L"AlbumArtist", metadata->artist);
        ReadTag(*stream, tags, "album", L"Album", metadata->album);
        ReadTag(*stream, tags, "composer", L"Composer", metadata->composer);
        if (scanOnly) return stream.release();
        stream->outputRate = mixerRate ? mixerRate : rate;

        stream->decoder = avcodec_alloc_context3(codec);
        stream->packet = av_packet_alloc();
        stream->frame = av_frame_alloc();
        if (!stream->decoder || !stream->packet || !stream->frame ||
            avcodec_parameters_to_context(stream->decoder, selected->codecpar) < 0 ||
            avcodec_open2(stream->decoder, codec, nullptr) < 0) return nullptr;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&stream->converter, &stereo, AV_SAMPLE_FMT_FLT, stream->outputRate,
                               &stream->decoder->ch_layout, stream->decoder->sample_fmt, rate, 0, nullptr) < 0 ||
            swr_init(stream->converter) < 0) return nullptr;
        return stream.release();
    } catch (...) {
        return nullptr;
    }
}

extern "C" __declspec(dllexport) Stream* __cdecl DiscCX_Open(const wchar_t* path, Metadata* metadata, bool scanOnly) {
    return OpenStream(path, metadata, scanOnly, 0);
}

extern "C" __declspec(dllexport) Stream* __cdecl DiscCX_OpenForMixer(const wchar_t* path, Metadata* metadata, int rate) {
    return rate >= 8000 && rate <= 192000 ? OpenStream(path, metadata, false, rate) : nullptr;
}

extern "C" __declspec(dllexport) const std::uint8_t* __cdecl DiscCX_Artwork(const Stream* reader, unsigned* bytes) {
    if (!reader || !bytes) return nullptr;
    *bytes = 0;
    const AVPacket* selected = nullptr;
    for (unsigned i = 0; i < reader->input->nb_streams; ++i) {
        const auto* stream = reader->input->streams[i];
        const auto& packet = stream->attached_pic;
        if (!(stream->disposition & AV_DISPOSITION_ATTACHED_PIC) || !packet.data ||
            packet.size <= 0 || packet.size > 16 * 1024 * 1024) continue;
        const auto* type = av_dict_get(stream->metadata, "comment", nullptr, 0);
        if (!selected) selected = &packet;
        if (type && !_stricmp(type->value, "Cover (front)")) { selected = &packet; break; }
    }
    if (!selected) return nullptr;
    *bytes = static_cast<unsigned>(selected->size);
    return selected->data;
}

extern "C" __declspec(dllexport) void __cdecl DiscCX_Close(Stream* stream) {
    delete stream;
}

extern "C" __declspec(dllexport) int __cdecl DiscCX_Read(Stream* stream, float* output, int frames) {
    if (!stream || !stream->converter || !output || frames <= 0 || frames > 32768) return -1;
    auto& reader = *stream;
    for (unsigned attempts = 0; attempts != 512; ++attempts) {
        if (reader.offset < reader.frame->nb_samples) {
            const auto format = reader.decoder->sample_fmt;
            const auto channels = reader.decoder->ch_layout.nb_channels;
            const auto planar = av_sample_fmt_is_planar(format);
            const auto bytes = av_get_bytes_per_sample(format);
            const uint8_t* input[16]{};
            const auto inputBudget = std::max<int64_t>(1, av_rescale_rnd(frames,
                reader.decoder->sample_rate, reader.outputRate, AV_ROUND_DOWN));
            const int count = static_cast<int>(std::min<int64_t>(inputBudget, reader.frame->nb_samples - reader.offset));
            for (int i = 0; i < (planar ? channels : 1); ++i) {
                if (!reader.frame->extended_data[i]) return -1;
                input[i] = reader.frame->extended_data[i] + reader.offset * bytes * (planar ? 1 : channels);
            }
            auto* packed = reinterpret_cast<uint8_t*>(output);
            const int converted = swr_convert(reader.converter, &packed, frames, input, count);
            reader.offset += count;
            if (converted != 0) return converted;
        }

        av_frame_unref(reader.frame);
        reader.offset = 0;
        const int result = avcodec_receive_frame(reader.decoder, reader.frame);
        if (result == 0) {
            if (reader.frame->sample_rate != reader.decoder->sample_rate ||
                reader.frame->format != reader.decoder->sample_fmt ||
                reader.frame->ch_layout.nb_channels != reader.decoder->ch_layout.nb_channels) return -1;
            if (reader.seekSample >= 0) {
                const auto* audio = reader.input->streams[reader.index];
                const auto pts = reader.frame->best_effort_timestamp;
                if (pts != AV_NOPTS_VALUE) {
                    const auto origin = audio->start_time == AV_NOPTS_VALUE ? 0 : audio->start_time;
                    const auto first = av_rescale_q(pts - origin, audio->time_base, {1, reader.decoder->sample_rate});
                    reader.offset = static_cast<int>(std::clamp<int64_t>(reader.seekSample - first, 0, reader.frame->nb_samples));
                }
                if (reader.offset < reader.frame->nb_samples) reader.seekSample = -1;
            }
            continue;
        }
        if (result == AVERROR_EOF) {
            auto* packed = reinterpret_cast<std::uint8_t*>(output);
            return swr_convert(reader.converter, &packed, frames, nullptr, 0);
        }
        if (result != AVERROR(EAGAIN) || reader.draining) return -1;
        const int got = av_read_frame(reader.input, reader.packet);
        if (got < 0) {
            if (got != AVERROR_EOF) return -1;
            reader.draining = true;
            if (avcodec_send_packet(reader.decoder, nullptr) < 0) return -1;
            continue;
        }
        const int sent = reader.packet->stream_index == reader.index ? avcodec_send_packet(reader.decoder, reader.packet) : 0;
        av_packet_unref(reader.packet);
        if (sent < 0 && sent != AVERROR_INVALIDDATA) return -1;
    }
    return -1;
}

extern "C" __declspec(dllexport) bool __cdecl DiscCX_Seek(Stream* stream, double seconds) {
    if (!stream || !stream->converter || !std::isfinite(seconds) || seconds < 0 || seconds > 10000000) return false;
    auto& reader = *stream;
    const auto* audio = reader.input->streams[reader.index];
    const int rate = reader.decoder->sample_rate;
    const int64_t sample = static_cast<int64_t>(seconds * rate);
    const int64_t origin = audio->start_time == AV_NOPTS_VALUE ? 0 : audio->start_time;
    const int64_t target = origin + av_rescale_q(sample, {1, rate}, audio->time_base);
    if (avformat_seek_file(reader.input, reader.index, INT64_MIN, target, target, 0) < 0) return false;
    avcodec_flush_buffers(reader.decoder);
    av_frame_unref(reader.frame);
    reader.offset = 0;
    reader.draining = false;
    reader.seekSample = sample;
    swr_close(reader.converter);
    return swr_init(reader.converter) >= 0;
}

}
