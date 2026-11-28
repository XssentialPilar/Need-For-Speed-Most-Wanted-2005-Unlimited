// Run from the project root. Maps the retail PC image into this test only.
#include "../vram_probe.cpp"

namespace {
using Storage = std::array<std::uint32_t,64>;
Storage option{}, optionData{}, left{}, leftData{}, right{}, rightData{}, frontend{}, cache{}, pool{}, font{}, white{};
Storage dal{}, options{}, firstLabel{}, firstLabelData{};
const std::uint16_t testString[]{'V','a','l','u','e',0};
const OptionColor nativeColor{104,120,162,194};
float textWidth = 400;
unsigned glyphCalls = 0, plateCalls = 0, measureCalls = 0, arrowCalls = 0;
bool retainWhite = true, valid = true;
D3DMATRIX drawn{}, identity{};
OptionColor ink{};
OptionPlate bounds{};

void* __cdecl FindArrow(const char*,std::uint32_t hash) {
    if (hash == firstOptionName) return firstLabel.data();
    ++arrowCalls;
    if (hash == firstLeftArrow || hash == firstLeftArrow+5) return left.data();
    if (hash == firstRightArrow || hash == firstRightArrow+5) return right.data();
    return nullptr;
}

float __fastcall Measure(void* f,void*,const std::uint16_t* text,unsigned flags,unsigned maxWidth,bool wrap) {
    valid &= f == font.data() && text == testString && flags == 0x20 && !maxWidth && !wrap;
    ++measureCalls;
    return textWidth;
}

void* __cdecl FindWhite(std::uint32_t hash,int load,int fallback) {
    valid &= hash == 0x0e55b9c1 && !load && !fallback;
    return retainWhite ? white.data() : nullptr;
}

void __fastcall Plate(void* c,void*,float x0,float y0,float x1,float y1,float z,float s0,float t0,float s1,float t1,
                      const std::uint32_t* colors,void* texture,void* renderInfo) {
    valid &= c == cache.data() && !std::memcmp(&HudField<D3DMATRIX>(c,0x30),&identity,sizeof(identity)) &&
        !glyphCalls && x0<x1 && y0<y1 && z==1 && s0==0 && t0==0 && s1==1 && t1==1 &&
        colors[0]!=colors[1] && (colors[0]>>24)==nativeColor.alpha*196u/255 &&
        texture==white.data() && renderInfo==frontend.data();
    bounds={x0,y0,x1,y1}; ++plateCalls;
}

void __fastcall Glyphs(void* f,void*,const OptionColor& color,const std::uint16_t*,void* object,
                       D3DMATRIX* matrix,void* c,void* info) {
    valid &= f == font.data() && object == option.data() && c == cache.data() && info == frontend.data();
    drawn=*matrix; ink=color; ++glyphCalls;
}

template<typename T> bool Fixture(std::uintptr_t offset,T entry) {
    const auto delta = reinterpret_cast<std::uintptr_t>(entry)-reinterpret_cast<std::uintptr_t>(engine)-offset-5;
    std::array<std::uint8_t,5> jump{0xe9};
    std::memcpy(jump.data()+1,&delta,4);
    return WriteGameBytes(offset,jump.data(),jump.size());
}

bool LoadRetail(const wchar_t* file) {
    const auto handle=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (handle==INVALID_HANDLE_VALUE) return false;
    const auto image=CreateFileMappingW(handle,nullptr,PAGE_READONLY|SEC_IMAGE,0,0,nullptr);
    CloseHandle(handle);
    if (!image) return false;
    auto* view=MapViewOfFile(image,FILE_MAP_READ,0,0,0);
    CloseHandle(image);
    if (!view || !IsTrackedBuild(static_cast<HMODULE>(view))) return false;
    engine=static_cast<HMODULE>(VirtualAlloc(nullptr,0x678e4e,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if (engine) std::memcpy(&GameValue<std::uint8_t>(0x1a45a1),static_cast<char*>(view)+0x1a45a1,5);
    UnmapViewOfFile(view);
    return engine && InstallResponsiveOptionLayout() && !InstallResponsiveOptionLayout() &&
        Fixture(0x124850,&FindArrow) && Fixture(0x186130,&Measure) && Fixture(0x103400,&FindWhite) &&
        Fixture(0x182820,&Plate) && Fixture(0x19ac60,&Glyphs);
}

void SendRow(unsigned hash, unsigned format, const std::uint16_t* text = testString) {
    glyphCalls=plateCalls=measureCalls=arrowCalls=0;
    HudField<unsigned>(option.data(),0x10)=hash;
    HudField<unsigned>(option.data(),0x6c)=format;
    auto source=identity;
    source._11=.37f; source._22=.9f; source._41=hash==firstOptionName+5 ? 230.f : 370.f; source._42=200;
    const auto original=source;
    const auto cached=cache, sourceObject=option, nativeData=optionData;
    // Resolve the game's patched rel32 instruction, not an independent test entry.
    const auto hook=GameFunction<void(__thiscall*)(void*,const OptionColor&,const std::uint16_t*,void*,
                                      D3DMATRIX*,void*,void*)>(0x1a45a6+GameValue<std::int32_t>(0x1a45a2));
    hook(font.data(),nativeColor,text,option.data(),&source,cache.data(),frontend.data());
    valid &= glyphCalls==1 && nativeData==optionData && sourceObject==option && cached==cache &&
             !std::memcmp(&source,&original,sizeof(source));
}

bool RunRows() {
    identity._11=identity._22=identity._33=identity._44=1;
    GameValue<void*>(0x51cfac)=frontend.data(); GameValue<void*>(0x51cfa0)=pool.data();
    GameValue<void*>(0x51cf90)=dal.data();
    HudField<void*>(dal.data(),0x10)=options.data();
    auto& category=HudField<int>(options.data(),0x24);
    HudField<int>(pool.data(),0x2c)=840;
    HudField<unsigned>(white.data(),0x24)=0x0e55b9c1;
    HudField<void*>(font.data(),8)=white.data(); HudField<float>(font.data(),0x24)=20;
    HudField<unsigned>(option.data(),0x18)=2; HudField<unsigned>(option.data(),0x1c)=0x20;
    HudField<void*>(option.data(),0x2c)=optionData.data();
    HudField<float>(optionData.data(),0x3c)=.9f; HudField<float>(optionData.data(),0x1c)=50;
    HudField<unsigned>(firstLabel.data(),0x18)=2;
    HudField<void*>(firstLabel.data(),0x2c)=firstLabelData.data();
    HudField<float>(firstLabelData.data(),0x1c)=8;
    HudField<unsigned>(left.data(),0x18)=1; HudField<unsigned>(right.data(),0x18)=1;
    HudField<void*>(left.data(),0x2c)=leftData.data(); HudField<void*>(right.data(),0x2c)=rightData.data();
    HudField<float>(leftData.data(),0x1c)=-34; HudField<float>(rightData.data(),0x1c)=133;
    HudField<float>(leftData.data(),0x38)=24; HudField<float>(rightData.data(),0x38)=-24;
    HudField<float>(cache.data(),0x30)=.73f;
    unsigned cases=0;
    constexpr unsigned sizes[][2]{{640,480},{1920,1200},{1920,1080},{3440,1440},{5120,1440},
        {1280,1024},{720,720},{1080,1920},{1200,120},{120,1200}};
    for (const auto* name : optionPackages) {
        HudField<const char*>(frontend.data(),0xc)=name;
        for (auto& size : sizes) {
            activeLayout=MakeAspectLayout(size[0],size[1]);
            SendRow(firstOptionValue+5,1);
            valid &= measureCalls==1 && arrowCalls==2 && !plateCalls && drawn._11==drawn._22 &&
                drawn._11*textWidth <= 130.01f && !std::memcmp(&ink,&nativeColor,sizeof(ink));
            SendRow(firstOptionName+5,2);
            valid &= measureCalls==1 && !arrowCalls && !plateCalls && drawn._11==drawn._22 &&
                std::fabs(drawn._41-188)<.01f &&
                !std::memcmp(&ink,&nativeColor,sizeof(ink));
            const auto farEdge=drawn._41 - drawn._11*textWidth;
            valid &= UiToClip(farEdge,640,activeLayout.hudX) > -.985f;
            cases+=2;
            category=unlimited_language::category;
            SendRow(firstOptionValue,1);
            if (!_stricmp(name,"Options.fng")) {
                valid &= plateCalls==1 && ink.alpha==nativeColor.alpha && ink.red>nativeColor.red &&
                    std::fabs((bounds.right-bounds.left)-138)<.01f &&
                    std::fabs(drawn._42+drawn._22*10 - (bounds.top+bounds.bottom)/2)<.01f;
                for (auto pos : {bounds.left,bounds.right})
                    valid &= std::fabs(UiToClip(pos,640,activeLayout.hudX)) < .98f;
            } else valid &= !plateCalls && !std::memcmp(&ink,&nativeColor,sizeof(ink));
            category=0;
            SendRow(firstOptionValue,1); valid &= !plateCalls;
        }
    }
    HudField<const char*>(frontend.data(),0xc)="Options.fng";
    category=unlimited_language::category;
    std::swap(HudField<float>(leftData.data(),0x1c),HudField<float>(rightData.data(),0x1c));
    SendRow(firstOptionValue,1); valid &= plateCalls==1 && std::fabs(bounds.right-bounds.left-138)<.01f;
    retainWhite=false; SendRow(firstOptionValue,1);
    valid &= measureCalls==1 && !plateCalls && !std::memcmp(&ink,&nativeColor,sizeof(ink));
    retainWhite=true; HudField<int>(pool.data(),0x18)=830;
    SendRow(firstOptionValue,1); valid &= !plateCalls;
    HudField<int>(pool.data(),0x18)=0;
    HudField<const char*>(frontend.data(),0xc)="Subtitles.fng";
    SendRow(firstOptionValue+5,1); valid &= !plateCalls && !arrowCalls && !measureCalls && drawn._11==.37f;
    HudField<const char*>(frontend.data(),0xc)="Options.fng";
    SendRow(firstOptionValue+5,17); valid &= !measureCalls && drawn._11==.37f;
    const std::uint16_t wrapped[]{'A','^','B',0};
    SendRow(firstOptionValue+5,1,wrapped); valid &= !measureCalls && drawn._11==.37f;
    SendRow(0x12345678,1); valid &= !measureCalls && drawn._11==.37f;
    std::printf("%u settings rows; 7 native packages, 10 viewports, focus alpha, arrow clearance, text aspect, native fallbacks: %s\n",
                cases,valid?"PASS":"FAIL");
    return valid;
}
}

int main() {
    if (!LoadRetail(L"Juego/speed.exe")) { std::puts("Retail hook fixture unavailable"); return 2; }
    const bool ok=RunRows();
    VirtualFree(engine,0,MEM_RELEASE); engine=nullptr;
    return ok?0:1;
}

