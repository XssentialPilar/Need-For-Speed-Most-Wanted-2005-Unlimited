#pragma once

#include <windows.h>
#include <cstdint>
#include "../../nfs_discx.hpp"

namespace unlimited_split_screen {
constexpr std::uint32_t languageIconHash = 0x5c2b0010;
constexpr std::uint32_t discxNoticeCoverHash = 0x5c2b0016;
constexpr std::uint32_t discxNoticeTabHash = 0x5c2b0017;
constexpr std::uint32_t discxNoticeEmptyHash = 0x5c2b0018;
constexpr std::uint32_t discxCoverBase = 0xa56ee487;
void SetDiscCover(std::shared_ptr<const unlimited_discx::Cover> cover);
bool Install(HMODULE executable);
bool RaceActive();
bool DrawingPlayerPane();
void SetGameWindow(HWND window);
void SetRenderScale(unsigned tenths);
void SetPostPreferences(int filters, int motionBlur);
int FeedbackPad(int controllerPort);
}
