#include <cstdlib>
#include "scene_movie_d3d12.hpp"
#include "movie_reconstruction_d3d9.hpp"
#include <d3dx9.h>
using movie_pass::Com;
using movie_pass::Require;

namespace {
void Check(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
struct Input {
    UINT w,h;
    std::vector<BYTE> pixels;
    Com<IDirect3DTexture9> texture;
    Input(IDirect3DDevice9* d,UINT width,UINT height,D3DFORMAT format=D3DFMT_A8R8G8B8) : w(width),h(height),pixels(w*h*4) {
        Require(d->CreateTexture(w,h,1,0,format,D3DPOOL_MANAGED,texture.Put(),nullptr),"Create managed movie");
    }
    void Update(scene_movie::GameMovie& pass,UINT tick, const wchar_t* rawFrame=nullptr) {
        for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) {
            auto* p=pixels.data()+4*(y*w+x);
            p[0]=BYTE(x*9+y*4+tick);p[1]=BYTE(y*7+x*2);p[2]=BYTE(255-3*x-tick);p[3]=BYTE(139+x%96);
        }
        if (rawFrame) {
            FILE* file=_wfopen(rawFrame,L"rb");
            Check(file!=nullptr,"Cannot open decoded BGRA frame");
            const bool valid=std::fread(pixels.data(),1,pixels.size(),file)==pixels.size() && std::fgetc(file)==EOF;
            std::fclose(file);
            Check(valid,"Wrong decoded frame dimensions");
        }
        D3DLOCKED_RECT r{};
        Require(texture->LockRect(0,&r,nullptr,0),"Lock legacy movie");
        for(UINT i=0;i<h;++i)std::memcpy(static_cast<BYTE*>(r.pBits)+i*r.Pitch,pixels.data()+w*i*4,w*4);
        Require(texture->UnlockRect(0),"Commit legacy movie");
        pass.Update(texture.Get(),pixels.data(),w,h);
    }
};

class Lab {
public:
    explicit Lab(const wchar_t* executable, UINT width=136, UINT height=84,
                 D3DMULTISAMPLE_TYPE msaa=D3DMULTISAMPLE_NONE) : width(width), height(height) {
        window=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        auto dll=LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        *factory.Put()=scene_hud::CreateInteropFactory(dll,D3D_SDK_VERSION);
        Check(factory.Get()!=nullptr,"Missing 9On12");
        D3DPRESENT_PARAMETERS pp{};
        pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=width;pp.BackBufferHeight=height;
        Require(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,device.Put()),"9On12 device");
        HMODULE image=LoadLibraryExW(executable,nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
        Check(image!=nullptr,"Cannot load installed image");
        const auto r=FindResourceW(image,L"IDI_FE_FX",MAKEINTRESOURCEW(10));
        Require(D3DXCreateEffect(device.Get(),LockResource(LoadResource(image,r)),SizeofResource(image,r),nullptr,nullptr,
                                 0,nullptr,fx.Put(),nullptr),"Create original FE material");
        FreeLibrary(image);
        const D3DVERTEXELEMENT9 elements[]={
            {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},
            {0,16,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        Require(device->CreateVertexDeclaration(elements,decl.Put()),"FE declaration");
        Require(device->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,msaa,0,FALSE,color.Put(),nullptr),"Render target");
        if (msaa!=D3DMULTISAMPLE_NONE)
            Require(device->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,resolved.Put(),nullptr),"Resolved color");
        Require(device->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,staging.Put(),nullptr),"Readback");
        Require(device->SetRenderTarget(0,color.Get()),"Bind target");
    }
    ~Lab(){native.Reset();if(window)DestroyWindow(window);}
    IDirect3DDevice9* Get()const{return device.Get();}
    std::vector<DWORD> Draw(Input& src,bool modern,bool flip,bool linear,bool fade,bool cut,bool reconstructed9=false,bool angled=false) {
        auto* d=device.Get();
        const D3DVIEWPORT9 full{0,0,width,height,0,1}, area{3,2,width-6,height-4,0,1};
        Require(d->SetViewport(&full),"Full area");
        Require(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x7d374123,1,0),"Draw background");
        Require(d->SetViewport(&area),"Video area");
        Require(d->BeginScene(),"Begin film");
        D3DXMATRIX transform{};
        D3DXMatrixIdentity(&transform);
        D3DXVECTOR4 offset{0,0,0,0};
        Require(fx->SetMatrix("WorldViewProj",&transform),"Film matrix");
        Require(fx->SetVector("TextureOffset",&offset),"Film UV");
        Require(fx->SetTechnique(fx->GetTechnique(0)),"FE technique");
        UINT passes=0;Require(fx->Begin(&passes,0),"Begin retail effect");Require(fx->BeginPass(0),"Bind retail pass");
        Require(d->SetVertexShaderConstantF(0,&transform._11,4),"Bind FE transform");
        Require(d->SetVertexShaderConstantF(4,&offset.x,1),"Bind FE UV constant");
        Require(d->SetTexture(0,src.texture.Get()),"Bind stock film texture");
        Require(d->SetVertexDeclaration(decl.Get()),"Bind FE declaration");
        d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);
        d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,angled?D3DCULL_CCW:D3DCULL_NONE);
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff);
        d->SetRenderState(D3DRS_ALPHATESTENABLE,cut);d->SetRenderState(D3DRS_ALPHAREF,97);
        d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);d->SetRenderState(D3DRS_ALPHABLENDENABLE,fade);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,linear?D3DTEXF_LINEAR:D3DTEXF_POINT);
        d->SetSamplerState(0,D3DSAMP_MAGFILTER,linear?D3DTEXF_LINEAR:D3DTEXF_POINT);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        const DWORD t=fade?0x9e6f6366:0xff7f7f7f;
        scene_movie::Vertex q[]{
            {{-.91f,.76f,.5f},t,{0,float(flip)},{0,0}},
            {{ .85f,.76f,.5f},t,{1,float(flip)},{0,0}},
            {{ .85f,-.82f,.5f},t,{1,float(!flip)},{0,0}},
            {{-.91f,-.82f,.5f},t,{0,float(!flip)},{0,0}}
        };
        if (angled) {
            for (auto& vertex : q) {
                const float x=vertex.xyz[0],y=vertex.xyz[1];
                vertex.xyz[0]=0.86f*x-0.17f*y;
                vertex.xyz[1]=0.17f*x+0.86f*y;
            }
        }
        if (modern) {
            if(!native.Draw(d,src.texture.Get(),D3DPT_TRIANGLEFAN,2,q,sizeof(q[0]))) {
                std::printf("Last error: %08lx %s\n",native.LastError(),native.LastErrorText()); throw std::runtime_error("Native movie fallback during parity test");
            }
        } else if (reconstructed9) {
            HRESULT result{};
            Check(movie_reconstruction::Draw(d,src.texture.Get(),D3DPT_TRIANGLEFAN,2,q,sizeof(q[0]),result),
                  "D3D9 reconstruction unavailable");
            Require(result,"Reconstruct stock movie");
            Com<IDirect3DPixelShader9> restored;
            Require(d->GetPixelShader(restored.Put()),"Read restored shader");
            Check(scene_movie::MatchesShader(restored.Get(),0x2e49799b0014f387ULL),"Retail FE state was not restored");
        } else Require(d->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,2,q,sizeof(q[0])),"Draw retail movie");
        Require(fx->EndPass(),"End film pass"); Require(fx->End(),"End FE material");
        Require(d->EndScene(),"End film");
        if (resolved.Get()) Require(d->StretchRect(color.Get(),nullptr,resolved.Get(),nullptr,D3DTEXF_NONE),"Resolve legacy MSAA");
        Require(d->GetRenderTargetData(resolved.Get()?resolved.Get():color.Get(),staging.Get()),"Synchronize legacy target");
        D3DLOCKED_RECT r{};Require(staging->LockRect(&r,nullptr,D3DLOCK_READONLY),"Read combined frame");
        std::vector<DWORD> out(width*height);
        for(UINT y=0;y<height;++y)std::memcpy(out.data()+y*width,static_cast<BYTE*>(r.pBits)+size_t(y)*r.Pitch,width*4);
        staging->UnlockRect();
        return out;
    }
    void Pair(Input& src,bool flip,bool linear,bool fade,bool cut,const char* name,bool angled=false) {
        const auto a=Draw(src,false,flip,linear,fade,cut,false,angled);
        const auto b=Draw(src,true,flip,linear,fade,cut,false,angled);
        UINT differences=0,maxError=0,shown=0,first=0;
        for(UINT i=0;i<a.size();++i) {
            shown+=a[i]!=0x7d374123;
            if(a[i]==b[i])continue;
            if(!differences)first=i;
            ++differences;
            for(unsigned n=0;n<4;++n) {
                const int expected=int((a[i]>>(n*8)) & 255);
                const int actual=int((b[i]>>(n*8)) & 255);
                maxError=std::max(maxError,unsigned(std::abs(expected-actual)));
            }
        }
        std::printf("%s: %u covered, %u differing pixels, max channel delta %u, first %u %08lx/%08lx\n",name,
            shown,differences,maxError,first,a[first],b[first]);
        Check(shown>1000 && maxError<=1,"FE video parity failed");
    }
    scene_movie::GameMovie native;
private:
    UINT width, height;
    HWND window{};
    Com<IDirect3D9> factory;Com<IDirect3DDevice9> device;
    Com<IDirect3DVertexDeclaration9> decl;
    Com<ID3DXEffect> fx;
    Com<IDirect3DSurface9> color,staging,resolved;
};
} // namespace

int wmain(int argc,wchar_t**argv) {
    const bool use11=argc>2 && wcscmp(argv[argc-1],L"--d3d11")==0;
    const bool useVk=argc>2 && wcscmp(argv[argc-1],L"--vulkan")==0;
    const int api=useVk?scene_movie::GameMovie::VulkanBackend:use11?11:12;
    if (use11 || useVk) --argc;
    if(argc<2 || argc>3)return 1;
    try {
        Lab lab(argv[1]);
        lab.native.SetBackend(api);
        Input small(lab.Get(),32,17),full(lab.Get(),1920,1080),opaque(lab.Get(),63,37,D3DFMT_X8R8G8B8);
        small.Update(lab.native,1);
        lab.Pair(small,false,false,false,false,"Point");
        lab.Pair(small,true,true,false,false,"Bilinear and inverted");
        lab.Pair(small,true,true,true,true,"Tint, blend, alpha test");
        Check(lab.native.UploadCount()==1,"Reused video uploaded again");
        small.Update(lab.native,47);lab.Pair(small,false,false,false,false,"Next video frame");
        full.Update(lab.native,19);lab.Pair(full,false,true,false,false,"Full HD resize");
        opaque.Update(lab.native,82);lab.Pair(opaque,true,true,true,true,"Opaque XRGB alpha");
        Check(lab.native.UploadCount()==4 && lab.native.DrawCount()==6,"Unexpected upload/draw count");
        lab.native.Invalidate();
        Check(!lab.native.Draw(lab.Get(),opaque.texture.Get(),D3DPT_TRIANGLEFAN,2,&lab,32),"Stale native video");
        {
            Lab multisample(argv[1],136,84,D3DMULTISAMPLE_4_SAMPLES);
            multisample.native.SetBackend(api);
            Input film(multisample.Get(),32,17);
            film.Update(multisample.native,1);
            multisample.Pair(film,true,true,true,true,"4x MSAA with alpha test and blending");
            multisample.Pair(film,true,true,true,true,"Tilted 4x MSAA with clockwise winding",true);
        }
        Lab large(argv[1],1920,1080);
        large.native.SetBackend(api);
        Input stock(large.Get(),1024,512);
        stock.Update(large.native,17,argc==3?argv[2]:nullptr);
        for (bool flip : {false,true}) {
            const auto original=large.Draw(stock,false,flip,true,flip,flip);
            const auto legacy=large.Draw(stock,false,flip,true,flip,flip,true);
            const auto modern=large.Draw(stock,true,flip,true,flip,flip);
            unsigned changed=0, mismatchedAlpha=0, maxDelta=0;
            for (size_t i=0;i<original.size();++i) {
                changed+=original[i]!=legacy[i];
                mismatchedAlpha+=(original[i]>>24)!=(legacy[i]>>24);
                for (unsigned shift : {0u,8u,16u,24u}) {
                    const int a=(legacy[i]>>shift)&255, b=(modern[i]>>shift)&255;
                    maxDelta=std::max(maxDelta,unsigned(std::abs(a-b)));
                }
            }
            std::printf("FHD stock reconstruction, flip=%d: changed=%u; alpha mismatches=%u; D3D9/%d max delta=%u\n",
                        flip,changed,mismatchedAlpha,api,maxDelta);
            Check(changed>1000 && !mismatchedAlpha && maxDelta<=1,"Unexpected reconstruction output");
        }
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 2;}
}

