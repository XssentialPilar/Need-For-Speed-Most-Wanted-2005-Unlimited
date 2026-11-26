#pragma once
#include "renderer-lab/scene_shadow_d3d12.hpp"

namespace {
scene_shadow::GameShadow& nativeShadowPass = *new scene_shadow::GameShadow;
std::atomic<unsigned long long> shadowBatchCount{}, shadowMeshCount{}, shadowFallbackCount{};

// An embedded bTList node lives at +0x10; the render manager contains its
// sentinel at +0. Retail 1.3 builds the entire list before the D3DX pass.
template<class T> T ShadowField(const BYTE* mesh,std::size_t offset) {
    T result{};
    std::memcpy(&result,mesh+offset,sizeof(result));
    return result;
}

void WINAPI DrawShadowList(BYTE* sentinel) noexcept {
    const auto first=ShadowField<BYTE*>(sentinel,0);
    auto stop=GameValue<BYTE>(0x588570)!=0;
    if (nativePassesActive && nativePassesAtStartup==12) try {
        thread_local std::vector<scene_shadow::Mesh> batch;
        batch.clear();
        scene_shadow::Mesh finalBinding{};
        bool supported=true;
        auto* link=first;
        for (unsigned traversed=0;link && link!=sentinel;link=ShadowField<BYTE*>(link,0)) {
            if (++traversed>4096) { supported=false; break; }
            auto* node=link-0x10;
            scene_shadow::Mesh mesh{ShadowField<IDirect3DVertexBuffer9*>(node,0x48),
                                    ShadowField<IDirect3DIndexBuffer9*>(node,0x34),
                                    ShadowField<UINT>(node,0x40),ShadowField<UINT>(node,0x30)};
            if (mesh.vertices && mesh.indices) {
                // 0x6c9aa0 sets stream 0 even for a node with no triangles.
                if (!mesh.indexCount || !mesh.vertexCount) { supported=false; break; }
                finalBinding=mesh;
                batch.push_back(mesh);
            }
            if (stop) break;
        }
        auto* device=GameValue<IDirect3DDevice9*>(0x582bdc);
        if (supported && nativeShadowPass.Draw(device,batch)) {
            device->SetStreamSource(0,finalBinding.vertices,0,12);
            device->SetIndices(finalBinding.indices);
            shadowMeshCount+=batch.size();
            ++shadowBatchCount;
            return;
        }
    } catch (...) { /* Let the engine render the complete list. */ }
    ++shadowFallbackCount;
    const auto draw=GameFunction<void (__thiscall*)(void*)>(0x2c9aa0);
    for (auto* link=first;link && link!=sentinel;link=ShadowField<BYTE*>(link,0)) {
        draw(link-0x10);
        if (GameValue<BYTE>(0x588570)) break;
    }
}

bool InstallNativeShadowList() {
    static_assert(sizeof(void*)==4);
    // Validate the entire loop, its relative draw call and both allocation
    // instructions before modifying either page. All other geometry keeps
    // its original buffer usage, including Dynamic/WRITEONLY streams.
    constexpr std::uintptr_t vb=0x2d7b4f, ib=0x2d7b87, loop=0x2d7ff4;
    constexpr BYTE vbCode[]{0x6a,0x08,0xc7,0x06,0x74,0xfb,0x8a,0x00};
    constexpr BYTE ibCode[]{0x6a,0x08,0x03,0xc0,0x50,0x51,0xff,0x52,0x6c};
    constexpr BYTE body[]{0x8b,0x03,0x85,0xc0,0x74,0x05,0x8d,0x78,0xf0,0xeb,0x02,0x33,0xff,0x83,
        0xc3,0xf0,0x3b,0xfb,0x74,0x20,0x8b,0xcf,0xe8,0x91,0x1a,0xff,0xff,0xa0,0x70,0x85,0x98,0x00,
        0x84,0xc0,0x75,0x10,0x8b,0x7f,0x10,0x85,0xff,0x74,0x05,0x83,0xc7,0xf0,0xeb,0xe0,0x33,0xff,0xeb,0xdc};
    if (!MatchGameBytes(vb,vbCode,sizeof(vbCode)) || !MatchGameBytes(ib,ibCode,sizeof(ibCode)) ||
        !MatchGameBytes(loop,body,sizeof(body))) return false;
    auto* begin=&GameValue<BYTE>(vb);
    auto* code=&GameValue<BYTE>(loop);
    // The written instructions reside in a single page, including the short
    // branch. The original retirement address is 0x6d8028, on the next page.
    const auto length=loop+9-vb;
    DWORD oldProtection=0,ignore=0;
    if (!VirtualProtect(begin,length,PAGE_EXECUTE_READWRITE,&oldProtection)) return false;
    BYTE replacement[]{0x53,0xe8,0,0,0,0,0xeb,0x2c,0x90};
    const auto relative=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&DrawShadowList)-
                                           reinterpret_cast<std::uintptr_t>(code+6));
    std::memcpy(replacement+2,&relative,sizeof(relative));
    GameValue<BYTE>(vb+1)=GameValue<BYTE>(ib+1)=0;
    std::memcpy(code,replacement,sizeof(replacement));
    if (FlushInstructionCache(GetCurrentProcess(),begin,length) &&
        VirtualProtect(begin,length,oldProtection,&ignore)) return true;
    std::memcpy(code,body,sizeof(replacement));
    GameValue<BYTE>(vb+1)=GameValue<BYTE>(ib+1)=D3DUSAGE_WRITEONLY;
    FlushInstructionCache(GetCurrentProcess(),begin,length);
    VirtualProtect(begin,length,oldProtection,&ignore);
    return false;
}
} // namespace
