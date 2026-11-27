#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>

namespace unlimited_settings {

inline bool UserFile(wchar_t (&output)[MAX_PATH], const wchar_t* group) {
    wchar_t folder[MAX_PATH]{}, executable[MAX_PATH]{};
    const DWORD folderLength = GetEnvironmentVariableW(L"LOCALAPPDATA", folder, MAX_PATH);
    const DWORD exeLength = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!folderLength || folderLength >= MAX_PATH || !exeLength || exeLength >= MAX_PATH) return false;
    const int length = std::swprintf(output, MAX_PATH, L"%ls\\NFSMW-Unlimited", folder);
    if (length < 1 || length >= MAX_PATH) { output[0] = 0; return false; }
    if (!CreateDirectoryW(output, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        output[0] = 0;
        return false;
    }
    CharLowerBuffW(executable, exeLength);
    std::uint32_t hash = 2166136261u;
    for (DWORD i = 0; i < exeLength; ++i) hash = (hash ^ executable[i]) * 16777619u;
    const int suffix = std::swprintf(output + length, MAX_PATH - length, L"\\%ls-%08x.ini",
                                     group, static_cast<unsigned>(hash));
    if (suffix < 1 || suffix >= MAX_PATH - length) { output[0] = 0; return false; }
    return true;
}

} // namespace unlimited_settings
