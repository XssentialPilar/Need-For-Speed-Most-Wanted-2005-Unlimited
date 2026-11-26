#pragma once
#include "scene_movie_state.hpp"
#include <deque>
#include <cmath>
#include <vector>

namespace scene_shadow {
using movie_pass::Com;
using movie_pass::Require;
#include "scene_shadow_shaders.inc"

// In the 1.3 executable 0x6d7b30 creates a private VB/IB pair for each node.
// Both are filled and unlocked before insertion by 0x6d7eef; the destructor
// 0x6d7c10 releases them. Other solid geometry is never eligible for this cache.
struct Mesh {
    IDirect3DVertexBuffer9* vertices;
    IDirect3DIndexBuffer9* indices;
    UINT vertexCount;
    UINT indexCount;
};

inline constexpr GUID CachedGeometry{0x4773c9f2,0xf463,0x4c8b,{0x9f,0xd6,0xa4,0x2c,0x37,0x28,0xcd,0x1a}};
struct State {
    D3DVIEWPORT9 viewport{};
    RECT clip{};
    float constants[18]{};
    bool marker = false;
    bool depthWrite = false;
    bool multisample = false;
    float depthBias = 0, slopeBias = 0;
    DWORD mask = 0, sampleMask = 0;
    D3D12_COMPARISON_FUNC compare{};
};

inline bool CaptureState(IDirect3DDevice9* d, State& s) {
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps;
    Require(d->GetVertexShader(vs.Put()), "Get shadow VS");
    Require(d->GetPixelShader(ps.Put()), "Get shadow PS");
    if (scene_movie::MatchesShader(vs.Get(),0x6424dc6c2fae32c9ULL)) {
        if (!scene_movie::MatchesShader(ps.Get(),0x8ca4bde6c61aeed7ULL)) return false;
        s.marker=true;
    } else if (!scene_movie::MatchesShader(vs.Get(),0x800a4a527497b8bdULL) ||
               !scene_movie::MatchesShader(ps.Get(),0x1924915ffbe63346ULL)) return false;
    auto rs = [d](D3DRENDERSTATETYPE name) {
        DWORD value=0; Require(d->GetRenderState(name,&value),"Read shadow render state"); return value;
    };
    if (rs(D3DRS_ZENABLE)!=D3DZB_TRUE || !rs(D3DRS_CLIPPING) ||
        rs(D3DRS_FILLMODE)!=D3DFILL_SOLID || rs(D3DRS_CULLMODE)!=D3DCULL_NONE ||
        rs(D3DRS_STENCILENABLE) || rs(D3DRS_ALPHATESTENABLE) || rs(D3DRS_ALPHABLENDENABLE) ||
        rs(D3DRS_FOGENABLE) || rs(D3DRS_CLIPPLANEENABLE) || rs(D3DRS_SRGBWRITEENABLE)) return false;
    const DWORD bias=rs(D3DRS_DEPTHBIAS),slope=rs(D3DRS_SLOPESCALEDEPTHBIAS);
    std::memcpy(&s.depthBias,&bias,sizeof(bias));
    std::memcpy(&s.slopeBias,&slope,sizeof(slope));
    if (!std::isfinite(s.depthBias) || std::fabs(s.depthBias)>1 || !std::isfinite(s.slopeBias)) return false;
    s.mask=rs(D3DRS_COLORWRITEENABLE)&15;
    s.depthWrite=rs(D3DRS_ZWRITEENABLE)!=0;
    s.compare=static_cast<D3D12_COMPARISON_FUNC>(rs(D3DRS_ZFUNC));
    s.multisample=rs(D3DRS_MULTISAMPLEANTIALIAS)!=0;
    s.sampleMask=rs(D3DRS_MULTISAMPLEMASK);
    if (s.compare<D3D12_COMPARISON_FUNC_NEVER || s.compare>D3D12_COMPARISON_FUNC_ALWAYS ||
        FAILED(d->GetViewport(&s.viewport)) || !s.viewport.Width || !s.viewport.Height ||
        FAILED(d->GetVertexShaderConstantF(0,s.constants,4))) return false;
    s.constants[16]=1.f/s.viewport.Width;
    s.constants[17]=-1.f/s.viewport.Height;
    s.clip={LONG(s.viewport.X),LONG(s.viewport.Y),LONG(s.viewport.X+s.viewport.Width),LONG(s.viewport.Y+s.viewport.Height)};
    if (rs(D3DRS_SCISSORTESTENABLE)) Require(d->GetScissorRect(&s.clip),"Get shadow scissor");
    return true;
}

inline DXGI_FORMAT DepthFormat(D3DFORMAT legacy, DXGI_FORMAT actual) {
    if ((legacy==D3DFMT_D24S8 || legacy==D3DFMT_D24X8 ||
         legacy==static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'))) &&
        (actual==DXGI_FORMAT_D24_UNORM_S8_UINT || actual==DXGI_FORMAT_R24G8_TYPELESS))
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    if ((legacy==D3DFMT_D16 || legacy==static_cast<D3DFORMAT>(MAKEFOURCC('D','F','1','6'))) &&
        (actual==DXGI_FORMAT_D16_UNORM || actual==DXGI_FORMAT_R16_TYPELESS))
        return DXGI_FORMAT_D16_UNORM;
    return DXGI_FORMAT_UNKNOWN;
}

class Pass {
    struct Geometry {
        Com<ID3D12Resource> gpu, upload;
        Com<IDirect3DVertexBuffer9> cacheOwner;
        UINT vertices=0, indices=0;
    };
    struct Frame {
        Com<ID3D12DescriptorHeap> dsv;
        Com<ID3D12PipelineState> pipeline;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC key{};
        std::deque<Geometry> meshes;
    };
    std::array<Frame,3> frames;
    Com<ID3D12RootSignature> root;
    scene_hud::NativeHudPass submission;
public:
    explicit Pass(ID3D12Device* owner) : submission(owner) {
        Require(owner->CreateRootSignature(0,ShadowRoot,sizeof(ShadowRoot),__uuidof(ID3D12RootSignature),
            root.AsVoid()),"Create shadow root signature");
        for (auto& f:frames) movie_pass::MakeHeap12(owner,D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                                  D3D12_DESCRIPTOR_HEAP_FLAG_NONE,f.dsv);
    }
    ID3D12Device* Device() const { return submission.Device(); }
    bool Draw(scene_hud::Interop* interop, IDirect3DSurface9* color, const D3DSURFACE_DESC& cd,
              IDirect3DSurface9* depth, const D3DSURFACE_DESC& dd, const State& s,
              const std::vector<Mesh>& meshes, bool& executed) {
        scene_hud::TargetLease ds(interop,depth,submission.Queue());
        const auto desc=ds.resource->GetDesc();
        const auto format=DepthFormat(dd.Format,desc.Format);
        if (format==DXGI_FORMAT_UNKNOWN || desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.DepthOrArraySize!=1 || desc.MipLevels!=1 || desc.Width!=cd.Width ||
            desc.Height!=cd.Height || !(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ||
            (desc.SampleDesc.Count>1 && !s.multisample)) return false;
        Frame* active=nullptr;
        const bool drawn=submission.DrawSurface(interop,color,cd,[&](unsigned slot,ID3D12GraphicsCommandList* cmd,
            DXGI_FORMAT colorFormat,DXGI_SAMPLE_DESC samples,D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
            if (desc.SampleDesc.Count!=samples.Count || desc.SampleDesc.Quality!=samples.Quality)
                throw std::invalid_argument("Mismatched shadow surfaces");
            active=&frames[slot];
            auto& f=*active;
            f.meshes.clear();
            for (const auto& m:meshes) LoadGeometry(m,cmd,f.meshes.emplace_back());
            PreparePipeline(f,colorFormat,format,samples,s);
            D3D12_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format=format;
            view.ViewDimension=samples.Count==1?D3D12_DSV_DIMENSION_TEXTURE2D:D3D12_DSV_DIMENSION_TEXTURE2DMS;
            const auto dsv=f.dsv->GetCPUDescriptorHandleForHeapStart();
            Device()->CreateDepthStencilView(ds.resource.Get(),&view,dsv);
            const D3D12_VIEWPORT vp{float(s.viewport.X),float(s.viewport.Y),float(s.viewport.Width),
                                    float(s.viewport.Height),s.viewport.MinZ,s.viewport.MaxZ};
            cmd->RSSetViewports(1,&vp);
            cmd->RSSetScissorRects(1,&s.clip);
            cmd->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
            cmd->SetPipelineState(f.pipeline.Get());
            cmd->SetGraphicsRootSignature(root.Get());
            cmd->SetGraphicsRoot32BitConstants(0,18,s.constants,0);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            for (const auto& m:f.meshes) {
                const auto base=m.gpu->GetGPUVirtualAddress();
                const D3D12_VERTEX_BUFFER_VIEW vb{base,m.vertices*12,12};
                const D3D12_INDEX_BUFFER_VIEW ib{base+vb.SizeInBytes,m.indices*2,DXGI_FORMAT_R16_UINT};
                cmd->IASetVertexBuffers(0,1,&vb);
                cmd->IASetIndexBuffer(&ib);
                cmd->DrawIndexedInstanced(m.indices,1,0,0,0);
            }
        },&ds,&executed);
        // Only publish successfully submitted uploads. The frame retains both
        // sides until its queue fence signals, even if a sector unloads first.
        if (drawn) for (const auto& m:active->meshes) if (m.cacheOwner.Get())
            m.cacheOwner->SetPrivateData(CachedGeometry,m.gpu.Get(),sizeof(IUnknown*),D3DSPD_IUNKNOWN);
        return drawn;
    }
private:
    template<class Buffer> static void CopyManaged(Buffer* buffer,void* out,UINT bytes) {
        void* data=nullptr;
        Require(buffer->Lock(0,bytes,&data,D3DLOCK_READONLY),"Read static shadow geometry");
        std::memcpy(out,data,bytes);
        Require(buffer->Unlock(),"Unlock static shadow geometry");
    }
    void LoadGeometry(const Mesh& mesh,ID3D12GraphicsCommandList* cmd,Geometry& entry) {
        if (!mesh.vertices || !mesh.indices || !mesh.vertexCount || mesh.vertexCount>65536 ||
            !mesh.indexCount || mesh.indexCount%3 || mesh.indexCount>3*1024*1024)
            throw std::invalid_argument("Invalid indexed shadow mesh");
        entry.vertices=mesh.vertexCount;
        entry.indices=mesh.indexCount;
        const UINT vb=mesh.vertexCount*12,ib=mesh.indexCount*2;
        DWORD bytes=sizeof(ID3D12Resource*);
        if (SUCCEEDED(mesh.vertices->GetPrivateData(CachedGeometry,entry.gpu.Put(),&bytes)) &&
            entry.gpu.Get() && entry.gpu->GetDesc().Width==vb+ib) return;
        D3DVERTEXBUFFER_DESC vd{};
        D3DINDEXBUFFER_DESC id{};
        Require(mesh.vertices->GetDesc(&vd),"Describe shadow vertices");
        Require(mesh.indices->GetDesc(&id),"Describe shadow indices");
        if (vd.Pool!=D3DPOOL_MANAGED || id.Pool!=D3DPOOL_MANAGED || vd.Usage || id.Usage ||
            vd.Size<vb || id.Size<ib || id.Format!=D3DFMT_INDEX16)
            throw std::invalid_argument("Shadow buffers are not static readable geometry");
        const auto info=movie_pass::Buffer12(vb+ib);
        movie_pass::CreateResource12(Device(),info,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,entry.gpu);
        movie_pass::CreateResource12(Device(),info,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,entry.upload);
        D3D12_RANGE nothing{0,0};
        void* dst=nullptr;
        Require(entry.upload->Map(0,&nothing,&dst),"Map shadow upload");
        try {
            CopyManaged(mesh.vertices,dst,vb);
            CopyManaged(mesh.indices,static_cast<BYTE*>(dst)+vb,ib);
        } catch (...) {
            entry.upload->Unmap(0,&nothing);
            throw;
        }
        const D3D12_RANGE written{0,vb+ib};
        entry.upload->Unmap(0,&written);
        cmd->CopyBufferRegion(entry.gpu.Get(),0,entry.upload.Get(),0,vb+ib);
        movie_pass::Transition12(cmd,entry.gpu.Get(),D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER|D3D12_RESOURCE_STATE_INDEX_BUFFER);
        Com<IDirect3DVertexBuffer9> owner(mesh.vertices);
        entry.cacheOwner.Swap(owner);
    }
    void PreparePipeline(Frame& f,DXGI_FORMAT color,DXGI_FORMAT depth,DXGI_SAMPLE_DESC samples,const State& s) {
        static const D3D12_INPUT_ELEMENT_DESC layout{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,
                                                      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature=root.Get();
        desc.VS={ShadowVertex,sizeof(ShadowVertex)};
        desc.PS=s.marker?D3D12_SHADER_BYTECODE{ShadowMarker,sizeof(ShadowMarker)}:
                         D3D12_SHADER_BYTECODE{ShadowPixel,sizeof(ShadowPixel)};
        desc.InputLayout={&layout,1};
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask=UINT8(s.mask);
        desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable=TRUE;
        // 9On12's DepthBiasFactorFromDSVFormat uses the UNORM bit count.
        desc.RasterizerState.DepthBias=static_cast<INT>(s.depthBias*(depth==DXGI_FORMAT_D16_UNORM?65536.f:16777216.f));
        desc.RasterizerState.SlopeScaledDepthBias=s.slopeBias;
        desc.RasterizerState.MultisampleEnable=samples.Count>1;
        desc.DepthStencilState.DepthEnable=TRUE;
        desc.DepthStencilState.DepthWriteMask=s.depthWrite?D3D12_DEPTH_WRITE_MASK_ALL:D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc=s.compare;
        desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets=1;
        desc.RTVFormats[0]=color;
        desc.DSVFormat=depth;
        desc.SampleDesc=samples;
        desc.SampleMask=s.sampleMask;
        if (f.pipeline.Get() && !std::memcmp(&desc,&f.key,sizeof(desc))) return;
        Require(Device()->CreateGraphicsPipelineState(&desc,__uuidof(ID3D12PipelineState),f.pipeline.AsVoid()),
                "Create shadow mesh pipeline");
        f.key=desc;
    }
};

class GameShadow {
public:
    bool Draw(IDirect3DDevice9* device,const std::vector<Mesh>& meshes) noexcept {
        if (!device || meshes.empty()) return false;
        bool executed=false;
        try {
            State s{};
            if (!CaptureState(device,s)) return false;
            Com<scene_hud::Interop> interop;
            if (FAILED(device->QueryInterface(scene_hud::InteropId,interop.AsVoid()))) return false;
            Com<IDirect3DSurface9> color,depth;
            D3DSURFACE_DESC cd{},dd{};
            Require(device->GetRenderTarget(0,color.Put()),"Read shadow color");
            Require(device->GetDepthStencilSurface(depth.Put()),"Read shadow depth");
            Require(color->GetDesc(&cd),"Describe shadow color");
            Require(depth->GetDesc(&dd),"Describe shadow depth");
            Com<ID3D12Device> owner;
            Require(interop->GetD3D12Device(__uuidof(ID3D12Device),owner.AsVoid()),"Get shadow device");
            if (!pass || pass->Device()!=owner.Get()) pass=std::make_unique<Pass>(owner.Get());
            return pass->Draw(interop.Get(),color.Get(),cd,depth.Get(),dd,s,meshes,executed);
        } catch (const movie_pass::D3DError& error) {
            lastError=error.Code();
        } catch (...) { lastError=E_FAIL; }
        // A lost device after ExecuteCommandLists must never replay the same
        // batch on D3D9. The regular device-reset path retires the pass.
        return executed;
    }
    HRESULT LastError() const { return lastError; }
    void Reset() { pass.reset(); }
private:
    std::unique_ptr<Pass> pass;
    HRESULT lastError=S_OK;
};
} // namespace scene_shadow
