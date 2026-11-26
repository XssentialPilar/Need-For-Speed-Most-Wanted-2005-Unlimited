// Build as a Win32 console program with the same compiler as Unlimited.
// The hidden device renders into offscreen targets; no game or desktop input.
#include "../vram_probe.cpp"
#include "../nfs_frontend_artwork.hpp"
#include <set>
#include <vector>

namespace {
template<class T> struct AutoCom {
    T* ptr = nullptr;
    ~AutoCom() { if (ptr) ptr->Release(); }
};

std::array<std::uint8_t, 124> NativeRecord(const FrontendImage& image) {
    std::array<std::uint8_t, 124> record{};
    std::memcpy(record.data()+12, image.nativeName, std::strlen(image.nativeName));
    std::memcpy(record.data()+36, &image.hash, sizeof(image.hash));
    const auto size = static_cast<std::uint16_t>(image.logicalSize);
    std::memcpy(record.data()+68, &size, sizeof(size));
    std::memcpy(record.data()+70, &size, sizeof(size));
    return record;
}

bool ExerciseImages(IDirect3DDevice9* device) {
    std::set<std::uint32_t> keys;
    std::set<std::uint64_t> glyphs;
    for (const auto& image : frontendImages) {
        auto record = NativeRecord(image);
        if (!keys.insert(image.hash).second || MatchFrontendArtwork(record.data()) != &image) return false;
        AutoCom<IDirect3DTexture9> texture;
        if (FAILED(device->CreateTexture(image.logicalSize,image.logicalSize,1,0,D3DFMT_DXT3,
                                         D3DPOOL_SYSTEMMEM,&texture.ptr,nullptr))) return false;
        auto* original = texture.ptr;
        const auto saved = record;
        ++record[12];
        if (UploadFrontendArtwork(&texture.ptr,record.data()) || texture.ptr != original) return false;
        record = saved; record[70] /= 2;
        if (UploadFrontendArtwork(&texture.ptr,record.data()) || texture.ptr != original) return false;
        record = saved;
        if (!UploadFrontendArtwork(&texture.ptr,record.data()) || texture.ptr == original || record != saved) return false;
        // Compare every channel at every level, including the final one-texel mip.
        unsigned edge = image.logicalSize*4;
        std::vector<std::uint32_t> wanted(edge*edge,0x00f6f7f7);
        IconPainter painter(wanted.data(),edge);
        PaintFrontendGlyph(painter,image.glyph);
        std::uint64_t fingerprint = 1469598103934665603ull;
        // A glyph must remain visible at its authored size. Design identities
        // are compared below on a common 64-pixel grid.
        unsigned visible = 0;
        for (const auto pixel : wanted) visible += (pixel>>24) > 64;
        if (visible < wanted.size()/100) return false;
        for (unsigned mip=0; mip<texture.ptr->GetLevelCount(); ++mip) {
            D3DSURFACE_DESC desc{};
            D3DLOCKED_RECT locked{};
            if (FAILED(texture.ptr->GetLevelDesc(mip,&desc)) || desc.Width!=edge || desc.Height!=edge ||
                desc.Format!=D3DFMT_A8R8G8B8 || FAILED(texture.ptr->LockRect(mip,&locked,nullptr,D3DLOCK_READONLY))) return false;
            bool valid = true;
            for (unsigned y=0; y<edge; ++y) {
                const auto* bytes=static_cast<const std::uint8_t*>(locked.pBits)+y*locked.Pitch;
                valid &= std::memcmp(bytes,wanted.data()+y*edge,4*edge)==0;
            }
            if (FAILED(texture.ptr->UnlockRect(mip)) || !valid) return false;
            if (edge>1) { ShrinkFrontendMip(wanted.data(),edge); edge/=2; }
        }
        if (edge!=1) return false;
        std::array<std::uint32_t,64*64> identity{};
        std::fill(identity.begin(),identity.end(),0x00f6f7f7);
        IconPainter reference(identity.data(),64);
        PaintFrontendGlyph(reference,image.glyph);
        for (auto pixel : identity) fingerprint=(fingerprint^pixel)*1099511628211ull;
        glyphs.insert(fingerprint);
    }
    std::printf("%zu native image keys, %zu distinct 64px designs and complete mip chains checked\n",keys.size(),glyphs.size());
    return glyphs.size() == static_cast<unsigned>(MenuGlyph::Heat) + 1;
}

bool ExerciseViewport(IDirect3DDevice9* device,unsigned width,unsigned height) {
    // A real FEng image uses these logical dimensions for positioning even
    // after its physical texture is supersampled. The pointer is the same
    // kind of resource the running game gets from the intercepted upload.
    const auto& icon=frontendImages[0];
    const auto record=NativeRecord(icon);
    AutoCom<IDirect3DTexture9> sampled;
    AutoCom<IDirect3DSurface9> color, read, oldColor;
    const unsigned size=icon.logicalSize;
    if (FAILED(device->CreateTexture(size,size,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&sampled.ptr,nullptr)) ||
        !UploadFrontendArtwork(&sampled.ptr,record.data()) ||
        FAILED(device->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&color.ptr,nullptr)) ||
        FAILED(device->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read.ptr,nullptr)) ||
        FAILED(device->GetRenderTarget(0,&oldColor.ptr)) || FAILED(device->SetRenderTarget(0,color.ptr))) return false;
    const auto layout=MakeAspectLayout(width,height);
    const float left=width*(UiToClip(288,640,layout.hudX)+1)*0.5f;
    const float top=height*(UiToClip(208,480,layout.hudY)+1)*0.5f;
    const float right=width*(UiToClip(352,640,layout.hudX)+1)*0.5f;
    const float bottom=height*(UiToClip(272,480,layout.hudY)+1)*0.5f;
    if (std::fabs((right-left)-(bottom-top)) > 0.01f) return false;
    struct Vertex { float x,y,z,w,u,v; };
    const Vertex quad[]{
        {left-.5f,top-.5f,0,1,0,0},{right-.5f,top-.5f,0,1,1,0},
        {left-.5f,bottom-.5f,0,1,0,1},{right-.5f,bottom-.5f,0,1,1,1}
    };
    if (FAILED(device->SetTexture(0,sampled.ptr)) ||
        FAILED(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1)) ||
        FAILED(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE)) ||
        FAILED(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1)) ||
        FAILED(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE)) ||
        FAILED(device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT)) ||
        FAILED(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT)) ||
        FAILED(device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE)) ||
        FAILED(device->SetRenderState(D3DRS_ZENABLE,FALSE)) || FAILED(device->SetRenderState(D3DRS_LIGHTING,FALSE)) ||
        FAILED(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE)) ||
        FAILED(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1)) ||
        FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0)) || FAILED(device->BeginScene())) return false;
    const auto drawn=device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
    if (FAILED(device->EndScene()) || FAILED(drawn) || FAILED(device->SetTexture(0,nullptr)) ||
        FAILED(device->SetRenderTarget(0,oldColor.ptr)) || FAILED(device->GetRenderTargetData(color.ptr,read.ptr))) return false;
    std::vector<std::uint32_t> expected(256*256,0x00f6f7f7);
    IconPainter painter(expected.data(),256);
    PaintFrontendGlyph(painter,icon.glyph);
    D3DLOCKED_RECT lock{};
    if (FAILED(read.ptr->LockRect(&lock,nullptr,D3DLOCK_READONLY))) return false;
    unsigned matching=0,total=0,visible=0,atBoundary=0,spill=0;
    for (unsigned y=0;y<height;++y) {
        const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(lock.pBits)+y*lock.Pitch);
        for(unsigned x=0;x<width;++x) {
            if (x+0.5f<left || y+0.5f<top || x+0.5f>=right || y+0.5f>=bottom) {
                spill += (row[x]>>24)!=0;
                continue;
            }
            const auto sx=std::min(255u,static_cast<unsigned>((x+0.5f-left)*256/(right-left)));
            const auto sy=std::min(255u,static_cast<unsigned>((y+0.5f-top)*256/(bottom-top)));
            const auto wanted=expected[sy*256+sx];
            if (row[x]==wanted) {
                ++matching;
            } else {
                // A sampler may round either way when precisely on a source
                // texel boundary. Include D3D9's 1/256-pixel vertex grid.
                const float u=(x+0.5f-left)*256/(right-left);
                const float v=(y+0.5f-top)*256/(bottom-top);
                const float tolerance=0.0005f+256.0f/(256.0f*(right-left));
                const auto firstX=std::clamp(static_cast<int>(std::floor(u-tolerance)),0,255);
                const auto lastX=std::clamp(static_cast<int>(std::floor(u+tolerance)),0,255);
                const auto firstY=std::clamp(static_cast<int>(std::floor(v-tolerance)),0,255);
                const auto lastY=std::clamp(static_cast<int>(std::floor(v+tolerance)),0,255);
                bool valid=false;
                for (int j=firstY;j<=lastY;++j) for (int k=firstX;k<=lastX;++k)
                    valid |= row[x]==expected[j*256+k];
                atBoundary += valid;
            }
            ++total; visible += (row[x]>>24)>64;
        }
    }
    read.ptr->UnlockRect();
    std::printf("%ux%u square %.2f pixels, %u/%u exact GPU samples, %u boundary ties, %u outside\n",width,height,right-left,matching,total,atBoundary,spill);
    return visible>0 && total>0 && matching+atBoundary==total && !spill;
}
} // namespace
int main() {
    const HWND window=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    AutoCom<IDirect3D9Ex> api;
    using MakeEx=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);
    const auto create=reinterpret_cast<MakeEx>(GetProcAddress(LoadSystemLibrary(L"d3d9.dll"),"Direct3DCreate9Ex"));
    if (!window || !create || FAILED(create(D3D_SDK_VERSION,&api.ptr))) return 1;
    AutoCom<IDirect3DDevice9Ex> device;
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth=pp.BackBufferHeight=32;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8; pp.hDeviceWindow=window; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    if (FAILED(api.ptr->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&device.ptr))) return 2;
    if (!ExerciseImages(device.ptr)) return 3;
    for (const auto wh : {std::pair<UINT,UINT>{640,480},{960,600},{1280,720},{840,360},{1280,360},
                           {720,720},{512,640},{432,768},{1200,120},{120,1200}}) {
        if (!ExerciseViewport(device.ptr,wh.first,wh.second)) return 4;
    }
    std::printf("All menu artwork checks passed.\n");
    return 0;
}

