// The test maps the retail image as data. It uses the actual FEng setter and the
// patched call target, with stand-ins only for texture lookup and final drawing.
#include "../vram_probe.cpp"

namespace {
using namespace unlimited_language;
using Storage=std::array<DWORD,40>;
Storage texture{}, object{}, screen{}, dal{}, options{};
bool found=true;
unsigned draws=0;

void* __cdecl Lookup(DWORD hash,short load,short fallback) {
    return found && !load && !fallback && hash==languageChevronTexture ? texture.data() : nullptr;
}
void __fastcall Draw(void*,void*,void*,void*) { ++draws; }

template<class T> bool Bind(std::uintptr_t rva,T fn) {
    std::uint8_t jump[5]{0xe9};
    const auto rel=reinterpret_cast<std::uintptr_t>(fn)-reinterpret_cast<std::uintptr_t>(engine)-rva-5;
    std::memcpy(jump+1,&rel,4);
    return WriteGameBytes(rva,jump,sizeof(jump));
}

bool Run(const wchar_t* path) {
    const auto file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (file==INVALID_HANDLE_VALUE) return false;
    auto mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE,0,0,nullptr);
    CloseHandle(file);
    if (!mapping) return false;
    auto* view=static_cast<const BYTE*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
    CloseHandle(mapping);
    if (!view) return false;
    engine=static_cast<HMODULE>(VirtualAlloc(nullptr,0x678e4e,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if (!engine) { UnmapViewOfFile(view); return false; }
    for (auto range : {std::pair<unsigned,unsigned>{0x115b90,32},{0x1a46fd,5}}) {
        std::memcpy(&GameValue<BYTE>(range.first),view+range.first,range.second);
    }
    UnmapViewOfFile(view);
    if (!RedirectCall(0x1a46fd,0x1a45b0,reinterpret_cast<std::uintptr_t>(&RenderLanguageChevron)) ||
        !Bind(0x103400,&Lookup) || !Bind(0x1a45b0,&Draw)) return false;
    GameValue<void*>(0x51cfac)=screen.data();
    GameValue<void*>(0x51cf90)=dal.data();
    HudField<void*>(dal.data(),0x10)=options.data();
    const auto invoke=GameFunction<void (__thiscall*)(void*,void*,void*)>(0x1a4702+GameValue<int>(0x1a46fe));
    unsigned cases=0;
    auto check=[&](const char* package,DWORD name,DWORD type,int categoryIndex,DWORD start,DWORD expected) {
        HudField<const char*>(screen.data(),0xc)=package;
        HudField<int>(options.data(),0x24)=categoryIndex;
        object.fill(0xcccccccc);
        HudField<DWORD>(object.data(),0x18)=type;
        HudField<DWORD>(object.data(),0x10)=name;
        HudField<DWORD>(object.data(),0x24)=start;
        auto expectedObject=object;
        HudField<DWORD>(expectedObject.data(),0x24)=expected;
        if (start!=expected) HudField<DWORD>(expectedObject.data(),0x1c)|=0x02400000;
        invoke(nullptr,object.data(),nullptr);
        return ++cases==draws && object==expectedObject;
    };
    HudField<DWORD>(texture.data(),0x24)=languageChevronTexture;
    bool ok=true;
    for (DWORD arrow : {leftArrowRowOne,rightArrowRowOne}) {
        ok &= check("Options.fng",arrow,1,category,thinArrowTexture,languageChevronTexture);
        ok &= check("Options.fng",arrow,1,category,languageChevronTexture,languageChevronTexture);
        ok &= check("Options.fng",arrow,1,0,languageChevronTexture,thinArrowTexture);
        ok &= check("Options.fng",arrow,1,3,thinArrowTexture,thinArrowTexture);
        ok &= check("Options.fng",arrow+1,1,category,thinArrowTexture,thinArrowTexture);
        ok &= check("Options.fng",arrow,2,category,thinArrowTexture,thinArrowTexture);
        ok &= check("Pause_Options.fng",arrow,1,category,thinArrowTexture,thinArrowTexture);
        ok &= check("Options.fng",arrow,1,category,0x12345678,0x12345678);
        found=false;
        ok &= check("Options.fng",arrow,1,category,thinArrowTexture,thinArrowTexture);
        found=true;
    }
    std::printf("Language chevrons: %u requests, %u forwarded, all other FE object bytes preserved: %s\n",
                cases,draws,ok ? "yes" : "no");
    VirtualFree(engine,0,MEM_RELEASE);
    return ok;
}
}

int wmain(int argc,wchar_t** argv) { return argc==2 && Run(argv[1]) ? 0 : 1; }
