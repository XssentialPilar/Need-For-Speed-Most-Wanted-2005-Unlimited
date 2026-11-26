#pragma once
#include "scene_movie_state.hpp"
#include "shared_device11.hpp"
#include <d3d11_4.h>
#include <dxgi1_4.h>

namespace scene_movie {

// The film is shaded by an independent D3D11 device. D3D12 only composites
// the shared layer onto the game's private surface using its current blending.
class Gpu11 {
    struct SharedHandle {
        HANDLE value{};
        ~SharedHandle() { if (value) CloseHandle(value); }
    };
    struct Timeline {
        Com<ID3D12Fence> modern;
        Com<ID3D11Fence> video;
    };
    struct Layer {
        Com<ID3D12Resource> resource[2];
        Com<ID3D11RenderTargetView> rtv[2];
        Com<ID3D11RenderTargetView> sharedRtv[2];
        Com<ID3D11ShaderResourceView> multiView[2];
        Com<ID3D12DescriptorHeap> srv;
        Com<ID3D12PipelineState> pipeline;
        D3D12_RENDER_TARGET_BLEND_DESC blend{};
        DXGI_FORMAT output = DXGI_FORMAT_UNKNOWN;
        DXGI_SAMPLE_DESC samples{};
        UINT width{}, height{}, sampleMask{},tileColumns{1},tileRows{1};
        UINT64 lastRead{};
    };
public:
    explicit Gpu11(ID3D12Device* owner) : target(owner) {
        movie_pass::CreatePeer11(owner,device,context);
        for (auto* timeline : {&written, &consumed}) {
            SharedHandle handle;
            Require(owner->CreateFence(0,D3D12_FENCE_FLAG_SHARED,__uuidof(ID3D12Fence),timeline->modern.AsVoid()),"Create video timeline");
            Require(owner->CreateSharedHandle(timeline->modern.Get(),nullptr,GENERIC_ALL,nullptr,&handle.value),"Export video timeline");
            Require(device->OpenSharedFence(handle.value,__uuidof(ID3D11Fence),timeline->video.AsVoid()),"Import video timeline");
        }
        Require(owner->CreateRootSignature(0,CompositeRoot,sizeof(CompositeRoot),__uuidof(ID3D12RootSignature),root.AsVoid()),"Create movie composition signature");
        Require(device->CreateVertexShader(MovieVertex,sizeof(MovieVertex),nullptr,vs.Put()),"Create D3D11 movie VS");
        Require(device->CreatePixelShader(MovieLayer,sizeof(MovieLayer),nullptr,ps.Put()),"Create D3D11 movie PS");
        Require(device->CreateVertexShader(CompositeVertex,sizeof(CompositeVertex),nullptr,exportVs.Put()),"Create video sample export VS");
        Require(device->CreatePixelShader(ExportSamples,sizeof(ExportSamples),nullptr,exportPs.Put()),"Create video sample export PS");
        const D3D11_INPUT_ELEMENT_DESC elements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        Require(device->CreateInputLayout(elements,3,MovieVertex,sizeof(MovieVertex),layout.Put()),"Create D3D11 movie input");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth=sizeof(Vertex)*6;buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        Require(device->CreateBuffer(&buffer,nullptr,vertices.Put()),"Create video geometry buffer");
        buffer.ByteWidth=112;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        Require(device->CreateBuffer(&buffer,nullptr,constants.Put()),"Create video constants");
        const D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
        for (auto& layer:layers)
            Require(owner->CreateDescriptorHeap(&heap,__uuidof(ID3D12DescriptorHeap),layer.srv.AsVoid()),"Create movie layer descriptors");
    }
    ~Gpu11() { target.Drain(); }
    ID3D12Device* Device() const {return target.Device();}
    UINT64 uploads{};
    bool Draw(scene_hud::Interop* interop, IDirect3DSurface9* dest, const D3DSURFACE_DESC& desc,
              const State& state, const Vertex* quad, const Decoded& decoded) {
        unsigned used=0;
        const bool drew=target.DrawSurface(interop,dest,desc,[&](unsigned index,ID3D12GraphicsCommandList* cmd,
                      DXGI_FORMAT format,DXGI_SAMPLE_DESC samples,D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
            used=index;
            Layer& layer=layers[index];
            PrepareLayer(layer,desc,format,samples,state);
            if (layer.lastRead)
                Require(context->Wait(consumed.video.Get(),layer.lastRead),"Wait for compositor to release shared video");
            Render(layer,state,quad,decoded);
            Require(context->Signal(written.video.Get(),++writeSerial),"Signal shaded movie");
            context->Flush();
            Require(target.Queue()->Wait(written.modern.Get(),writeSerial),"Wait for native D3D11 movie pixels");
            for(auto& resource:layer.resource)
                movie_pass::Transition12(cmd,resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->SetPipelineState(layer.pipeline.Get());
            cmd->SetGraphicsRootSignature(root.Get());
            ID3D12DescriptorHeap* heap=layer.srv.Get();
            cmd->SetDescriptorHeaps(1,&heap);
            const UINT dimensions[]{desc.Width,desc.Height,layer.tileColumns};
            cmd->SetGraphicsRoot32BitConstants(0,3,dimensions,0);
            cmd->SetGraphicsRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());
            const auto& v=state.viewport;
            const D3D12_VIEWPORT vp{float(v.X),float(v.Y),float(v.Width),float(v.Height),0,1};
            cmd->RSSetViewports(1,&vp);cmd->RSSetScissorRects(1,&state.scissor);
            cmd->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->DrawInstanced(3,1,0,0);
            for(auto& resource:layer.resource)
                movie_pass::Transition12(cmd,resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        });
        if (drew) {
            const UINT64 serial=++readSerial;
            // DrawSurface has already submitted the composition. In the rare
            // event of a signal failure, drain it before allowing layer reuse.
            if (SUCCEEDED(target.Queue()->Signal(consumed.modern.Get(),serial))) layers[used].lastRead=serial;
            else {target.Drain();layers[used].lastRead=0;}
        }
        return drew;
    }
private:
    void PrepareLayer(Layer& layer,const D3DSURFACE_DESC& desc,DXGI_FORMAT format,DXGI_SAMPLE_DESC samples,const State& state) {
        const bool resize=!layer.resource[1].Get() || desc.Width!=layer.width || desc.Height!=layer.height ||
                     samples.Count!=layer.samples.Count || samples.Quality!=layer.samples.Quality;
        if(resize) {
            D3D12_RESOURCE_DESC tex{};
            // Shared 2D textures are single-sampled. Pack each original MSAA
            // sample into a separate tile for the compositor's sample shader.
            UINT cols=1;
            while (cols*cols<samples.Count && UINT64(desc.Width)*cols*2<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) cols*=2;
            const UINT rows=(samples.Count+cols-1)/cols;
            tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;tex.Width=UINT64(desc.Width)*cols;tex.Height=desc.Height*rows;
            tex.MipLevels=tex.DepthOrArraySize=tex.SampleDesc.Count=1;
            tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            auto cpu=layer.srv->GetCPUDescriptorHandleForHeapStart();
            const auto increment=Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            for(unsigned i=0;i<2;++i,cpu.ptr+=increment) {
                Com<ID3D12Resource> resource;
                Require(Device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&tex,D3D12_RESOURCE_STATE_COMMON,
                                nullptr,__uuidof(ID3D12Resource),resource.AsVoid()),"Create shared movie layer");
                SharedHandle handle;
                Require(Device()->CreateSharedHandle(resource.Get(),nullptr,GENERIC_ALL,nullptr,&handle.value),"Export movie layer");
                Com<ID3D11Texture2D> texture;
                Com<ID3D11RenderTargetView> view;
                Require(device->OpenSharedResource1(handle.value,__uuidof(ID3D11Texture2D),texture.AsVoid()),"Import movie layer into D3D11");
                Require(device->CreateRenderTargetView(texture.Get(),nullptr,view.Put()),"Create shared movie target");
                Device()->CreateShaderResourceView(resource.Get(),nullptr,cpu);
                layer.resource[i].Swap(resource);layer.sharedRtv[i].Swap(view);
                layer.rtv[i].Reset();layer.multiView[i].Reset();
                if(samples.Count>1) {
                    D3D11_TEXTURE2D_DESC msaa{};
                    msaa.Width=desc.Width;msaa.Height=desc.Height;msaa.SampleDesc=samples;
                    msaa.Format=tex.Format;msaa.ArraySize=msaa.MipLevels=1;
                    msaa.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
                    Com<ID3D11Texture2D> privateMsaa;
                    Require(device->CreateTexture2D(&msaa,nullptr,privateMsaa.Put()),"Create private multisample film");
                    Require(device->CreateRenderTargetView(privateMsaa.Get(),nullptr,layer.rtv[i].Put()),"Create multisample drawing view");
                    Require(device->CreateShaderResourceView(privateMsaa.Get(),nullptr,layer.multiView[i].Put()),"Create multisample export view");
                }
            }
            layer.width=desc.Width;layer.height=desc.Height;layer.tileColumns=cols;layer.tileRows=rows;
        }
        if(resize || !layer.pipeline.Get() || layer.output!=format || layer.sampleMask!=state.sampleMask ||
                std::memcmp(&layer.blend,&state.blend,sizeof(state.blend))) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipe{};
            pipe.pRootSignature=root.Get(); pipe.VS={CompositeVertex,sizeof(CompositeVertex)};
            pipe.PS=samples.Count>1?D3D12_SHADER_BYTECODE{CompositePixelMS,sizeof(CompositePixelMS)}:
                                   D3D12_SHADER_BYTECODE{CompositePixel,sizeof(CompositePixel)};
            pipe.BlendState.RenderTarget[0]=state.blend;
            pipe.SampleMask=state.sampleMask;pipe.SampleDesc=samples;
            pipe.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipe.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
            pipe.RasterizerState.DepthClipEnable=TRUE;pipe.RasterizerState.MultisampleEnable=samples.Count>1;
            pipe.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
            pipe.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pipe.NumRenderTargets=1;pipe.RTVFormats[0]=format;
            Require(Device()->CreateGraphicsPipelineState(&pipe,__uuidof(ID3D12PipelineState),layer.pipeline.AsVoid()),"Create movie composition pipeline");
            layer.output=format;layer.blend=state.blend;layer.samples=samples;layer.sampleMask=state.sampleMask;
        }
    }
    void Write(ID3D11Buffer* buffer,const void* data,UINT bytes) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map D3D11 movie data");
        std::memcpy(mapped.pData,data,bytes);
        context->Unmap(buffer,0);
    }
    void Render(Layer& layer,const State& state,const Vertex* quad,const Decoded& decoded) {
        if (!input.Get() || width!=decoded.width || height!=decoded.height || opaque!=state.forceOpaque) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width=decoded.width;d.Height=decoded.height;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;
            d.Format=state.forceOpaque?DXGI_FORMAT_B8G8R8X8_TYPELESS:DXGI_FORMAT_B8G8R8A8_TYPELESS;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            Com<ID3D11Texture2D> next;
            Require(device->CreateTexture2D(&d,nullptr,next.Put()),"Create decoded D3D11 movie texture");
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
            view.Format=state.forceOpaque?DXGI_FORMAT_B8G8R8X8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
            Require(device->CreateShaderResourceView(next.Get(),&view,linearView.Put()),"Create linear movie input");
            view.Format=state.forceOpaque?DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            Require(device->CreateShaderResourceView(next.Get(),&view,srgbView.Put()),"Create sRGB movie input");
            input.Swap(next);width=d.Width;height=d.Height;opaque=state.forceOpaque;stamp=0;
        }
        if (stamp!=decoded.stamp) {
            context->UpdateSubresource(input.Get(),0,nullptr,decoded.bgra.data(),width*4,0);
            stamp=decoded.stamp;++uploads;
        }
        D3D11_SAMPLER_DESC samplerDesc{};
        static_assert(sizeof(samplerDesc)==sizeof(state.sampler));
        std::memcpy(&samplerDesc,&state.sampler,sizeof(samplerDesc));
        if (!sampler.Get() || std::memcmp(&lastSampler,&samplerDesc,sizeof(samplerDesc))) {
            Require(device->CreateSamplerState(&samplerDesc,sampler.Put()),"Create movie sampler");
            lastSampler=samplerDesc;
        }
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode=D3D11_FILL_SOLID;rasterDesc.CullMode=D3D11_CULL_MODE(state.cull);
        rasterDesc.DepthClipEnable=rasterDesc.ScissorEnable=TRUE;rasterDesc.MultisampleEnable=layer.samples.Count>1;
        if(!raster.Get() || std::memcmp(&lastRaster,&rasterDesc,sizeof(rasterDesc))) {
            Require(device->CreateRasterizerState(&rasterDesc,raster.Put()),"Create movie raster state");
            lastRaster=rasterDesc;
        }
        const Vertex list[]{quad[0],quad[1],quad[2],quad[0],quad[2],quad[3]};
        Write(vertices.Get(),list,sizeof(list));Write(constants.Get(),state.constants,sizeof(state.constants));
        const auto& v=state.viewport;
        const D3D11_VIEWPORT vp{float(v.X),float(v.Y),float(v.Width),float(v.Height),v.MinZ,v.MaxZ};
        context->RSSetViewports(1,&vp);context->RSSetScissorRects(1,&state.scissor);context->RSSetState(raster.Get());
        const UINT stride=sizeof(Vertex),offset=0;
        auto* vb=vertices.Get();auto* cb=constants.Get();auto* srv=(state.srgbSample?srgbView:linearView).Get();
        auto* s=sampler.Get();
        auto* targets=layer.samples.Count>1 ? layer.rtv : layer.sharedRtv;
        ID3D11RenderTargetView* rtvs[]{targets[0].Get(),targets[1].Get()};
        context->IASetInputLayout(layout.Get());context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
        context->VSSetConstantBuffers(0,1,&cb);context->PSSetConstantBuffers(0,1,&cb);
        context->PSSetShaderResources(0,1,&srv);context->PSSetSamplers(0,1,&s);
        // The video shader writes alpha >= 0.5 for all covered samples.
        const float clear[]{0,0,0,0};
        context->ClearRenderTargetView(rtvs[0],clear);
        context->OMSetRenderTargets(2,rtvs,nullptr);context->Draw(6,0);
        context->OMSetRenderTargets(0,nullptr,nullptr);
        if (layer.samples.Count>1) {
            const UINT dimensions[]{layer.width,layer.height,layer.tileColumns};
            Write(constants.Get(),dimensions,sizeof(dimensions));
            ID3D11ShaderResourceView* sampleViews[]{layer.multiView[0].Get(),layer.multiView[1].Get()};
            ID3D11RenderTargetView* packed[]{layer.sharedRtv[0].Get(),layer.sharedRtv[1].Get()};
            const D3D11_VIEWPORT atlas{0,0,float(layer.width*layer.tileColumns),float(layer.height*layer.tileRows),0,1};
            context->RSSetState(nullptr);context->RSSetViewports(1,&atlas);
            context->IASetInputLayout(nullptr);
            context->VSSetShader(exportVs.Get(),nullptr,0);context->PSSetShader(exportPs.Get(),nullptr,0);
            context->PSSetShaderResources(0,2,sampleViews);
            context->OMSetRenderTargets(2,packed,nullptr);context->Draw(3,0);
            context->OMSetRenderTargets(0,nullptr,nullptr);
            ID3D11ShaderResourceView* unbind[2]{};
            context->PSSetShaderResources(0,2,unbind);
        }
    }
    scene_hud::NativeHudPass target;
    Timeline written,consumed;
    UINT64 writeSerial{},readSerial{},stamp{};
    UINT width{},height{};
    bool opaque{};
    Com<ID3D11Device5> device;
    Com<ID3D11DeviceContext4> context;
    Com<ID3D12RootSignature> root;
    Com<ID3D11Texture2D> input;
    Com<ID3D11ShaderResourceView> linearView,srgbView;
    Com<ID3D11VertexShader> vs,exportVs;
    Com<ID3D11PixelShader> ps,exportPs;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11Buffer> vertices,constants;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11RasterizerState> raster;
    D3D11_SAMPLER_DESC lastSampler{};
    D3D11_RASTERIZER_DESC lastRaster{};
    std::array<Layer,3> layers;
};
} // namespace scene_movie
