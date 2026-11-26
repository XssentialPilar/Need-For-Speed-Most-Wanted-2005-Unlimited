#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace movie_pass {

struct Extent {
    unsigned width;
    unsigned height;
};

struct Frame {
    Extent size;
    const std::uint8_t* bgra;
    std::size_t rowBytes;
    std::size_t storageSize;
};

struct Rect {
    unsigned left;
    unsigned top;
    Extent size;
};

constexpr Rect Fit(Extent source, Extent target) {
    if (!source.width || !source.height || !target.width || !target.height) return {};
    const auto scaledWidth = std::uint64_t(target.height) * source.width;
    const auto scaledHeight = std::uint64_t(target.width) * source.height;
    Extent fit = scaledWidth <= scaledHeight
        ? Extent{static_cast<unsigned>(scaledWidth / source.height), target.height}
        : Extent{target.width, static_cast<unsigned>(scaledHeight / source.width)};
    if (!fit.width) fit.width = 1;
    if (!fit.height) fit.height = 1;
    return {(target.width - fit.width) / 2, (target.height - fit.height) / 2, fit};
}

using Pixels = std::vector<std::uint8_t>;

inline std::size_t OutputSize(Frame frame, Extent target) {
    constexpr auto maxBytes = std::numeric_limits<std::size_t>::max();
    const auto rowSize = std::uint64_t(frame.size.width) * 4;
    if (!frame.bgra || !frame.size.width || !frame.size.height || !target.width || !target.height ||
        frame.rowBytes < rowSize || frame.rowBytes % 4 ||
        frame.rowBytes > frame.storageSize / frame.size.height ||
        std::uint64_t(target.width) * target.height > maxBytes / 4) {
        throw std::invalid_argument("Invalid movie dimensions, row pitch or frame storage");
    }
    return static_cast<std::size_t>(target.width) * target.height * 4;
}

} // namespace movie_pass
