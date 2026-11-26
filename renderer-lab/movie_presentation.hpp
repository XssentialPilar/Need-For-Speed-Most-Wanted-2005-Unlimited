#pragma once

#include "movie_pass.hpp"
#include <windows.h>

namespace movie_pass {

enum class MoviePresentation {
    Drawn,
    Presented,
    Suspended,
    Occluded,
    SurfaceChanged,
    DeviceLost,
};

inline Extent ClientExtent(HWND window) {
    RECT bounds{};
    if (!GetClientRect(window, &bounds)) throw std::runtime_error("Invalid presentation window");
    if (IsIconic(window)) return {};
    return {static_cast<unsigned>(bounds.right - bounds.left), static_cast<unsigned>(bounds.bottom - bounds.top)};
}

} // namespace movie_pass
