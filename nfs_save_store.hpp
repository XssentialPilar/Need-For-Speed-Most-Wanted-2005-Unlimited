#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace unlimited_saves {
namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
constexpr unsigned maxContainer = 16 * 1024 * 1024; // Retail PC profiles are about 64 KiB.

inline unsigned Word(const unsigned char* p) {
    unsigned value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

inline unsigned EaChecksum(const unsigned char* data, size_t size) {
    unsigned crc = ~((unsigned(data[0]) << 24) | (unsigned(data[1]) << 16) |
                     (unsigned(data[2]) << 8) | data[3]);
    static const auto table = [] {
        std::array<unsigned, 256> entries{};
        for (unsigned i = 0; i != entries.size(); ++i) {
            unsigned v = i << 24;
            for (int j = 0; j < 8; ++j) v = (v << 1) ^ ((v >> 31) * 0x04c11db7u);
            entries[i] = v;
        }
        return entries;
    }();
    for (size_t i = 4; i < size; ++i) crc = ((crc << 8) | data[i]) ^ table[crc >> 24];
    return ~crc;
}

enum class Integrity { Valid, Damaged, Unsupported, Unavailable };

inline Integrity Check(const Bytes& b) {
    if (b.size() < 28) return Integrity::Damaged;
    auto* p = b.data();
    if (Word(p) != 0x4d433032) {
        const bool profileEnvelope=b.size()>=68 && Word(p+8)==8 && Word(p+28)==0x10d &&
                                   Word(p+4)==b.size() && Word(p+32)==b.size()-36;
        return profileEnvelope ? Integrity::Damaged : Integrity::Unsupported;
    }
    if (Word(p+4) != b.size()) return Integrity::Damaged;
    if (Word(p+8) != 8) return Integrity::Unsupported;
    if (b.size() < 68 || Word(p+12) != b.size()-36 ||
        Word(p+16) != EaChecksum(p+28,8) || Word(p+20) != EaChecksum(p+36,b.size()-36) ||
        Word(p+24) != EaChecksum(p,24)) return Integrity::Damaged;
    if (Word(p+28) != 0x10d) return Integrity::Unsupported;
    if (Word(p+32) != Word(p+12)) return Integrity::Damaged;

    HCRYPTPROV provider{};
    HCRYPTHASH hash{};
    if (!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT))
        return Integrity::Unavailable;
    unsigned char digest[16];
    DWORD length = sizeof(digest);
    bool ok = CryptCreateHash(provider,CALG_MD5,0,0,&hash) &&
              CryptHashData(hash,p+52,static_cast<DWORD>(b.size()-68),0) &&
              CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0);
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider,0);
    if (!ok || length != sizeof(digest)) return Integrity::Unavailable;
    return std::memcmp(digest,p+b.size()-16,16) == 0 ? Integrity::Valid : Integrity::Damaged;
}

struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    File() = default;
    explicit File(HANDLE handle) : h(handle) {}
    File(const File&) = delete;
    ~File() { close(); }
    bool close() {
        if (h == INVALID_HANDLE_VALUE) return true;
        auto handle = h;
        h = INVALID_HANDLE_VALUE;
        return CloseHandle(handle) != FALSE;
    }
};

inline DWORD Read(const fs::path& path, Bytes& bytes) {
    File file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_DELETE,
                          nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (file.h == INVALID_HANDLE_VALUE) return GetLastError();
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.h,&size)) return GetLastError();
    if (size.QuadPart > maxContainer) return ERROR_FILE_TOO_LARGE;
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read{};
    if (!bytes.empty() && !ReadFile(file.h,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr))
        return GetLastError();
    return read == bytes.size() ? ERROR_SUCCESS : ERROR_HANDLE_EOF;
}

inline bool AtomicWrite(const fs::path& target, const Bytes& data) {
    // The staging file lives on the same volume. The previous name remains readable
    // until the flushed, read-back-verified replacement is ready.
    auto stage = target;
    stage += L".pending";
    // Discard an interrupted transaction by name before creating a new file.
    // CREATE_NEW also prevents writing through an existing link at this path.
    if (!DeleteFileW(stage.c_str()) && GetLastError()!=ERROR_FILE_NOT_FOUND) return false;
    File file(CreateFileW(stage.c_str(),GENERIC_READ | GENERIC_WRITE,0,nullptr,CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL,nullptr));
    if (file.h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote{}, read{};
    std::vector<unsigned char> verify(data.size());
    bool ok = WriteFile(file.h,data.data(),static_cast<DWORD>(data.size()),&wrote,nullptr) &&
              wrote == data.size() && FlushFileBuffers(file.h) &&
              SetFilePointer(file.h,0,nullptr,FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
              ReadFile(file.h,verify.data(),static_cast<DWORD>(verify.size()),&read,nullptr) &&
              read == data.size() && verify == data;
    ok = file.close() && ok;
    if (ok && MoveFileExW(stage.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(stage.c_str());
    return false;
}

inline bool ProfilePath(const char* input, fs::path& dest) {
    if (!input || !*input) return false;
    wchar_t path[MAX_PATH];
    if (!MultiByteToWideChar(AreFileApisANSI()?CP_ACP:CP_OEMCP,0,input,-1,path,MAX_PATH)) return false;
    dest = fs::path(path).lexically_normal();
    const auto profile = dest.parent_path();
    return dest.is_absolute() && !dest.filename().empty() &&
           !_wcsicmp(profile.filename().c_str(),dest.filename().c_str()) &&
           !_wcsicmp(profile.parent_path().filename().c_str(),L"NFS Most Wanted");
}

inline fs::path Recovery(const fs::path& dest) {
    return dest.parent_path().parent_path().parent_path() / L"Unlimited Save Recovery" /
           L"profiles" / dest.filename();
}

inline bool MakeDirectory(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir,ec);
    return !ec;
}

inline bool Lock(const fs::path& dir, File& file) {
    if (!MakeDirectory(dir)) return false;
    file.h = CreateFileW((dir/L"writer.lock").c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL,nullptr);
    return file.h != INVALID_HANDLE_VALUE;
}

struct Slot {
    fs::path path;
    ULONGLONG time{};
};
inline std::array<Slot,3> History(const fs::path& dir) {
    std::array<Slot,3> slots{};
    for (unsigned i = 0; i < slots.size(); ++i) {
        auto& slot = slots[i];
        slot.path = dir / (std::to_wstring(i+1)+L".mw");
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(slot.path.c_str(),GetFileExInfoStandard,&info))
            slot.time = (ULONGLONG(info.ftLastWriteTime.dwHighDateTime)<<32) | info.ftLastWriteTime.dwLowDateTime;
    }
    std::sort(slots.begin(),slots.end(),[](const Slot& a,const Slot& b){return a.time>b.time;});
    return slots;
}

inline bool Archive(const fs::path& dir,const Bytes& data) {
    auto slots = History(dir);
    Bytes existing;
    if (slots[0].time && Read(slots[0].path,existing)==ERROR_SUCCESS && existing==data) return true;
    return MakeDirectory(dir) && AtomicWrite(slots.back().path,data);
}

inline fs::path Unique(const fs::path& dir,const wchar_t* stem) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER stamp{};
    stamp.HighPart=now.dwHighDateTime;stamp.LowPart=now.dwLowDateTime;
    return dir/(std::wstring(stem)+std::to_wstring(stamp.QuadPart)+L"-"+std::to_wstring(GetCurrentProcessId()));
}

inline unsigned CompatibleDatabase(unsigned id) {
    return id==0x8aed5b89 || id==0x2a3300eb ? 0x0b28303d : id;
}

struct Layout {
    unsigned database{};
    unsigned payloadSize{};
    bool operator==(const Layout& other) const {
        return database==other.database && payloadSize==other.payloadSize;
    }
};

inline fs::path ConfirmedDirectory(const fs::path& history, Layout layout) {
    wchar_t key[32]{};
    std::swprintf(key,32,L"%08x-%u",layout.database,layout.payloadSize);
    return history/L"loaded"/key;
}

inline bool FitsLayout(const Bytes& image,Layout layout) {
    if (!layout.payloadSize) return true;
    if (image.size()<36 || image.size()-36!=layout.payloadSize) return false;
    // This offset is verified for the retail-sized PC container only.
    return image.size()!=63596 || CompatibleDatabase(Word(image.data()+0x5a2d))==layout.database;
}

// Caller holds the profile lock. Repeated conflicts with identical contents
// should keep just one file, even when a mod retries an autosave every frame.
inline bool PreserveVersion(const fs::path& dir,const Bytes& data) {
    BYTE digest[32]{};
    DWORD size=sizeof(digest);
    HCRYPTPROV provider{};
    HCRYPTHASH hash{};
    if (!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) return false;
    bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash) &&
            CryptHashData(hash,data.data(),static_cast<DWORD>(data.size()),0) &&
            CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0);
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider,0);
    if (!ok || size!=sizeof(digest)) return false;
    static constexpr wchar_t hex[]=L"0123456789abcdef";
    std::wstring name;
    for (BYTE b:digest) { name+=hex[b>>4]; name+=hex[b&15]; }
    const auto file=dir/(name+L".mw");
    Bytes prior;
    const DWORD error=Read(file,prior);
    if (error==ERROR_SUCCESS) return prior==data;
    return (error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND) &&
           MakeDirectory(dir) && AtomicWrite(file,data);
}

inline void RecordLoaded(const fs::path& dest,Layout layout,const Bytes& image) {
    if (!layout.payloadSize || !FitsLayout(image,layout) || Check(image)!=Integrity::Valid) return;
    File lock;
    Bytes disk;
    const auto recovery=Recovery(dest);
    if (!Lock(recovery,lock) || Read(dest,disk)!=ERROR_SUCCESS || disk!=image) return;
    const auto dir=ConfirmedDirectory(recovery/L"history",layout);
    if (!MakeDirectory(dir)) return;
    const auto first=dir/L"first.mw";
    const auto latest=dir/L"latest.mw";
    Bytes prior;
    if (Read(first,prior)==ERROR_FILE_NOT_FOUND && !AtomicWrite(first,image)) return;
    if (Read(latest,prior)!=ERROR_SUCCESS || prior!=image) AtomicWrite(latest,image);
}

struct SaveExpectation {
    const Bytes* primary{};
    bool fromCheckpoint{};
    bool rejected{};
};

inline bool Commit(const fs::path& dest,const Bytes& next,SaveExpectation expected={}) {
    if (Check(next)!=Integrity::Valid) return false;
    const auto recovery=Recovery(dest);
    const auto history=recovery/L"history";
    File lock;
    if (!Lock(recovery,lock)) return false;
    Bytes old;
    const DWORD error=Read(dest,old);
    // A new profile can legitimately reuse the name of one deleted in the
    // same session. Its prior history is retired below.
    if (error==ERROR_SUCCESS && (expected.rejected || (expected.primary && old!=*expected.primary))) {
        PreserveVersion(history/L"conflicts",next);
        return false;
    }
    if (error==ERROR_SUCCESS) {
        const auto status=Check(old);
        if (status==Integrity::Valid) {
            const bool differentLayout=old.size()!=next.size() ||
                (next.size()==63596 && CompatibleDatabase(Word(old.data()+0x5a2d))!=
                                       CompatibleDatabase(Word(next.data()+0x5a2d)));
            if (differentLayout && !expected.fromCheckpoint) {
                PreserveVersion(history/L"conflicts",next);
                return false;
            }
            if (expected.fromCheckpoint && old!=next && !PreserveVersion(history/L"displaced",old))
                return false;
            if (!Archive(history,old)) return false;
            if (old==next) return true;
        } else if (status!=Integrity::Damaged || !AtomicWrite(Unique(recovery,L"damaged-"),old)) return false;
    } else if (error==ERROR_FILE_NOT_FOUND) {
        // Recreating a deleted profile starts a new recovery history.
        if (GetFileAttributesW(history.c_str())!=INVALID_FILE_ATTRIBUTES &&
            !MoveFileExW(history.c_str(),Unique(recovery,L"retired-").c_str(),MOVEFILE_WRITE_THROUGH)) return false;
        if (!Archive(history,next)) return false;
    } else return false;
    if (!AtomicWrite(dest,next)) return false;
    // The primary is durable at this point. A failed extra snapshot must not
    // report a failed save after the player's progress has already committed.
    Archive(history,next);
    return true;
}

inline void Recover(const fs::path& dest,Layout layout={}) {
    Bytes data;
    if (Read(dest,data)!=ERROR_SUCCESS) return;
    auto status=Check(data);
    if (status!=Integrity::Valid && status!=Integrity::Damaged) return;
    const auto dir=Recovery(dest);
    auto history=dir/L"history";
    File lock;
    if (!Lock(dir,lock) || Read(dest,data)!=ERROR_SUCCESS) return;
    status=Check(data);
    if (status==Integrity::Valid) {
        Archive(history,data);
        return;
    }
    if (status!=Integrity::Damaged) return;
    auto restore=[&](const fs::path& path) {
        Bytes backup;
        if (Read(path,backup)!=ERROR_SUCCESS || Check(backup)!=Integrity::Valid || !FitsLayout(backup,layout))
            return false;
        if (AtomicWrite(Unique(dir,L"damaged-"),data)) AtomicWrite(dest,backup);
        return true;
    };
    for (const auto& slot:History(history)) {
        if (slot.time && restore(slot.path)) return;
    }
    if (!layout.payloadSize) return;
    const auto confirmed=ConfirmedDirectory(history,layout);
    if (!restore(confirmed/L"latest.mw")) restore(confirmed/L"first.mw");
}
} // namespace unlimited_saves
