#pragma once
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace unlimited_display {

enum class Mode : unsigned { Exclusive = 0, Borderless = 1, Windowed = 2 };

constexpr Mode DecodeMode(unsigned value) noexcept {
    return value <= static_cast<unsigned>(Mode::Windowed) ? static_cast<Mode>(value) : Mode::Exclusive;
}

inline D3DPRESENT_PARAMETERS ForMode(const D3DPRESENT_PARAMETERS& source, Mode mode) noexcept {
    auto desired = source;
    desired.Windowed = mode != Mode::Exclusive;
    if (desired.Windowed) {
        desired.FullScreen_RefreshRateInHz = 0;
        if (desired.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE &&
            desired.PresentationInterval != D3DPRESENT_INTERVAL_DEFAULT)
            desired.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    }
    return desired;
}

struct Geometry {
    RECT outer{};
    SIZE client{};
};

inline Geometry Place(Mode mode, const MONITORINFO& display, LONG width, LONG height, SIZE frame) noexcept {
    const RECT monitor = display.rcMonitor;
    const RECT work = display.rcWork;
    const LONG monitorWidth = monitor.right - monitor.left;
    const LONG monitorHeight = monitor.bottom - monitor.top;
    if (width <= 0) width = monitorWidth;
    if (height <= 0) height = monitorHeight;
    if (mode == Mode::Borderless) return {monitor, {monitorWidth, monitorHeight}};
    if (mode == Mode::Exclusive)
        return {{monitor.left, monitor.top, monitor.left + width, monitor.top + height}, {width,height}};

    const LONG workWidth = work.right - work.left;
    const LONG workHeight = work.bottom - work.top;
    const LONG availableWidth = (std::max)(1L, workWidth - frame.cx);
    const LONG availableHeight = (std::max)(1L, workHeight - frame.cy);
    if (width > availableWidth || height > availableHeight) {
        if (std::int64_t(availableWidth) * height <= std::int64_t(availableHeight) * width) {
            height = (std::max)(1L, LONG(std::int64_t(height) * availableWidth / width));
            width = availableWidth;
        } else {
            width = (std::max)(1L, LONG(std::int64_t(width) * availableHeight / height));
            height = availableHeight;
        }
    }
    const LONG outerWidth = width + frame.cx;
    const LONG outerHeight = height + frame.cy;
    const LONG x = work.left + (workWidth - outerWidth) / 2;
    const LONG y = work.top + (workHeight - outerHeight) / 2;
    return {{x, y, x + outerWidth, y + outerHeight}, {width,height}};
}

class WindowHost {
public:
    bool Applying() const noexcept { return applying; }

    // Call on the owner thread after releasing any previous native swapchain.
    // An explicit monitor is for a chosen output; otherwise follow the game HWND.
    bool Apply(HWND game, Mode mode, UINT renderWidth, UINT renderHeight,
               HMONITOR explicitMonitor = nullptr) noexcept {
        DWORD process = 0;
        if (applying || !game || GetWindowThreadProcessId(game, &process) != GetCurrentThreadId() ||
            process != GetCurrentProcessId() || GetAncestor(game, GA_ROOT) != game || IsIconic(game))
            return false;
        if (renderWidth > UINT((std::numeric_limits<LONG>::max)()) ||
            renderHeight > UINT((std::numeric_limits<LONG>::max)()))
            return false;

        struct ApplyingGuard {
            bool& flag;
            explicit ApplyingGuard(bool& value) : flag(value) { flag = true; }
            ~ApplyingGuard() { flag = false; }
        } guard(applying);
        // Use physical monitor/window coordinates without changing the process DPI
        // policy or the awareness of the game's existing window procedure.
        DpiScope dpiScope;
        const HMONITOR monitor = explicitMonitor ? explicitMonitor : MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST);
        MONITORINFO display{};
        display.cbSize = sizeof(display);
        RECT current{}, client{};
        if (!GetMonitorInfoW(monitor, &display) || !GetWindowRect(game, &current) || !GetClientRect(game, &client))
            return false;

        const DWORD oldStyle = static_cast<DWORD>(GetWindowLongPtrW(game, GWL_STYLE));
        const DWORD oldExtended = static_cast<DWORD>(GetWindowLongPtrW(game, GWL_EXSTYLE));
        const DWORD style = (oldStyle & ~(WS_POPUP | WS_OVERLAPPEDWINDOW | WS_MAXIMIZE)) |
            (mode == Mode::Windowed ? (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX) : WS_POPUP);
        // The D3D runtime may temporarily make an exclusive HWND topmost.
        // Demote a desktop window using SetWindowPos so USER32 updates its Z band.
        const bool demote = mode != Mode::Exclusive && (oldExtended & WS_EX_TOPMOST);
        const DWORD extended = oldExtended & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME);

        SIZE frame{};
        if (mode == Mode::Windowed && !FrameSize(game, style, extended, frame)) return false;
        const LONG width = renderWidth ? LONG(renderWidth) : client.right - client.left;
        const LONG height = renderHeight ? LONG(renderHeight) : client.bottom - client.top;
        auto target = Place(mode, display, width, height, frame).outer;
        if (mode == Mode::Windowed && game == lastWindow && mode == lastMode &&
            monitor == lastMonitor && renderWidth == lastWidth && renderHeight == lastHeight &&
            (current.right-current.left == target.right-target.left) &&
            (current.bottom-current.top == target.bottom-target.top) &&
            current.left >= display.rcWork.left && current.top >= display.rcWork.top &&
            current.right <= display.rcWork.right && current.bottom <= display.rcWork.bottom) {
            // The user can move the window. Applying another graphics setting
            // with the same client size should preserve its chosen position.
            target = current;
        }
        bool styleChanged = oldStyle != style;
        bool extendedChanged = oldExtended != extended;
        if (styleChanged && !WriteStyle(game, GWL_STYLE, style)) return false;
        if (extendedChanged && !WriteStyle(game, GWL_EXSTYLE, extended)) {
            if (styleChanged) WriteStyle(game, GWL_STYLE, oldStyle);
            return false;
        }

        if (styleChanged || extendedChanged || demote || !EqualRect(&current, &target)) {
            UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
            if (!demote) flags |= SWP_NOZORDER;
            if (styleChanged || extendedChanged) flags |= SWP_FRAMECHANGED;
            if (!SetWindowPos(game, demote ? HWND_NOTOPMOST : nullptr, target.left, target.top,
                              target.right-target.left, target.bottom-target.top, flags)) {
                if (styleChanged) WriteStyle(game, GWL_STYLE, oldStyle);
                if (extendedChanged) WriteStyle(game, GWL_EXSTYLE, oldExtended);
                SetWindowPos(game, nullptr, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER |
                             SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
                return false;
            }
        }
        lastWindow = game;
        lastMode = mode;
        lastMonitor = monitor;
        lastWidth = renderWidth;
        lastHeight = renderHeight;
        return true;
    }
private:
    struct DpiScope {
        using SetContext = HANDLE (WINAPI*)(HANDLE);
        SetContext set{};
        HANDLE previous{};
        DpiScope() {
            static const auto proc = reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
            set = proc;
            if (set) previous = set(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))); // PER_MONITOR_AWARE_V2
        }
        ~DpiScope() { if (previous) set(previous); }
    };
    static bool FrameSize(HWND game, DWORD style, DWORD extended, SIZE& frame) {
        using GetDpi = UINT (WINAPI*)(HWND);
        using Adjust = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static const HMODULE user = GetModuleHandleW(L"user32.dll");
        static const auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(user, "GetDpiForWindow"));
        static const auto adjust = reinterpret_cast<Adjust>(GetProcAddress(user, "AdjustWindowRectExForDpi"));
        RECT decoration{};
        const BOOL menu = GetMenu(game) != nullptr;
        const UINT dpi = getDpi ? getDpi(game) : 0;
        if (adjust && dpi) {
            if (!adjust(&decoration, style, menu, extended, dpi)) return false;
        } else if (!AdjustWindowRectEx(&decoration, style, menu, extended)) return false;
        frame = {decoration.right-decoration.left, decoration.bottom-decoration.top};
        return true;
    }
    static bool WriteStyle(HWND game, int index, DWORD style) {
        SetLastError(ERROR_SUCCESS);
        const auto previous = SetWindowLongPtrW(game, index, static_cast<LONG_PTR>(style));
        return previous || GetLastError() == ERROR_SUCCESS;
    }
    bool applying{};
    HWND lastWindow{};
    HMONITOR lastMonitor{};
    Mode lastMode = Mode::Exclusive;
    UINT lastWidth{}, lastHeight{};
};

} // namespace unlimited_display
