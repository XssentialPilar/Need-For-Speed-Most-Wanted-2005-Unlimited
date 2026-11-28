#pragma once

#include "nfs_save_store.hpp"
#include <memory>
#include <mutex>

namespace unlimited_saves {

// These three PC 1.3 vaults have identical career and garage layouts. Only
// the visual tables differ. Other database IDs retain the retail comparison.
extern "C" __attribute__((used,noinline)) unsigned MwProfileDatabase(unsigned id) {
    return CompatibleDatabase(id);
}

extern "C" __attribute__((used,noinline)) int MwProfileMatches(unsigned saved,unsigned current) {
    return MwProfileDatabase(saved)==MwProfileDatabase(current);
}

__attribute__((naked)) void CompareDatabase() {
    asm("movl 0x91cf90,%ecx\n"
        "pushl %eax\npushl %ecx\npushl %edx\n"
        "pushl 8(%ecx)\npushl %esi\ncall _MwProfileMatches\n"
        "addl $8,%esp\ncmpl $1,%eax\npopl %edx\npopl %ecx\npopl %eax\nret");
}

__attribute__((naked)) void SerializeDatabase() {
    asm("pushfl\npushl %eax\npushl %ecx\nmovl 0x91cf90,%edx\n"
        "pushl 8(%edx)\ncall _MwProfileDatabase\naddl $4,%esp\nmovl %eax,%edx\n"
        "popl %ecx\npopl %eax\npopfl\nret");
}

using NativeOpen = int (__cdecl*)(const char*,int,int);
extern "C" { __attribute__((used)) NativeOpen mwOriginalSaveOpen=nullptr; }

using Snapshot=std::shared_ptr<const Bytes>;

struct ReadTicket {
    fs::path path;
    Layout layout;
    Snapshot primary, selected;
    unsigned source{}; // 0 primary, 1 last confirmed, 2 first confirmed
};

struct Observation {
    fs::path path;
    Layout layout;
    Snapshot primary;
    unsigned rejectedMask{};
    bool fromCheckpoint{};

    bool matches(const fs::path& other) const {
        return primary && !_wcsicmp(path.c_str(),other.c_str());
    }
};

std::mutex profileMutex;
ReadTicket pendingRead;
Observation lastLoad, lastWrite;

Layout CurrentLayout() {
    auto* manager=GameValue<unsigned char*>(0x51cf90);
    if (!manager) return {};
    const auto size=GameFunction<unsigned(__thiscall*)(void*,int)>(0x17fd90)(manager,0);
    if (!size || size>maxContainer-36) return {};
    return {CompatibleDatabase(Word(manager+8)),size};
}

std::string NativeFilename(const fs::path& path) {
    const UINT page=AreFileApisANSI()?CP_ACP:CP_OEMCP;
    char narrow[MAX_PATH];
    wchar_t wide[MAX_PATH];
    if (!WideCharToMultiByte(page,0,path.c_str(),-1,narrow,MAX_PATH,nullptr,nullptr) ||
        !MultiByteToWideChar(page,0,narrow,-1,wide,MAX_PATH) || path.native()!=wide) return {};
    return narrow;
}

int __cdecl OpenForLoad(const char* path,int flags,int mode) {
    try {
        fs::path dest;
        if (ProfilePath(path,dest)) {
            const auto layout=CurrentLayout();
            std::lock_guard<std::mutex> guard(profileMutex);
            pendingRead={};
            Recover(dest,layout);
            auto primary=std::make_shared<Bytes>();
            if (Read(dest,*primary)==ERROR_SUCCESS && Check(*primary)==Integrity::Valid) {
                ReadTicket ticket{dest,layout,primary,primary,0};
                const bool retry=lastLoad.matches(dest) && lastLoad.layout==layout &&
                                 *lastLoad.primary==*primary && (lastLoad.rejectedMask&1);
                if (retry && layout.payloadSize) {
                    const auto dir=ConfirmedDirectory(Recovery(dest)/L"history",layout);
                    for (unsigned i=1; i<=2; ++i) {
                        if (lastLoad.rejectedMask&(1u<<i)) continue;
                        const auto file=dir/(i==1?L"latest.mw":L"first.mw");
                        auto candidate=std::make_shared<Bytes>();
                        if (Read(file,*candidate)!=ERROR_SUCCESS || *candidate==*primary ||
                            !FitsLayout(*candidate,layout) || Check(*candidate)!=Integrity::Valid) continue;
                        const auto narrow=NativeFilename(file);
                        if (narrow.empty()) continue;
                        const int fd=mwOriginalSaveOpen(narrow.c_str(),flags,mode);
                        if (fd<0) continue;
                        ticket.selected=std::move(candidate);
                        ticket.source=i;
                        pendingRead=std::move(ticket);
                        return fd;
                    }
                }
                const int fd=mwOriginalSaveOpen(path,flags,mode);
                if (fd>=0) {
                    pendingRead=std::move(ticket);
                    // Some layouts are rejected by the container loader before
                    // it ever calls the profile decoder. The first attempt still
                    // goes through the game, which can accept a modded layout.
                    if (layout.payloadSize && !FitsLayout(*primary,layout) && !retry)
                        lastLoad={dest,layout,primary,1,false};
                }
                return fd;
            }
        }
    } catch (...) { /* The retail loader can still report the file's status. */ }
    return mwOriginalSaveOpen(path,flags,mode);
}

using DecodeProfile=bool(__thiscall*)(void*,void*,unsigned,int,int);
bool __fastcall ObserveDecodedProfile(void* self,void*,void* payload,unsigned size,int settings,int player) {
    ReadTicket ticket;
    {
        std::lock_guard<std::mutex> guard(profileMutex);
        if (pendingRead.selected && payload && pendingRead.selected->size()==size_t(size)+36 &&
            std::memcmp(pendingRead.selected->data()+36,payload,size)==0)
            ticket=std::move(pendingRead);
    }
    const bool accepted=GameFunction<DecodeProfile>(0x1acb70)(self,payload,size,settings,player);
    if (ticket.primary) {
        try {
            std::lock_guard<std::mutex> guard(profileMutex);
            unsigned mask=0;
            if (lastLoad.matches(ticket.path) && lastLoad.layout==ticket.layout &&
                *lastLoad.primary==*ticket.primary) mask=lastLoad.rejectedMask;
            if (!accepted) mask |= 1u | (1u<<ticket.source);
            else if (!ticket.source) mask=0;
            lastLoad={ticket.path,ticket.layout,ticket.primary,mask,accepted && ticket.source!=0};
            if (lastWrite.matches(ticket.path)) lastWrite={};
            if (accepted && !ticket.source) RecordLoaded(ticket.path,ticket.layout,*ticket.primary);
        } catch (...) { /* File-system errors must not change the game's own verdict. */ }
    }
    return accepted;
}

extern "C" __attribute__((used,noinline)) int MwCommitProfile(unsigned char* job,const char* path) {
    bool success=false;
    try {
        fs::path dest;
        if (!ProfilePath(path,dest)) return -1;
        const auto* header=job+0x10c;
        const auto* meta=*reinterpret_cast<const unsigned char* const*>(job+0x128);
        const auto* payload=*reinterpret_cast<const unsigned char* const*>(job+0x12c);
        const unsigned total=Word(header+4), metaSize=Word(header+8), size=Word(header+12);
        if (meta && payload && metaSize==8 && size<=maxContainer-36 && total==size+36) {
            Bytes image(header,header+28);
            image.reserve(total);
            image.insert(image.end(),meta,meta+8);
            image.insert(image.end(),payload,payload+size);
            std::lock_guard<std::mutex> guard(profileMutex);
            auto& observation=lastWrite.matches(dest)?lastWrite:lastLoad;
            SaveExpectation expected{};
            if (observation.matches(dest))
                expected={observation.primary.get(),observation.fromCheckpoint,
                          observation.rejectedMask!=0 && !observation.fromCheckpoint};
            // Allocate the observation before publishing; once committed the
            // game must receive success even if a later allocation would fail.
            Observation updated{dest,{},std::make_shared<Bytes>(std::move(image)),0,false};
            success=Commit(dest,*updated.primary,expected);
            if (success) {
                if (lastLoad.matches(dest)) lastLoad={};
                lastWrite=std::move(updated);
            }
        }
    } catch (...) { /* Surface the native write-error result. */ }
    *reinterpret_cast<unsigned*>(job+0x108)=0;
    *reinterpret_cast<unsigned*>(job+0x2f4)=success?0:1;
    *reinterpret_cast<unsigned*>(job+0x2f8)=success?0:7;
    return 0;
}

// At this single retail call the native state machine has assembled all
// three container chunks and the destination path. Preserve its callbacks,
// overwrite prompt and async state transitions. Resume after its CRT writes.
__attribute__((naked)) void StoreProfile() {
    asm("pushl 4(%esp)\npushl %ebx\ncall _MwCommitProfile\naddl $8,%esp\n"
        "testl %eax,%eax\njs 1f\npopl %eax\naddl $0x34,%esp\n"
        "addl $0xf6,%eax\njmp *%eax\n1: jmp *_mwOriginalSaveOpen");
}

// A background save can finish without an owning frontend screen. The native
// notifier accepts a null target and dispatches to the active screen list.
__attribute__((naked)) void SaveNotificationTarget() {
    asm("movl 0xcc(%edx),%eax\npushfl\nxorl %ecx,%ecx\ntestl %eax,%eax\n"
        "jz 1f\nmovl 0x10(%eax),%ecx\n1: popfl\nret");
}

inline bool Install() {
    constexpr std::uint8_t compare[]={0x8b,0x0d,0x90,0xcf,0x91,0x00,0x3b,0x71,0x08};
    constexpr std::uint8_t storeId[]={0x8b,0x15,0x90,0xcf,0x91,0x00,0x8b,0x52,0x08};
    constexpr std::uint8_t write[]={0xe8,0x1c,0xf7,0xfe,0xff};
    constexpr std::uint8_t read[]={0xe8,0x43,0xf4,0xfe,0xff};
    constexpr std::uint8_t decode[]={0xe8,0x80,0xfd,0xff,0xff};
    constexpr std::uint8_t notificationTarget[]={0x8b,0x82,0xcc,0x00,0x00,0x00,0x8b,0x48,0x10};
    constexpr std::uint8_t done[]={0xc6,0x83,0x84,0x00,0x00,0x00,0x01};
    if (!MatchGameBytes(0x1acbf1,compare,sizeof(compare)) ||
        !MatchGameBytes(0x18fd65,storeId,sizeof(storeId)) ||
        !MatchGameBytes(0x3f506f,write,sizeof(write)) ||
        !MatchGameBytes(0x3f5348,read,sizeof(read)) ||
        !MatchGameBytes(0x1acdeb,decode,sizeof(decode)) ||
        !MatchGameBytes(0x127114,notificationTarget,sizeof(notificationTarget)) ||
        !MatchGameBytes(0x3f516a,done,sizeof(done))) return false;
    mwOriginalSaveOpen=GameFunction<NativeOpen>(0x3e4790);
    if (!InstallInlineCall(0x1acbf1,compare,&CompareDatabase)) return false;
    if (InstallInlineCall(0x18fd65,storeId,&SerializeDatabase)) {
        if (InstallInlineCall(0x3f506f,write,&StoreProfile)) {
            if (RedirectCall(0x3f5348,0x3e4790,reinterpret_cast<uintptr_t>(&OpenForLoad))) {
                if (RedirectCall(0x1acdeb,0x1acb70,reinterpret_cast<uintptr_t>(&ObserveDecodedProfile))) {
                    if (InstallInlineCall(0x127114,notificationTarget,&SaveNotificationTarget)) return true;
                    WriteGameBytes(0x1acdeb,decode,sizeof(decode));
                }
                WriteGameBytes(0x3f5348,read,sizeof(read));
            }
            WriteGameBytes(0x3f506f,write,sizeof(write));
        }
        WriteGameBytes(0x18fd65,storeId,sizeof(storeId));
    }
    WriteGameBytes(0x1acbf1,compare,sizeof(compare));
    return false;
}

} // namespace unlimited_saves
