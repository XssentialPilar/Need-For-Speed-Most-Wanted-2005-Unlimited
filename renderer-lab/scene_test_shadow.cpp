#include "scene_shadow_d3d12.hpp"
#include <d3dx9.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using movie_pass::Com;
using movie_pass::Require;
constexpr UINT Side=96;
using Image=std::array<DWORD,Side*Side>;
constexpr DWORD Background=0x3014283c;
struct Point { float x,y,z; };
constexpr Point Points[]={{-.83f,-.62f,.13f},{.26f,-.56f,.36f},{-.38f,.88f,.19f},
                          {-.37f,-.49f,.64f},{.88f,-.51f,.79f},{.39f,.78f,.7f}};

void Check(bool valid,const char* text) { if (!valid) throw std::runtime_error(text); }
void LoadEffect(IDirect3DDevice9* device,HMODULE game,const wchar_t* name,Com<ID3DXEffect>& effect) {
    const auto res=FindResourceW(game,name,MAKEINTRESOURCEW(10));
    Check(res!=nullptr,"Missing game shadow effect");
    Require(D3DXCreateEffect(device,LockResource(LoadResource(game,res)),SizeofResource(game,res),
        nullptr,nullptr,0,nullptr,effect.Put(),nullptr),"Load retail shadow effect");
}
struct MeshStorage {
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> ib;
    scene_shadow::Mesh Init(IDirect3DDevice9* d,const Point* points,UINT vertexCount,const WORD* order,UINT indexCount) {
        Require(d->CreateVertexBuffer(vertexCount*sizeof(Point),0,0,D3DPOOL_MANAGED,vb.Put(),nullptr),"Create static VB");
        Require(d->CreateIndexBuffer(indexCount*2,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,ib.Put(),nullptr),"Create static IB");
        void* data=nullptr;
        Require(vb->Lock(0,0,&data,0),"Write vertex data");
        std::memcpy(data,points,vertexCount*sizeof(Point));
        Require(vb->Unlock(),"Unlock vertices");
        Require(ib->Lock(0,0,&data,0),"Write indices");
        std::memcpy(data,order,indexCount*2);
        Require(ib->Unlock(),"Unlock indices");
        return {vb.Get(),ib.Get(),vertexCount,indexCount};
    }
};
struct StreetBarrier {
    std::array<Point,10> points{};
    std::array<WORD,12> indices{};
    explicit StreetBarrier(const std::filesystem::path& game) {
        std::ifstream stream(game.parent_path()/"TRACKS"/"STREAML2RA.BUN",std::ios::binary);
        std::array<char,4096> sector{};
        Check(bool(stream.read(sector.data(),sector.size())),"Read original L2RA sector");
        Check(!std::memcmp(sector.data()+0x9d0,"XW_CONCBARRIERCORRUGATEDA_2Z_RB_00",34),"Original L2RA barrier missing");
        const DWORD vbHeader[]{0x00134b01,600},ibHeader[]{0x00134b03,24};
        Check(!std::memcmp(sector.data()+0xaf8,vbHeader,sizeof(vbHeader)) &&
              !std::memcmp(sector.data()+0xad8,ibHeader,sizeof(ibHeader)),"Unexpected barrier geometry layout");
        for (UINT i=0;i<points.size();++i) std::memcpy(&points[i],sector.data()+0xb00+i*60,sizeof(Point));
        std::memcpy(indices.data(),sector.data()+0xae0,sizeof(indices));
        for (WORD index:indices) Check(index<points.size(),"Invalid barrier index");
    }
};
void Test(IDirect3D9* factory,UINT ordinal,HWND hwnd,HMODULE game,D3DFORMAT depthFormat,const StreetBarrier& barrier) {
    Com<IDirect3DDevice9> d;
    D3DPRESENT_PARAMETERS pp{};
    pp.hDeviceWindow=hwnd; pp.Windowed=TRUE; pp.BackBufferWidth=pp.BackBufferHeight=Side;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    Require(factory->CreateDevice(ordinal,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,d.Put()),"Create 9on12 device");
    Com<IDirect3DTexture9> color,depth;
    Com<IDirect3DSurface9> rt,ds,readback;
    Require(d->CreateTexture(Side,Side,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,color.Put(),nullptr),"Shadow color texture");
    Require(d->CreateTexture(Side,Side,1,D3DUSAGE_DEPTHSTENCIL,depthFormat,D3DPOOL_DEFAULT,depth.Put(),nullptr),"Shadow depth texture");
    Require(color->GetSurfaceLevel(0,rt.Put()),"Color level");
    Require(depth->GetSurfaceLevel(0,ds.Put()),"Depth level");
    Require(d->SetRenderTarget(0,rt.Get()),"Bind color");
    Require(d->SetDepthStencilSurface(ds.Get()),"Bind depth");
    Require(d->CreateOffscreenPlainSurface(Side,Side,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,readback.Put(),nullptr),"Create readback");
    Com<ID3DXEffect> marker,depthEffect;
    LoadEffect(d.Get(),game,L"IDI_SHADOW_MAP_MESH_DEPTH_FX",depthEffect);
    LoadEffect(d.Get(),game,L"IDI_SHADOW_MAP_MESH_FX",marker);
    Com<IDirect3DVertexDeclaration9> decl;
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    Require(d->CreateVertexDeclaration(elements,decl.Put()),"Shadow vertex declaration");
    Require(d->SetVertexDeclaration(decl.Get()),"Select shadow vertex declaration");
    MeshStorage first,second,street;
    const WORD order[]{2,0,1};
    const auto nearMesh=first.Init(d.Get(),Points,3,order,3),farMesh=second.Init(d.Get(),Points+3,3,order,3);
    const std::vector<scene_shadow::Mesh> streetMesh{street.Init(d.Get(),barrier.points.data(),10,barrier.indices.data(),12)};
    const std::vector<scene_shadow::Mesh> both{nearMesh,farMesh},foreground{nearMesh},background{farMesh};
    scene_shadow::GameShadow native;
    D3DXMATRIX mat{.82f,-.11f,.03f,.13f,.16f,.77f,.04f,.11f,
                   .02f,.04f,.79f,.12f,.09f,-.08f,.06f,1};
    const D3DXMATRIX barrierCamera{.18f,0,.015f,0,0,1.3f,.1f,0,0,.35f,.45f,0,.72f,.55f,.42f,1};
    auto render=[&](bool modern,unsigned mode) {
        Require(d->SetRenderState(D3DRS_ZENABLE,TRUE),"Enable depth");
        Require(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE),"Enable depth write");
        Require(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE),"Disable previous scissor");
        const D3DVIEWPORT9 full{0,0,Side,Side,0,1};
        Require(d->SetViewport(&full),"Full viewport");
        Require(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,Background,1,0),"Clear shadow scene");
        const D3DVIEWPORT9 vp{3,7,Side-10,Side-14,.1f,.95f};
        Require(d->SetViewport(&vp),"Inset shadow viewport");
        const RECT clip{11,10,80,79};
        Require(d->SetScissorRect(&clip),"Set test scissor");
        Require(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"Enable scissor");
        Require(d->BeginScene(),"Begin shadow scene");
        auto draw=[&](ID3DXEffect* fx,DWORD mask,const std::vector<scene_shadow::Mesh>& list) {
            const float bias=mode==4 && mask==0?-.0001f:0.f, slope=mode==5 && mask==0?-1.1f:0.f;
            DWORD encodedBias=0,encodedSlope=0;
            std::memcpy(&encodedBias,&bias,sizeof(bias));
            std::memcpy(&encodedSlope,&slope,sizeof(slope));
            Require(d->SetRenderState(D3DRS_DEPTHBIAS,encodedBias),"Set shadow bias");
            Require(d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,encodedSlope),"Set slope bias");
            Require(fx->SetTechnique(fx->GetTechnique(0)),"Select retail technique");
            Require(fx->SetMatrix("WorldViewProj",mode==3?&barrierCamera:&mat),"Set shadow transform");
            Require(fx->SetInt("ColorWriteMode",mask),"Set shader color write mode");
            Require(d->SetRenderState(D3DRS_COLORWRITEENABLE,mask),"Set write mask");
            UINT passes=0;
            Require(fx->Begin(&passes,0),"Begin retail effect");
            Require(fx->BeginPass(0),"Bind retail shaders");
            Require(fx->CommitChanges(),"Commit shadow uniforms");
            if (modern) {
                if (!native.Draw(d.Get(),list)) throw movie_pass::D3DError(native.LastError(),"Native shadow pass declined");
            } else for (const auto& m:list) {
                Require(d->SetStreamSource(0,m.vertices,0,12),"Bind positions");
                Require(d->SetIndices(m.indices),"Bind indices");
                Require(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,m.vertexCount,0,m.indexCount/3),"Draw retail mesh");
            }
            Require(fx->EndPass(),"End shadow pass");
            Require(fx->End(),"End shadow effect");
        };
        if (mode==0) draw(depthEffect.Get(),15,both);
        if (mode==2) draw(depthEffect.Get(),0,foreground);
        if (mode==1 || mode==2) draw(marker.Get(),3,background);
        if (mode==3) draw(depthEffect.Get(),15,streetMesh);
        if (mode>=4) {
            draw(depthEffect.Get(),0,foreground);
            draw(marker.Get(),3,both);
        }
        Require(d->EndScene(),"End shadow scene");
        Require(d->GetRenderTargetData(rt.Get(),readback.Get()),"Read shadow result");
        D3DLOCKED_RECT lock{};
        Require(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY),"Map result");
        Image result{};
        for (UINT y=0;y<Side;++y) std::memcpy(result.data()+y*Side,static_cast<BYTE*>(lock.pBits)+y*lock.Pitch,Side*4);
        Require(readback->UnlockRect(),"Unmap result");
        return result;
    };
    unsigned count[6]{};
    for (UINT mode=0;mode<6;++mode) {
        const auto expected=render(false,mode),actual=render(true,mode);
        unsigned diff=0,maxError=0;
        for (size_t i=0;i<expected.size();++i) {
            count[mode]+=expected[i]!=Background;
            bool sameCoverage=(expected[i]==Background)==(actual[i]==Background);
            for (unsigned s=0;s<32;s+=8) {
                const auto delta=unsigned(std::abs(int((expected[i]>>s)&255)-int((actual[i]>>s)&255)));
                maxError=std::max(delta,maxError);
                if (delta>(s<24 && (mode==0 || mode==3)?1u:0u)) sameCoverage=false;
            }
            diff+=!sameCoverage;
        }
        std::printf("adapter %u depth %08x mode %u coverage %u mismatches %u max error %u\n",
                    ordinal,UINT(depthFormat),mode,count[mode],diff,maxError);
        Check(count[mode]>200 && !diff,"Shadow output differs from retail");
    }
    Check(count[2]<count[1],"Native depth prepass did not occlude marker");
    Check(count[4]==count[2] && count[5]==count[2],"Depth bias did not reject coplanar mesh");
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=2) return 1;
    setvbuf(stdout,nullptr,_IONBF,0);
    try {
        auto game=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
        Check(game,"Missing speed.exe");
        const StreetBarrier barrier(argv[1]);
        auto sys=LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        Com<IDirect3D9> factory; *factory.Put()=scene_hud::CreateInteropFactory(sys,D3D_SDK_VERSION);
        Check(factory.Get(),"Unavailable 9on12");
        auto hwnd=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,Side,Side,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(hwnd,"Create hidden device window");
        for (UINT i=0;i<factory->GetAdapterCount();++i) {
            Test(factory.Get(),i,hwnd,game,D3DFMT_D24S8,barrier);
            Test(factory.Get(),i,hwnd,game,static_cast<D3DFORMAT>(MAKEFOURCC('D','F','1','6')),barrier);
            Test(factory.Get(),i,hwnd,game,static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),barrier);
        }
        DestroyWindow(hwnd); FreeLibrary(game);
        return 0;
    } catch (const movie_pass::D3DError& e) {
        std::fprintf(stderr,"%s %08lx\n",e.what(),e.Code()); return 2;
    } catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 3;}
}
