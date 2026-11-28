#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>

static const UCHAR supported_hash[32] = {
    0x80, 0x77, 0x4c, 0x2e, 0x5d, 0x61, 0x9b, 0x4f, 0x12, 0x0b, 0x48, 0xd4, 0x46, 0x28, 0x96, 0xfd,
    0x50, 0x4c, 0x26, 0x33, 0x99, 0xd2, 0x03, 0xa2, 0x38, 0x76, 0x9c, 0xff, 0xde, 0x1d, 0x25, 0x3c
};

static const UCHAR large_address_hash[32] = {
    0xbd, 0xe1, 0x2b, 0xdd, 0x15, 0x8b, 0x7f, 0x86, 0x10, 0x78, 0xad, 0x45, 0x27, 0xf5, 0xa6, 0x56,
    0xf3, 0x4c, 0x60, 0x68, 0xe4, 0xa2, 0x12, 0x0f, 0x28, 0x04, 0x4f, 0x92, 0xf0, 0xfa, 0x15, 0x8c
};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}

static DWORD game_image(HANDLE file) {
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart != 6029312) return FALSE;
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    HANDLE heap = GetProcessHeap();
    UCHAR *buffer = NULL;
    DWORD storage_size = 0, written = 0;
    UCHAR digest[32];
    DWORD matches = 0;
    const DWORD block_size = 65536;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&storage_size,
                          sizeof(storage_size), &written, 0) < 0 ||
        written != sizeof(storage_size) || storage_size > 65536) goto cleanup;
    buffer = HeapAlloc(heap, 0, storage_size + block_size);
    if (!buffer || BCryptCreateHash(algorithm, &hash, buffer, storage_size, NULL, 0, 0) < 0) goto cleanup;
    LONGLONG remaining = size.QuadPart;
    while (remaining > 0) {
        DWORD count;
        if (!ReadFile(file, buffer + storage_size, block_size, &count, NULL) || !count ||
            count > remaining || BCryptHashData(hash, buffer + storage_size, count, 0) < 0) goto cleanup;
        remaining -= count;
    }
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) goto cleanup;
    UCHAR original_difference = 0, large_difference = 0;
    for (unsigned i = 0; i < sizeof(supported_hash); ++i) {
        original_difference |= digest[i] ^ supported_hash[i];
        large_difference |= digest[i] ^ large_address_hash[i];
    }
    matches = !original_difference ? 1 : !large_difference ? 2 : 0;
cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (buffer) HeapFree(heap, 0, buffer);
    return matches;
}

static BOOL game_running(const wchar_t *path, wchar_t *other, DWORD capacity) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    BOOL running = FALSE;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (CompareStringOrdinal(entry.szExeFile, -1, L"speed.exe", -1, TRUE) != CSTR_EQUAL) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) continue;
        DWORD length = capacity;
        running = QueryFullProcessImageNameW(process, 0, other, &length) &&
                  CompareStringOrdinal(path, -1, other, -1, TRUE) == CSTR_EQUAL;
        CloseHandle(process);
        if (running) break;
    }
    CloseHandle(snapshot);
    return running;
}

static BOOL append(wchar_t *path, DWORD capacity, DWORD root_length, const wchar_t *suffix) {
    DWORD pos = root_length;
    while (pos + 1 < capacity && *suffix) path[pos++] = *suffix++;
    path[pos] = 0;
    return !*suffix;
}

DWORD WINAPI VerifyGameDirectory(const wchar_t *directory) {
    enum { capacity = 32768 };
    if (!directory || !*directory) return 1;
    HANDLE heap = GetProcessHeap();
    wchar_t *path = HeapAlloc(heap, 0, capacity * 2 * sizeof(wchar_t));
    if (!path) return 1;
    DWORD length = GetFullPathNameW(directory, capacity, path, NULL);
    DWORD result = 1;
    if (!length || length >= capacity || !append(path, capacity, length, L"\\speed.exe")) goto done;
    if (game_running(path, path + capacity, capacity)) {
        result = 2;
        goto done;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) result = 2;
        goto done;
    }
    const DWORD compatible = game_image(file);
    CloseHandle(file);
    if (!compatible || !append(path, capacity, length, L"\\TRACKS")) goto done;
    const DWORD tracks = GetFileAttributesW(path);
    if (tracks == INVALID_FILE_ATTRIBUTES || !(tracks & FILE_ATTRIBUTE_DIRECTORY)) goto done;
    DWORD global = append(path, capacity, length, L"\\GLOBAL\\GLOBALB.BUN") ?
                   GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
    if (global == INVALID_FILE_ATTRIBUTES) {
        global = append(path, capacity, length, L"\\GLOBAL\\GlobalB.lzc") ?
                 GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
    }
    if (global != INVALID_FILE_ATTRIBUTES && !(global & FILE_ATTRIBUTE_DIRECTORY)) result = 0;
done:
    HeapFree(heap, 0, path);
    return result;
}

static BOOL read_exact(HANDLE file, void *data, DWORD size) {
    DWORD bytes;
    return ReadFile(file, data, size, &bytes, NULL) && bytes == size;
}

typedef ULONG (WINAPI *ComputeCrc32)(ULONG, const void *, ULONG);

static DWORD load_le32(const BYTE *bytes) {
    return (DWORD)bytes[0] | ((DWORD)bytes[1] << 8) |
           ((DWORD)bytes[2] << 16) | ((DWORD)bytes[3] << 24);
}

static BOOL read_ups_number(const BYTE *patch, DWORD end, DWORD *cursor, DWORD *out) {
    ULONGLONG value = 0, weight = 1;
    for (unsigned i = 0; i < 5 && *cursor < end; ++i) {
        const BYTE byte = patch[(*cursor)++];
        value += (byte & 127u) * weight;
        if (byte & 128u) {
            if (value > MAXDWORD) return FALSE;
            *out = (DWORD)value;
            return TRUE;
        }
        weight <<= 7;
        value += weight;
    }
    return FALSE;
}

static BOOL apply_track_delta(BYTE *track, DWORD track_size, const BYTE *patch, DWORD end, DWORD cursor) {
    DWORD offset = 0;
    while (cursor < end) {
        DWORD distance;
        if (offset >= track_size || !read_ups_number(patch, end, &cursor, &distance) ||
            distance >= track_size - offset || cursor == end || !patch[cursor]) return FALSE;
        offset += distance;
        while (cursor < end && patch[cursor]) {
            if (offset >= track_size) return FALSE;
            track[offset++] ^= patch[cursor++];
        }
        if (cursor == end) return FALSE;
        ++cursor;
        ++offset;
    }
    return TRUE;
}

enum { track_patch_size = 5078 };

static BOOL stage_console_track(const wchar_t *source, const wchar_t *delta, const wchar_t *output, BYTE *patch) {
    enum { track_size = 532663556, table_end = track_patch_size - 12 };
    const DWORD stock_crc = 0xbd947661u, console_crc = 0xae90f386u;
    ComputeCrc32 crc = (ComputeCrc32)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlComputeCrc32");
    if (!crc) return FALSE;

    LARGE_INTEGER size;
    HANDLE file = CreateFileW(delta, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    BOOL ok = GetFileSizeEx(file, &size) && size.QuadPart == track_patch_size && read_exact(file, patch, track_patch_size);
    CloseHandle(file);
    if (!ok || load_le32(patch) != 0x31535055u || load_le32(patch + table_end) != stock_crc ||
        load_le32(patch + table_end + 4) != console_crc ||
        crc(0, patch, track_patch_size - 4) != load_le32(patch + track_patch_size - 4)) return FALSE;

    DWORD cursor = 4, before, after;
    if (!read_ups_number(patch, table_end, &cursor, &before) || before != track_size ||
        !read_ups_number(patch, table_end, &cursor, &after) || after != track_size ||
        !CopyFileW(source, output, TRUE)) return FALSE;

    file = CreateFileW(output, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE mapping = NULL;
    BYTE *track = NULL;
    ok = FALSE;
    if (file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && size.QuadPart == track_size) {
        mapping = CreateFileMappingW(file, NULL, PAGE_READWRITE, 0, 0, NULL);
        if (mapping) track = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, track_size);
        ok = track && crc(0, track, track_size) == stock_crc &&
             apply_track_delta(track, track_size, patch, table_end, cursor) &&
             crc(0, track, track_size) == console_crc && FlushViewOfFile(track, track_size);
    }
    if (track) UnmapViewOfFile(track);
    if (mapping) CloseHandle(mapping);
    if (file != INVALID_HANDLE_VALUE) {
        ok = ok && FlushFileBuffers(file);
        CloseHandle(file);
    }
    if (!ok) DeleteFileW(output);
    return ok;
}

BOOL WINAPI StageConsoleTrack(const wchar_t *source, const wchar_t *delta, const wchar_t *output) {
    HANDLE heap = GetProcessHeap();
    BYTE *patch = HeapAlloc(heap, 0, track_patch_size);
    if (!patch) return FALSE;
    const BOOL ok = stage_console_track(source, delta, output, patch);
    HeapFree(heap, 0, patch);
    return ok;
}

BOOL WINAPI EnableLargeAddressCopy(const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER position;
    position.QuadPart = 0x11e; // COFF Characteristics in the verified PC 1.3 image
    WORD characteristics;
    DWORD written = 0;
    BOOL ok = game_image(file) == 1 &&
              SetFilePointerEx(file, position, NULL, FILE_BEGIN) &&
              read_exact(file, &characteristics, sizeof(characteristics));
    if (ok) {
        characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
        position.QuadPart = 0x11e;
        ok = SetFilePointerEx(file, position, NULL, FILE_BEGIN) &&
             WriteFile(file, &characteristics, sizeof(characteristics), &written, NULL) &&
             written == sizeof(characteristics) && FlushFileBuffers(file);
    }
    position.QuadPart = 0;
    ok = ok && SetFilePointerEx(file, position, NULL, FILE_BEGIN) && game_image(file) == 2;
    CloseHandle(file);
    return ok;
}

static BOOL valid_hd_movie(HANDLE file, BOOL pal) {
    DWORD header[8];
    LARGE_INTEGER length;
    if (!GetFileSizeEx(file, &length) || length.QuadPart < sizeof(header) ||
        length.QuadPart > 0x7fffffff || !read_exact(file, header, sizeof(header)) ||
        header[0] != 0x6468564d || header[1] != sizeof(header) || header[2] != 0x56503630 ||
        !header[4] || !header[5] || header[5] > length.QuadPart || !header[7]) return FALSE;
    const DWORD width = header[3] & 0xffff, height = header[3] >> 16;
    if (!(width == 1280 && height == 720) && !(width == 1920 && (height == 1080 || height == 1088))) return FALSE;
    LONGLONG error = pal ? (LONGLONG)header[6] - 25LL * header[7]
                         : 1001LL * header[6] - 30000LL * header[7];
    if (error < 0) error = -error;
    if (pal ? 10 * error > header[7] : 10 * error > 1001LL * header[7]) return FALSE;

    const DWORD largest_packet = (header[5] + 3) & ~3u;
    DWORD frames = 0;
    LARGE_INTEGER pos;
    for (pos.QuadPart = sizeof(header); pos.QuadPart < length.QuadPart;) {
        DWORD packet[2];
        if (!SetFilePointerEx(file, pos, NULL, FILE_BEGIN) || !read_exact(file, packet, sizeof(packet)) ||
            packet[1] < sizeof(packet) || packet[1] > length.QuadPart - pos.QuadPart) return FALSE;
        const BOOL keyframe = packet[0] == 0x4b30564d; // MV0K
        if (keyframe || packet[0] == 0x4630564d) {   // MV0F
            const DWORD size = packet[1] - sizeof(packet);
            if (!size || size > largest_packet || (!frames && !keyframe) || ++frames > header[4]) return FALSE;
        }
        if (keyframe) {
            UCHAR frame[8];
            if (packet[1] <= sizeof(packet) + sizeof(frame) || !read_exact(file, frame, sizeof(frame)) ||
                (frame[0] & 0x80) || (frame[1] >> 3) > 8) return FALSE;
            const unsigned offset = ((frame[0] & 1) || !(frame[1] & 6)) ? 4 : 2;
            if (frame[offset] != (height + 15) / 16 || frame[offset + 1] != (width + 15) / 16) return FALSE;
        }
        pos.QuadPart += packet[1];
    }
    return frames == header[4];
}

BOOL WINAPI VerifyReplacementMovie(const wchar_t *path, BOOL pal) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    const BOOL valid = valid_hd_movie(file, pal);
    CloseHandle(file);
    return valid;
}
