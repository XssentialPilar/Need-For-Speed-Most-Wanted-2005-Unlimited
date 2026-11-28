// Usage: profile_store.exe <valid PC save> <new, nonexistent test directory>
#include "../nfs_save_store.hpp"
#include <cstdio>
#include <stdexcept>

using namespace unlimited_saves;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

Bytes Contents(const fs::path& path) {
    Bytes result;
    Require(Read(path,result)==ERROR_SUCCESS,"could not read fixture");
    return result;
}

Bytes WithPrefix(Bytes bytes, unsigned char salt) {
    bytes[36]^=salt;
    unsigned crc=EaChecksum(bytes.data()+36,bytes.size()-36);
    std::memcpy(bytes.data()+20,&crc,4);
    crc=EaChecksum(bytes.data(),24);
    std::memcpy(bytes.data()+24,&crc,4);
    return bytes;
}

int wmain(int argc, wchar_t** argv) {
    if (argc!=3) return 2;
    try {
        const fs::path root=fs::absolute(argv[2]);
        Require(fs::create_directory(root),"test root must be new");
        const fs::path profile=root/L"NFS Most Wanted"/L"Driver"/L"Driver";
        const fs::path recovery=Recovery(profile);
        fs::create_directories(profile.parent_path());
        const Bytes original=Contents(argv[1]);
        Require(Check(original)==Integrity::Valid,"source fixture invalid");

        // Change only the 16-byte prefix outside the native gameplay MD5.
        const Bytes newer=WithPrefix(original,1);
        Require(Check(newer)==Integrity::Valid,"second fixture invalid");
        Require(Commit(profile,original),"initial commit");
        Require(Commit(profile,newer) && Contents(profile)==newer,"replacement");
        auto partial=newer;
        partial.resize(1700);
        Require(!Commit(profile,partial) && Contents(profile)==newer,"partial commit");
        auto bitFlip=newer;
        bitFlip[600]^=1;
        Require(!Commit(profile,bitFlip) && Contents(profile)==newer,"invalid payload");
        unsigned crc=EaChecksum(bitFlip.data()+36,bitFlip.size()-36);
        std::memcpy(bitFlip.data()+20,&crc,4);
        crc=EaChecksum(bitFlip.data(),24);
        std::memcpy(bitFlip.data()+24,&crc,4);
        Require(Check(bitFlip)==Integrity::Damaged,"bad native MD5 with valid container CRCs");
        {
            File winner;
            Require(Lock(recovery,winner),"exclusive writer");
            Require(!Commit(profile,original) && Contents(profile)==newer,"contending writer");
        }
        {
            File held(CreateFileW(profile.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
            Require(held.h!=INVALID_HANDLE_VALUE,"hold destination");
            Require(!Commit(profile,original) && Contents(profile)==newer,"rename failure");
        }
        auto pending=profile;pending+=L".pending";
        Require(AtomicWrite(pending,partial),"stage interrupted write");
        Recover(profile);
        Require(Contents(profile)==newer,"interrupted stage changed live file");
        Require(AtomicWrite(profile,partial),"truncate isolated fixture");
        Recover(profile);
        Require(Contents(profile)==newer,"latest verified generation");
        bool evidence=false;
        for (const auto& e:fs::directory_iterator(recovery))
            if (e.path().filename().wstring().find(L"damaged-")==0 && Contents(e.path())==partial) evidence=true;
        Require(evidence,"damaged source retained");
        auto slots=History(recovery/L"history");
        Require(AtomicWrite(slots[0].path,bitFlip),"damage latest backup");
        Require(AtomicWrite(profile,partial),"damage primary");
        Recover(profile);
        Require(Contents(profile)==original,"fall back past damaged backup");
        fs::remove(profile);
        Recover(profile);
        Require(!fs::exists(profile),"intentional deletion");
        Require(Commit(profile,newer),"recreate profile");
        size_t active=0;
        for (const auto& slot:History(recovery/L"history")) if (slot.time) ++active;
        Require(active==1,"recreated profile inherited old history");
        auto unknown=newer;std::fill(unknown.begin(),unknown.begin()+36,0x57);
        Require(AtomicWrite(profile,unknown),"stage unsupported format");
        Recover(profile);
        Require(Contents(profile)==unknown && !Commit(profile,newer),"unsupported format preservation");
        const auto driver=root/L"NFS Most Wanted"/L"Checkpoint"/L"Checkpoint";
        fs::create_directories(driver.parent_path());
        const auto history=Recovery(driver)/L"history";
        Require(original.size()==63596,"expected retail-sized fixture");
        const Layout layout{CompatibleDatabase(Word(original.data()+0x5a2d)),unsigned(original.size()-36)};
        const auto loaded=ConfirmedDirectory(history,layout);
        Require(Commit(driver,original),"checkpoint profile");
        const Layout incompatible{layout.database^1u,layout.payloadSize};
        RecordLoaded(driver,incompatible,original);
        Require(!fs::exists(ConfirmedDirectory(history,incompatible)),"wrong database confirmed");
        RecordLoaded(driver,layout,original);
        Require(Commit(driver,newer),"second profile revision");
        RecordLoaded(driver,layout,newer);
        for (unsigned char revision=2;revision<=7;++revision)
            Require(Commit(driver,WithPrefix(original,revision)),"rolling history");
        RecordLoaded(driver,layout,original);
        Require(Contents(loaded/L"first.mw")==original && Contents(loaded/L"latest.mw")==newer,
                "unloaded saves or stale observation advanced confirmed points");
        for (const auto& slot:History(history)) Require(AtomicWrite(slot.path,partial),"damage rolling slot");
        Require(AtomicWrite(driver,partial),"damage active save");
        Recover(driver,incompatible);
        Require(Contents(driver)==partial,"foreign database checkpoint restored");
        Recover(driver,layout);
        Require(Contents(driver)==newer,"recover most recent accepted save");
        Require(AtomicWrite(loaded/L"latest.mw",bitFlip) && AtomicWrite(driver,partial),"damage latest");
        Recover(driver,layout);
        Require(Contents(driver)==original,"recover first accepted save");
        const auto candidate=WithPrefix(original,8);
        for (int attempt=0;attempt<2;++attempt)
            Require(!Commit(driver,candidate,{&newer,false,false}) && Contents(driver)==original,
                    "concurrent edit overwritten");
        Require(!Commit(driver,candidate,{&original,false,true}),"save after rejected load");
        size_t conflicts=0;
        for (const auto& entry:fs::directory_iterator(history/L"conflicts")) {
            Require(Contents(entry.path())==candidate,"wrong conflict copy");
            ++conflicts;
        }
        Require(conflicts==1,"duplicate conflict copy");
        Require(Commit(driver,candidate,{&original,true,false}),"commit after fallback");
        bool displaced=false;
        for (const auto& entry:fs::directory_iterator(history/L"displaced"))
            if (Contents(entry.path())==original) displaced=true;
        Require(displaced && Contents(driver)==candidate,"displaced original lost");
        fs::remove(driver);
        Require(Commit(driver,newer) && !fs::exists(loaded),"old checkpoints reused after deletion");
        std::puts("PASS: pinned first/latest, rotation, fallback, conflicting edits, deduplication, displaced file, checkpoint retirement");
        std::puts("PASS: create, replace, validation, contention, failed publish, interrupted stage, recovery, damaged backup, deletion, recreation, unsupported format");
        return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
