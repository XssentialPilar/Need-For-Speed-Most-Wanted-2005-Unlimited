#include "movie_d3d.hpp"
#include <d3dx9.h>

#include <array>
#include <cstdint>
#include <cstdlib>

namespace {

using movie_pass::Com;
using movie_pass::Require;

constexpr unsigned Side = 64;
constexpr DWORD MarkerChannels = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN;
constexpr DWORD AllColorChannels = MarkerChannels | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA;
constexpr D3DCOLOR Background = D3DCOLOR_ARGB(48, 20, 40, 60);
constexpr float ClearColor[] = {20.f / 255, 40.f / 255, 60.f / 255, 48.f / 255};
constexpr unsigned PixelBytes = 4;
using Image = std::array<std::uint8_t, Side * Side * PixelBytes>;
using Matrix = std::array<std::array<float, 4>, 4>;

struct Vertex {
    float x, y, z;
};

static_assert(sizeof(Vertex) == 12);
static_assert(sizeof(Matrix) == 64);

constexpr Vertex Geometry[]{
    {-.85f, -.57f, .18f}, { .26f, -.51f, .39f}, {-.43f, .81f, .26f},
    {-.35f, -.42f, .64f}, { .84f, -.62f, .77f}, { .42f, .75f, .66f}
};
constexpr Matrix Identity{{
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1}
}};
constexpr Matrix Perspective{{
    {.82f, -.11f, .03f, .13f},
    {.16f,  .77f, .04f, .11f},
    {.02f,  .04f, .79f, .12f},
    {.09f, -.08f, .06f, 1}
}};

void Expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Window {
    HWND handle = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, Side, Side, nullptr,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (handle) DestroyWindow(handle); }
};

struct Executable {
    explicit Executable(const wchar_t* path)
        : module(LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
        Expect(module != nullptr, "Could not map the installed executable as a resource");
    }
    ~Executable() { FreeLibrary(module); }
    HMODULE module;
};

void LoadEffect(IDirect3DDevice9* device, HMODULE executable, const wchar_t* name, Com<ID3DXEffect>& output) {
    const HRSRC resource = FindResourceW(executable, name, MAKEINTRESOURCEW(10));
    Expect(resource != nullptr, "Could not find installed effect");
    const auto bytes = SizeofResource(executable, resource);
    const void* data = LockResource(LoadResource(executable, resource));
    Expect(data && bytes >= 4, "Could not read installed effect");
    Com<ID3DXBuffer> error;
    const auto result = D3DXCreateEffect(device, data, bytes, nullptr, nullptr, 0, nullptr, output.Put(), error.Put());
    if (FAILED(result) && error.Get()) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));
    Require(result, "Load the installed effect");
}

class LegacyShadow {
public:
    LegacyShadow(IDirect3D9Ex* factory, UINT ordinal, HWND window, HMODULE executable) {
        D3DPRESENT_PARAMETERS params{};
        params.hDeviceWindow = window;
        params.BackBufferWidth = params.BackBufferHeight = Side;
        params.Windowed = TRUE;
        params.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Require(factory->CreateDeviceEx(ordinal, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                        &params, nullptr, device.Put()), "Create D3D9Ex device");
        Require(device->CreateRenderTarget(Side, Side, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                           color.Put(), nullptr), "Create D3D9 color target");
        Require(device->CreateDepthStencilSurface(Side, Side, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                  depth.Put(), nullptr), "Create D3D9 depth target");
        Require(device->CreateOffscreenPlainSurface(Side, Side, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                     readback.Put(), nullptr), "Create D3D9 staging surface");
        LoadEffect(device.Get(), executable, L"IDI_SHADOW_MAP_MESH_DEPTH_FX", depthEffect);
        LoadEffect(device.Get(), executable, L"IDI_SHADOW_MAP_MESH_FX", markerEffect);

        constexpr D3DVERTEXELEMENT9 declaration[] = {
            {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            D3DDECL_END()
        };
        Require(device->CreateVertexDeclaration(declaration, vertices.Put()), "D3D9 vertex declaration");
    }

    Image Render(const Matrix& matrix, bool marker, bool depthOnlyForeground) {
        const D3DVIEWPORT9 viewport{0, 0, Side, Side, 0, 1};
        Require(device->SetRenderTarget(0, color.Get()), "Set D3D9 target");
        Require(device->SetDepthStencilSurface(depth.Get()), "Set D3D9 depth");
        Require(device->SetViewport(&viewport), "Set D3D9 viewport");
        Require(device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE), "Enable D3D9 depth");
        Require(device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE), "Enable D3D9 depth writes");
        Require(device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS), "Set D3D9 depth comparison");
        Require(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "Disable D3D9 culling");
        Require(device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "Disable D3D9 blending");
        Require(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, Background, 1, 0), "Clear D3D9");
        Require(device->BeginScene(), "Begin D3D9 scene");
        Require(device->SetVertexDeclaration(vertices.Get()), "Bind D3D9 vertices");
        if (depthOnlyForeground) Draw(*depthEffect.Get(), matrix, 0, 0);
        Draw(marker ? *markerEffect.Get() : *depthEffect.Get(), matrix, marker ? MarkerChannels : AllColorChannels,
             marker ? 3 : 0, marker ? 1 : 2);
        Require(device->EndScene(), "End D3D9 scene");
        Require(device->GetRenderTargetData(color.Get(), readback.Get()), "Copy D3D9 color");
        D3DLOCKED_RECT rect{};
        Require(readback->LockRect(&rect, nullptr, D3DLOCK_READONLY), "Read D3D9 color");
        Image image{};
        for (unsigned y = 0; y < Side; ++y) {
            std::memcpy(image.data() + y * Side * PixelBytes,
                        static_cast<const std::uint8_t*>(rect.pBits) + y * rect.Pitch, Side * PixelBytes);
        }
        Require(readback->UnlockRect(), "Unlock D3D9 color");
        return image;
    }

private:
    void Draw(ID3DXEffect& effect, const Matrix& matrix, DWORD mask, unsigned first, unsigned primitives = 1) {
        // D3D9's sample centers are on integers. Move its rasterized geometry
        // by -0.5 pixels on both axes to compare with D3D11 sample locations.
        D3DXMATRIX constants{};
        for (unsigned i = 0; i < 4; ++i) {
            constants(i, 0) = matrix[i][0] - matrix[i][3] / Side;
            constants(i, 1) = matrix[i][1] + matrix[i][3] / Side;
            constants(i, 2) = matrix[i][2];
            constants(i, 3) = matrix[i][3];
        }
        const auto technique = effect.GetTechnique(0);
        const auto clip = effect.GetParameterByName(nullptr, "WorldViewProj");
        const auto writeMode = effect.GetParameterByName(nullptr, "ColorWriteMode");
        Expect(technique && clip && writeMode, "Incorrect installed effect parameters");
        Require(effect.SetTechnique(technique), "Select D3DX technique");
        Require(effect.SetMatrix(clip, &constants), "Set D3DX transform");
        Require(effect.SetInt(writeMode, mask), "Set D3DX write mode");
        Require(device->SetRenderState(D3DRS_COLORWRITEENABLE, mask), "Set D3D9 color mask");
        UINT passes = 0;
        Require(effect.Begin(&passes, 0), "Begin D3DX effect");
        Expect(passes == 1, "Unexpected number of D3DX passes");
        Require(effect.BeginPass(0), "Begin D3DX pass");
        DWORD activeMask = 0;
        Require(device->GetRenderState(D3DRS_COLORWRITEENABLE, &activeMask), "Read active D3D9 color mask");
        Expect(activeMask == mask, "Unexpected active D3D9 color mask");
        Require(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, primitives, Geometry + first, sizeof(Vertex)), "Draw D3D9");
        Require(effect.EndPass(), "End D3DX pass");
        Require(effect.End(), "End D3DX effect");
    }

    Com<IDirect3DDevice9Ex> device;
    Com<IDirect3DSurface9> color, depth, readback;
    Com<IDirect3DVertexDeclaration9> vertices;
    Com<ID3DXEffect> depthEffect, markerEffect;
};

void Compile(const wchar_t* path, const char* entry, const char* target, Com<ID3DBlob>& code) {
    Com<ID3DBlob> errors;
    const auto result = D3DCompileFromFile(path, nullptr, nullptr, entry, target,
                                           D3DCOMPILE_ENABLE_STRICTNESS, 0, code.Put(), errors.Put());
    if (FAILED(result) && errors.Get()) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    Require(result, "Compile D3D11 shadow shader");
}

class ModernShadow {
public:
    ModernShadow(IDXGIAdapter1* adapter, const wchar_t* shader) {
        Require(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                  device.Put(), nullptr, context.Put()), "Create D3D11 device");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = Side;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        Require(device->CreateTexture2D(&desc, nullptr, color.Put()), "Create D3D11 color texture");
        Require(device->CreateRenderTargetView(color.Get(), nullptr, colorTarget.Put()), "Create D3D11 color view");
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Require(device->CreateTexture2D(&desc, nullptr, readback.Put()), "Create D3D11 color staging");
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        desc.CPUAccessFlags = 0;
        Require(device->CreateTexture2D(&desc, nullptr, depth.Put()), "Create D3D11 depth texture");
        Require(device->CreateDepthStencilView(depth.Get(), nullptr, depthTarget.Put()), "Create D3D11 depth view");

        Com<ID3DBlob> vertexCode, depthCode, markerCode;
        Compile(shader, "ShadowVertex", "vs_5_0", vertexCode);
        Compile(shader, "ShadowDepth", "ps_5_0", depthCode);
        Compile(shader, "ShadowMarker", "ps_5_0", markerCode);
        Require(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr,
                                           vertex.Put()), "Create D3D11 shadow VS");
        Require(device->CreatePixelShader(depthCode->GetBufferPointer(), depthCode->GetBufferSize(), nullptr,
                                          depthPixel.Put()), "Create D3D11 depth PS");
        Require(device->CreatePixelShader(markerCode->GetBufferPointer(), markerCode->GetBufferSize(), nullptr,
                                          markerPixel.Put()), "Create D3D11 marker PS");
        constexpr D3D11_INPUT_ELEMENT_DESC position{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                                                      D3D11_INPUT_PER_VERTEX_DATA, 0};
        Require(device->CreateInputLayout(&position, 1, vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
                                          input.Put()), "Create D3D11 shadow input layout");

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Geometry);
        buffer.Usage = D3D11_USAGE_IMMUTABLE;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{Geometry, 0, 0};
        Require(device->CreateBuffer(&buffer, &data, geometry.Put()), "Create D3D11 shadow geometry");
        buffer.ByteWidth = sizeof(Matrix);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Require(device->CreateBuffer(&buffer, nullptr, constants.Put()), "Create D3D11 shadow constants");

        D3D11_RASTERIZER_DESC rasterState{};
        rasterState.FillMode = D3D11_FILL_SOLID;
        rasterState.CullMode = D3D11_CULL_NONE;
        rasterState.DepthClipEnable = TRUE;
        Require(device->CreateRasterizerState(&rasterState, raster.Put()), "Create D3D11 raster state");
        D3D11_DEPTH_STENCIL_DESC depthState{};
        depthState.DepthEnable = TRUE;
        depthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthState.DepthFunc = D3D11_COMPARISON_LESS;
        Require(device->CreateDepthStencilState(&depthState, depthTest.Put()), "Create D3D11 depth state");
        MakeColorWriteState(AllColorChannels, allColor);
        MakeColorWriteState(MarkerChannels, markerColor);
        MakeColorWriteState(0, noColor);
    }

    Image Render(const Matrix& matrix, bool marker, bool depthOnlyForeground) {
        context->ClearRenderTargetView(colorTarget.Get(), ClearColor);
        context->ClearDepthStencilView(depthTarget.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        auto* view = colorTarget.Get();
        context->OMSetRenderTargets(1, &view, depthTarget.Get());
        context->OMSetDepthStencilState(depthTest.Get(), 0);
        constexpr D3D11_VIEWPORT viewport{0, 0, Side, Side, 0, 1};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(raster.Get());
        const UINT stride = sizeof(Vertex), offset = 0;
        auto* vertexBuffer = geometry.Get();
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->IASetInputLayout(input.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(), nullptr, 0);
        auto* matrixBuffer = constants.Get();
        context->UpdateSubresource(matrixBuffer, 0, nullptr, matrix.data(), 0, 0);
        context->VSSetConstantBuffers(0, 1, &matrixBuffer);
        if (depthOnlyForeground) Draw(depthPixel.Get(), noColor.Get(), 0, 3);
        Draw(marker ? markerPixel.Get() : depthPixel.Get(), marker ? markerColor.Get() : allColor.Get(),
             marker ? 3 : 0, marker ? 3 : 6);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(readback.Get(), color.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        Require(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map), "Read D3D11 color");
        Image image{};
        for (unsigned y = 0; y < Side; ++y) {
            std::memcpy(image.data() + y * Side * PixelBytes,
                        static_cast<const std::uint8_t*>(map.pData) + y * map.RowPitch, Side * PixelBytes);
        }
        context->Unmap(readback.Get(), 0);
        return image;
    }

private:
    void MakeColorWriteState(UINT8 mask, Com<ID3D11BlendState>& output) {
        D3D11_BLEND_DESC state{};
        state.RenderTarget[0].RenderTargetWriteMask = mask;
        Require(device->CreateBlendState(&state, output.Put()), "Create D3D11 color mask");
    }

    void Draw(ID3D11PixelShader* pixel, ID3D11BlendState* mask, UINT first, UINT count) {
        context->PSSetShader(pixel, nullptr, 0);
        context->OMSetBlendState(mask, nullptr, ~0U);
        context->Draw(count, first);
    }

    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<ID3D11Texture2D> color, depth, readback;
    Com<ID3D11RenderTargetView> colorTarget;
    Com<ID3D11DepthStencilView> depthTarget;
    Com<ID3D11VertexShader> vertex;
    Com<ID3D11PixelShader> depthPixel, markerPixel;
    Com<ID3D11InputLayout> input;
    Com<ID3D11Buffer> geometry, constants;
    Com<ID3D11RasterizerState> raster;
    Com<ID3D11DepthStencilState> depthTest;
    Com<ID3D11BlendState> allColor, markerColor, noColor;
};

void FindAdapter(IDirect3D9Ex* legacy, UINT ordinal, Com<IDXGIAdapter1>& adapter, DXGI_ADAPTER_DESC1& desc) {
    LUID target{};
    Require(legacy->GetAdapterLUID(ordinal, &target), "Get D3D9Ex LUID");
    Com<IDXGIFactory1> factory;
    Require(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.AsVoid()), "Create DXGI factory");
    for (unsigned i = 0; factory->EnumAdapters1(i, adapter.Put()) != DXGI_ERROR_NOT_FOUND; ++i) {
        Expect(adapter.Get() != nullptr, "Could not enumerate DXGI adapter");
        Require(adapter->GetDesc1(&desc), "Get DXGI adapter description");
        if (desc.AdapterLuid.HighPart == target.HighPart && desc.AdapterLuid.LowPart == target.LowPart) return;
    }
    throw std::runtime_error("D3D9Ex adapter has no DXGI match");
}

unsigned Compare(const Image& original, const Image& modern, bool marker, const char* label) {
    const std::uint8_t background[] = {60, 40, 20, 48};
    unsigned differences = 0, colored = 0, maximumError = 0;
    for (unsigned offset = 0; offset < original.size(); offset += PixelBytes) {
        const bool oldDrawn = std::memcmp(original.data() + offset, background, PixelBytes) != 0;
        const bool newDrawn = std::memcmp(modern.data() + offset, background, PixelBytes) != 0;
        colored += newDrawn;
        bool match = oldDrawn == newDrawn;
        for (unsigned channel = 0; channel < PixelBytes; ++channel) {
            const unsigned delta = std::abs(int(original[offset + channel]) - int(modern[offset + channel]));
            maximumError = std::max(maximumError, delta);
            match = match && delta <= (!marker && channel != 3 ? 1u : 0u);
        }
        if (marker && newDrawn) {
            match = match && modern[offset] == 60 && modern[offset + 1] == 255 &&
                    modern[offset + 2] == 0 && modern[offset + 3] == 48;
        }
        if (!match) {
            if (!differences) {
                std::fprintf(stderr, "%s first diff at (%u,%u) BGRA %u,%u,%u,%u vs %u,%u,%u,%u\n", label,
                    (offset / PixelBytes) % Side, (offset / PixelBytes) / Side,
                    original[offset], original[offset + 1], original[offset + 2], original[offset + 3],
                    modern[offset], modern[offset + 1], modern[offset + 2], modern[offset + 3]);
            }
            ++differences;
        }
    }
    std::printf("%s | %u drawn pixels | %u differing pixels | max channel error %u\n",
                label, colored, differences, maximumError);
    Expect(colored > 200 && colored < Side * Side - 200, "Degenerate shadow fixture coverage");
    Expect(differences == 0, "D3D9 and D3D11 shadow readbacks differ");
    return colored;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::fputs("Uso: test_shadow_mesh_d3d11 <speed.exe> <shadow_mesh.hlsl>\n", stderr);
        return 1;
    }
    try {
        const Executable executable(argv[1]);
        const Window window;
        Expect(window.handle != nullptr, "Could not create the hidden D3D9 window");
        Com<IDirect3D9Ex> legacy;
        Require(Direct3DCreate9Ex(D3D_SDK_VERSION, legacy.Put()), "Create D3D9Ex factory");
        Expect(legacy->GetAdapterCount() > 0, "No D3D9 adapter available");
        for (unsigned ordinal = 0; ordinal < legacy->GetAdapterCount(); ++ordinal) {
            Com<IDXGIAdapter1> adapter;
            DXGI_ADAPTER_DESC1 desc{};
            FindAdapter(legacy.Get(), ordinal, adapter, desc);
            std::printf("Adapter: %ls\n", desc.Description);
            LegacyShadow original(legacy.Get(), ordinal, window.handle, executable.module);
            ModernShadow modern(adapter.Get(), argv[2]);
            Compare(original.Render(Identity, false, false), modern.Render(Identity, false, false),
                    false, "Depth with identity");
            Compare(original.Render(Perspective, false, false), modern.Render(Perspective, false, false),
                    false, "Depth with perspective WVP");
            const unsigned marker = Compare(original.Render(Perspective, true, false), modern.Render(Perspective, true, false),
                                            true, "Marker with RG write mask");
            const unsigned occluded = Compare(original.Render(Perspective, true, true), modern.Render(Perspective, true, true),
                                              true, "Far marker after masked foreground depth write");
            Expect(occluded < marker, "Masked depth prepass failed to affect marker coverage");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}
