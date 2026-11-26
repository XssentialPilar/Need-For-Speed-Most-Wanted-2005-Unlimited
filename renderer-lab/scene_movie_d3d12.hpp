#pragma once
#include "scene_movie_state.hpp"
#include "scene_movie_d3d11.hpp"
#if __has_include(<vulkan/vulkan.h>)
#include "scene_movie_vulkan.hpp"
#define UNLIMITED_MOVIE_VULKAN 1
#endif

namespace scene_movie {
class Gpu {
    struct Slot {
        Com<ID3D12DescriptorHeap> srv, sampler;
        Com<ID3D12Resource> upload, sampled;
        UINT64 capacity=0;
        Com<ID3D12PipelineState> pipeline;
        D3D12_RENDER_TARGET_BLEND_DESC blend{};
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        DXGI_SAMPLE_DESC samples{};
        D3D12_CULL_MODE cull=D3D12_CULL_MODE_NONE;
        UINT sampleMask=0;
    };
public:
    explicit Gpu(ID3D12Device* device) : target(device) {
        Require(device->CreateRootSignature(0,MovieRoot,sizeof(MovieRoot),__uuidof(ID3D12RootSignature),root.AsVoid()),
                "Create frontend video root signature");
        for(auto& slot:slots) {
            movie_pass::MakeHeap12(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,slot.srv);
            movie_pass::MakeHeap12(device,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,slot.sampler);
        }
    }
    ~Gpu(){target.Drain();}
    ID3D12Device* Device() const {return target.Device();}
    UINT64 uploads=0;
    bool Draw(scene_hud::Interop* interop, IDirect3DSurface9* dest, const D3DSURFACE_DESC& desc, State& state,
              const Vertex* quad, const Decoded& decoded) {
        const Vertex list[]{quad[0],quad[1],quad[2],quad[0],quad[2],quad[3]};
        const bool changing=stamp!=decoded.stamp;
        const bool drawn=target.DrawSurface(interop,dest,desc,[&](unsigned index,ID3D12GraphicsCommandList* cmd,
                DXGI_FORMAT format,DXGI_SAMPLE_DESC samples,D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
            auto& slot=slots[index];
            if(!slot.pipeline.Get() || slot.format!=format || slot.samples.Count!=samples.Count ||
               slot.samples.Quality!=samples.Quality || slot.cull!=state.cull || slot.sampleMask!=state.sampleMask ||
               std::memcmp(&slot.blend,&state.blend,sizeof(state.blend))) {
                BuildPipeline(slot,format,samples,state);
            }
            const auto vb=target.UploadGeometry(index,list,sizeof(list),sizeof(Vertex));
            const bool newSize=!input.Get() || width!=decoded.width || height!=decoded.height;
            if(newSize) {
                D3D12_RESOURCE_DESC image{};
                image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                image.Width=decoded.width; image.Height=decoded.height;
                image.DepthOrArraySize=image.MipLevels=image.SampleDesc.Count=1;
                image.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;
                Com<ID3D12Resource> next;
                movie_pass::CreateResource12(Device(),image,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,next);
                input.Swap(next); width=decoded.width; height=decoded.height; stamp=0;
            }
            if(changing || newSize) {
                const UINT row=decoded.width*4, pitch=(row+255u)&~255u;
                const UINT64 total=UINT64(pitch)*decoded.height;
                if(slot.capacity<total) {
                    movie_pass::CreateResource12(Device(),movie_pass::Buffer12(total),D3D12_HEAP_TYPE_UPLOAD,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ,slot.upload);
                    slot.capacity=total;
                }
                D3D12_RANGE none{};
                void* data=nullptr;
                Require(slot.upload->Map(0,&none,&data),"Map native movie upload");
                for(UINT y=0;y<decoded.height;++y) std::memcpy(static_cast<BYTE*>(data)+size_t(y)*pitch,
                                                               decoded.bgra.data()+size_t(y)*row,row);
                const D3D12_RANGE written{0,SIZE_T(total)};
                slot.upload->Unmap(0,&written);
                movie_pass::Transition12(cmd,input.Get(),stamp?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON,
                                         D3D12_RESOURCE_STATE_COPY_DEST);
                const D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{0,{DXGI_FORMAT_B8G8R8A8_UNORM,width,height,1,pitch}};
                movie_pass::TransferTexture12(cmd,input.Get(),slot.upload.Get(),fp,true);
                movie_pass::Transition12(cmd,input.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            Com<ID3D12Resource> retained(input.Get()); slot.sampled.Swap(retained);
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=state.srgbSample?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM;
            view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels=1;
            view.Shader4ComponentMapping=state.forceOpaque ?
                D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
                    D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2,
                    D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1) : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            Device()->CreateShaderResourceView(input.Get(),&view,slot.srv->GetCPUDescriptorHandleForHeapStart());
            Device()->CreateSampler(&state.sampler,slot.sampler->GetCPUDescriptorHandleForHeapStart());
            ID3D12DescriptorHeap* heaps[]{slot.srv.Get(),slot.sampler.Get()};
            const auto& vp=state.viewport;
            const D3D12_VIEWPORT area{float(vp.X),float(vp.Y),float(vp.Width),float(vp.Height),vp.MinZ,vp.MaxZ};
            cmd->SetPipelineState(slot.pipeline.Get()); cmd->SetGraphicsRootSignature(root.Get());
            cmd->SetDescriptorHeaps(2,heaps);
            cmd->SetGraphicsRoot32BitConstants(0,26,state.constants,0);
            cmd->SetGraphicsRootDescriptorTable(1,slot.srv->GetGPUDescriptorHandleForHeapStart());
            cmd->SetGraphicsRootDescriptorTable(2,slot.sampler->GetGPUDescriptorHandleForHeapStart());
            cmd->RSSetViewports(1,&area); cmd->RSSetScissorRects(1,&state.scissor);
            cmd->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); cmd->IASetVertexBuffers(0,1,&vb);
            cmd->DrawInstanced(6,1,0,0);
        });
        if(drawn && changing) {stamp=decoded.stamp;++uploads;}
        return drawn;
    }
private:
    void BuildPipeline(Slot& slot,DXGI_FORMAT format,DXGI_SAMPLE_DESC samples,const State& state) {
        const D3D12_INPUT_ELEMENT_DESC inputLayout[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature=root.Get();
        d.VS={MovieVertex,sizeof(MovieVertex)}; d.PS={MoviePixel,sizeof(MoviePixel)};
        d.InputLayout={inputLayout,3};
        d.BlendState.RenderTarget[0]=state.blend;
        d.SampleMask=state.sampleMask;
        d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=state.cull;
        d.RasterizerState.DepthClipEnable=TRUE;d.RasterizerState.MultisampleEnable=samples.Count>1;
        d.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        d.DepthStencilState.FrontFace.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        d.DepthStencilState.FrontFace.StencilFailOp=d.DepthStencilState.FrontFace.StencilDepthFailOp=
            d.DepthStencilState.FrontFace.StencilPassOp=D3D12_STENCIL_OP_KEEP;
        d.DepthStencilState.BackFace=d.DepthStencilState.FrontFace;
        d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets=1; d.SampleDesc=samples; d.RTVFormats[0]=format;
        Require(Device()->CreateGraphicsPipelineState(&d,__uuidof(ID3D12PipelineState),slot.pipeline.AsVoid()),"Create video material pipeline");
        slot.format=format;slot.samples=samples;slot.cull=state.cull;slot.blend=state.blend;slot.sampleMask=state.sampleMask;
    }
    scene_hud::NativeHudPass target;
    std::array<Slot,3> slots;
    Com<ID3D12RootSignature> root;
    Com<ID3D12Resource> input;
    UINT width=0,height=0;
    UINT64 stamp=0;
};

class GameMovie {
public:
    static constexpr int VulkanBackend=13;
    void SetBackend11(bool selected) { SetBackend(selected?11:12); }
    void SetBackend(int selected) {
        std::lock_guard<std::mutex> lock(mutex);
        if (backend==selected) return;
        ReleaseBackends();backend=selected;
    }
    void Invalidate() {
        std::lock_guard<std::mutex> lock(mutex);
        decoded.texture=nullptr;
    }
    void Update(IDirect3DTexture9* texture, const BYTE* pixels, UINT width, UINT height) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        decoded.texture=nullptr;
        if (!texture || !pixels || !width || !height || UINT64(width)*height>16777216) return;
        try {
            decoded.bgra.resize(size_t(width)*height*4);
            std::memcpy(decoded.bgra.data(),pixels,decoded.bgra.size());
            decoded.width=width;decoded.height=height;
            ++decoded.stamp;decoded.texture=texture;
        } catch (const std::bad_alloc&) {}
    }
    bool Draw(IDirect3DDevice9* d, IDirect3DTexture9* movie, D3DPRIMITIVETYPE type, UINT prims,
              const void* verts, UINT stride) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if (!d || !movie || type!=D3DPT_TRIANGLEFAN || prims!=2 || !verts || stride!=sizeof(Vertex)) return Decline("Missing film texture or unexpected draw geometry");
        if (movie!=decoded.texture) return Decline("No matching decoded movie frame");
        if (failedDevice==d && GetTickCount64()<retryDeviceAfter) return false;
        bool creating=false;
        try {
            Com<IDirect3DBaseTexture9> bound;
            if (FAILED(d->GetTexture(0,bound.Put())) || bound.Get()!=movie) return Decline("Movie is not bound on sampler 0");
            State state{};
            const char* reason=nullptr;
            if (!ReadState(d,state,&reason)) return Decline(reason);
            Com<scene_hud::Interop> interop;
            Com<IDirect3DSurface9> dest;
            D3DSURFACE_DESC desc{},source{};
            if (FAILED(d->QueryInterface(scene_hud::InteropId,interop.AsVoid()))) return Decline("D3D9 device does not expose D3D9On12 interop");
            if (FAILED(d->GetRenderTarget(0,dest.Put())) || FAILED(dest->GetDesc(&desc)) ||
                FAILED(movie->GetLevelDesc(0,&source))) return Decline("Game movie target or source unavailable");
            if (source.Format!=D3DFMT_A8R8G8B8 && source.Format!=D3DFMT_X8R8G8B8) return Decline("Unexpected decoded movie format");
            state.forceOpaque=source.Format==D3DFMT_X8R8G8B8;
            state.constants[22]=float(source.Width)/decoded.width;
            state.constants[23]=float(source.Height)/decoded.height;
            Com<ID3D12Device> device;
            Require(interop->GetD3D12Device(__uuidof(ID3D12Device),device.AsVoid()),"Get movie device");
            const auto* quad=static_cast<const Vertex*>(verts);
            if (backend==VulkanBackend) {
#ifdef UNLIMITED_MOVIE_VULKAN
                if (!gpuVk || device.Get()!=gpuVk->Device()) {
                    creating=true;gpuVk=std::make_unique<GpuVk>(device.Get());creating=false;
                }
                if (!gpuVk->Draw(interop.Get(),dest.Get(),desc,state,quad,decoded)) return Decline("Vulkan movie target or free frame unavailable");
#else
                return Decline("Build omitted Vulkan headers");
#endif
            } else if (backend==11) {
                if (!gpu11 || device.Get()!=gpu11->Device()) {
                    creating=true;gpu11=std::make_unique<Gpu11>(device.Get());creating=false;
                }
                if (!gpu11->Draw(interop.Get(),dest.Get(),desc,state,quad,decoded)) return Decline("D3D11 movie target or free frame unavailable");
            } else if (backend==12) {
                if (!gpu || device.Get()!=gpu->Device()) {
                    creating=true;gpu=std::make_unique<Gpu>(device.Get());creating=false;
                }
                if (!gpu->Draw(interop.Get(),dest.Get(),desc,state,quad,decoded)) return Decline("D3D12 movie target or free frame unavailable");
            } else return Decline("Compatibility movie backend selected");
            error=S_OK;declined=nullptr;errorText[0]=0;failedDevice=nullptr;
            ++draws;
            // DrawPrimitiveUP clears stream zero after consuming its caller-owned vertices.
            d->SetStreamSource(0,nullptr,0,0);
            return true;
        } catch (const movie_pass::D3DError& e) {RecordError(e.Code(),e.what());}
          catch (const std::exception& e) {RecordError(E_FAIL,e.what());}
          catch (...) {RecordError(E_FAIL,"Unknown movie renderer error");}
        // Retry missing runtime capabilities occasionally. Recreating a failed
        // graphics device for every film draw stalls the compatibility path.
        if (creating || error==E_OUTOFMEMORY) { failedDevice=d;retryDeviceAfter=GetTickCount64()+5000; }
        return false;
    }
    UINT64 DrawCount() const {return draws;}
    UINT64 UploadCount() const {
#ifdef UNLIMITED_MOVIE_VULKAN
        if(gpuVk) return gpuVk->uploads;
#endif
        return gpu11?gpu11->uploads:(gpu?gpu->uploads:0);
    }
    HRESULT LastError() const {return error;}
    const char* LastErrorText() const {return declined?declined:errorText;}
    void Reset(){std::lock_guard<std::mutex> lock(mutex);ReleaseBackends();decoded=Decoded{};}
private:
    bool Decline(const char* why) {declined=why;error=S_FALSE;return false;}
    void RecordError(HRESULT code,const char* why) {declined=nullptr;error=code;std::snprintf(errorText,sizeof(errorText),"%s",why);}
    void ReleaseBackends() {
        failedDevice=nullptr;retryDeviceAfter=0;
        gpu.reset();gpu11.reset();
#ifdef UNLIMITED_MOVIE_VULKAN
        gpuVk.reset();
#endif
    }
    std::mutex mutex;
    Decoded decoded;
    std::unique_ptr<Gpu> gpu;
    std::unique_ptr<Gpu11> gpu11;
#ifdef UNLIMITED_MOVIE_VULKAN
    std::unique_ptr<GpuVk> gpuVk;
#endif
    int backend=12;
    UINT64 draws=0;
    HRESULT error=S_OK;
    char errorText[192]{};
    const char* declined=nullptr;
    IDirect3DDevice9* failedDevice{};
    ULONGLONG retryDeviceAfter{};
};
} // namespace scene_movie
