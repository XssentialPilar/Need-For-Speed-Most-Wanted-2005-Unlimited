#include "movie_d3d.hpp"
#include <d3dx9.h>

#include <string>

namespace {

using movie_pass::Com;
using movie_pass::Require;

struct Inventory {
    IDirect3DDevice9* device;
    const wchar_t* filter;
    bool failed = false;
    unsigned resources = 0;
    unsigned techniques = 0;
    unsigned passes = 0;
};

class StateInventory final : public ID3DXEffectStateManager {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        const GUID stateManager{0x79aab587, 0x6dbc, 0x4fa7, {0x82,0xde,0x37,0xfa,0x17,0x81,0xc5,0xce}};
        if (id != __uuidof(IUnknown) && id != stateManager) return E_NOINTERFACE;
        *result = this;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --references; }

    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX*) noexcept override {
        return Record(L"Transform", state);
    }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9*) noexcept override { return Record(L"Material"); }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD index, const D3DLIGHT9*) noexcept override { return Record(L"Light", index); }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD index, BOOL active) noexcept override { return Record(L"LightEnable", index, active); }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) noexcept override {
        return Record(L"Render", state, value);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9*) noexcept override { return Record(L"Texture", stage); }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) noexcept override {
        return Record(L"TextureStage", stage, type, value);
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value) noexcept override {
        return Record(L"Sampler", stage, type, value);
    }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(FLOAT segments) noexcept override {
        std::wprintf(L"State\tNPatch\t%g\n", segments);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD format) noexcept override { return Record(L"FVF", format); }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* shader) noexcept override { return Record(L"VertexShader", shader != nullptr); }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader) noexcept override { return Record(L"PixelShader", shader != nullptr); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start, const FLOAT*, UINT count) noexcept override {
        return Record(L"VertexFloatConstants", start, count);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT start, const INT*, UINT count) noexcept override {
        return Record(L"VertexIntConstants", start, count);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT start, const BOOL*, UINT count) noexcept override {
        return Record(L"VertexBoolConstants", start, count);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start, const FLOAT*, UINT count) noexcept override {
        return Record(L"PixelFloatConstants", start, count);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT start, const INT*, UINT count) noexcept override {
        return Record(L"PixelIntConstants", start, count);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT start, const BOOL*, UINT count) noexcept override {
        return Record(L"PixelBoolConstants", start, count);
    }

private:
    template<class... Values>
    static HRESULT Record(const wchar_t* type, Values... values) {
        std::wprintf(L"State\t%ls", type);
        (std::wprintf(L"\t%lu", static_cast<unsigned long>(values)), ...);
        std::wprintf(L"\n");
        return S_OK;
    }

    ULONG references = 1;
};

void PrintShader(const wchar_t* stage, const DWORD* program) {
    if (!program) {
        std::wprintf(L"\tnative-state");
        return;
    }
    const auto version = program[0];
    std::wprintf(L"\t%ls_%u_%u", stage, (version >> 8) & 0xff, version & 0xff);
}

BOOL CALLBACK RecordResource(HMODULE image, LPCWSTR type, LPWSTR name, LONG_PTR context) try {
    auto& inventory = *reinterpret_cast<Inventory*>(context);
    const auto found = FindResourceW(image, name, type);
    const DWORD bytes = SizeofResource(image, found);
    const void* content = LockResource(LoadResource(image, found));
    std::uint32_t signature = 0;
    if (content && bytes >= sizeof(signature)) std::memcpy(&signature, content, sizeof(signature));
    if (signature != 0xfeff0901) return TRUE;
    const auto label = IS_INTRESOURCE(name)
        ? std::wstring(L"#") + std::to_wstring(reinterpret_cast<ULONG_PTR>(name))
        : std::wstring(name);
    if (inventory.filter && label != inventory.filter) return TRUE;

    StateInventory states;
    Com<ID3DXEffect> effect;
    Com<ID3DXBuffer> errors;
    D3DXEFFECT_DESC description{};
    const auto loaded = D3DXCreateEffect(inventory.device, content, bytes, nullptr, nullptr, 0, nullptr,
                                         effect.Put(), errors.Put());
    if (FAILED(loaded) || FAILED(effect->GetDesc(&description))) {
        std::fwprintf(stderr, L"%ls: effect reflection failed (0x%08lx)\n", label.c_str(),
                      static_cast<unsigned long>(loaded));
        inventory.failed = true;
        return TRUE;
    }
    ++inventory.resources;
    Require(effect->SetStateManager(&states), "SetStateManager");
    std::wprintf(L"Effect\t%ls\t%lu bytes\t%u parameters\t%u techniques\n",
                 label.c_str(), bytes, description.Parameters, description.Techniques);
    for (unsigned i = 0; i < description.Parameters; ++i) {
        D3DXPARAMETER_DESC field{};
        Require(effect->GetParameterDesc(effect->GetParameter(nullptr, i), &field), "GetParameterDesc");
        std::wprintf(L"Parameter\t%hs\t%hs\tclass=%u type=%u rows=%u columns=%u elements=%u flags=%lu\n",
                     field.Name ? field.Name : "", field.Semantic ? field.Semantic : "",
                     unsigned(field.Class), unsigned(field.Type), field.Rows, field.Columns, field.Elements, field.Flags);
    }
    for (unsigned i = 0; i < description.Techniques; ++i) {
        auto technique = effect->GetTechnique(i);
        D3DXTECHNIQUE_DESC info{};
        Require(effect->GetTechniqueDesc(technique, &info), "GetTechniqueDesc");
        Require(effect->SetTechnique(technique), "SetTechnique");
        UINT activePasses = 0;
        Require(effect->Begin(&activePasses, D3DXFX_DONOTSAVESTATE), "Begin effect state inventory");
        if (activePasses != info.Passes) throw std::runtime_error("Unexpected effect pass count");
        ++inventory.techniques;
        std::wprintf(L"Technique\t%hs\t%u passes\n", info.Name ? info.Name : "", info.Passes);
        for (unsigned j = 0; j < info.Passes; ++j) {
            D3DXPASS_DESC pass{};
            Require(effect->GetPassDesc(effect->GetPass(technique, j), &pass), "GetPassDesc");
            ++inventory.passes;
            std::wprintf(L"Pass\t%hs", pass.Name ? pass.Name : "");
            PrintShader(L"vs", pass.pVertexShaderFunction);
            PrintShader(L"ps", pass.pPixelShaderFunction);
            std::wprintf(L"\n");
            Require(effect->BeginPass(j), "BeginPass state inventory");
            Require(effect->EndPass(), "EndPass state inventory");
        }
        Require(effect->End(), "End effect state inventory");
    }
    return TRUE;
} catch (const std::exception& error) {
    reinterpret_cast<Inventory*>(context)->failed = true;
    std::fprintf(stderr, "%s\n", error.what());
    return FALSE;
}

struct ReadOnlyImage {
    HMODULE handle;
    ~ReadOnlyImage() { if (handle) FreeLibrary(handle); }
};

struct HiddenWindow {
    HWND handle;
    ~HiddenWindow() { if (handle) DestroyWindow(handle); }
};

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) {
        std::fwprintf(stderr, L"Usage: inspect_legacy_effects.exe <speed.exe> [effect resource]\n");
        return 1;
    }
    try {
        const ReadOnlyImage game{LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE |
                                                               LOAD_LIBRARY_AS_IMAGE_RESOURCE)};
        if (!game.handle) throw std::runtime_error("Could not map executable resources");
        const HiddenWindow window{CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 8, 8,
                                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
        if (!window.handle) throw std::runtime_error("Could not create offscreen device window");
        Com<IDirect3D9Ex> api;
        Require(Direct3DCreate9Ex(D3D_SDK_VERSION, api.Put()), "Create D3D9Ex reflection factory");
        D3DPRESENT_PARAMETERS present{};
        present.hDeviceWindow = window.handle;
        present.BackBufferWidth = present.BackBufferHeight = 8;
        present.Windowed = TRUE;
        present.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Com<IDirect3DDevice9Ex> device;
        Require(api->CreateDeviceEx(0, D3DDEVTYPE_HAL, window.handle, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &present, nullptr, device.Put()), "Create D3D9Ex reflection device");
        Inventory inventory{device.Get(), argc == 3 ? argv[2] : nullptr};
        if (!EnumResourceNamesW(game.handle, MAKEINTRESOURCEW(10), &RecordResource,
                                reinterpret_cast<LONG_PTR>(&inventory))) {
            throw std::runtime_error("Could not enumerate executable RCDATA");
        }
        std::wprintf(L"Total\t%u effects\t%u techniques\t%u passes\n",
                     inventory.resources, inventory.techniques, inventory.passes);
        return !inventory.failed && inventory.resources ? 0 : 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 3;
    }
}
