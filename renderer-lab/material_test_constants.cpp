#include "material_constants.hpp"
#include "material_test_samplers.hpp"
#include <d3dx9.h>
#include <map>
#include <set>

namespace {
using movie_pass::Com;
using movie_pass::Require;
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }

struct Rig {
    IDirect3DDevice9* legacy;
    ID3D11Device* modern;
    ID3D11DeviceContext* context;
    sampler_test::Textures& textures;
    unsigned effects = 0, passes = 0, bindings = 0, changes = 0, aliases = 0;
};

struct Stage {
    Stage(Rig& rig, const DWORD* input, const std::string& source, bool vertex)
        : source(source), vertex(vertex) {
        Com<ID3DBlob> errors;
        Require(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, legacy_pixel::Entry,
            vertex ? "vs_5_0" : "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, code.Put(), errors.Put()), "Compile stage");
        transfer = std::make_unique<material::StageConstants11>(rig.modern, input, D3DXGetShaderSize(input), code.Get());
        samplers = std::make_unique<material::Samplers11>(rig.modern,code.Get());
        Require(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
                           IID_ID3D11ShaderReflection, reflection.AsVoid()), "Reflect validation shader");
        if (!transfer->Layout().ByteSize()) return;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = transfer->Layout().ByteSize();
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Require(rig.modern->CreateBuffer(&desc, nullptr, readback.Put()), "Create readback buffer");
        auto* layout = reflection->GetConstantBufferByIndex(0);
        if (legacy_program::IsPixel1(*input)) {
            for (UINT i = 0; i < 8; ++i) expected.push_back({material::Bank::Float, i, 1, i * 16});
        } else {
            // Independent oracle: resolve the actual HLSL register aliases used
            // by the compiled shader, without consulting MojoShader's metadata.
            std::istringstream lines(source);
            std::string line;
            while (std::getline(lines, line)) {
                std::istringstream fields(line);
                std::string directive, name, expression;
                if (!(fields >> directive >> name >> expression) || directive != "#define" ||
                    name.size() < 2 || !isdigit(static_cast<unsigned char>(name[1])) ||
                    expression.rfind("uniforms_", 0) != 0) continue;
                const auto open = expression.find('[');
                if (open == std::string::npos) continue;
                const UINT reg = std::stoul(name.substr(1));
                const UINT packed = std::stoul(expression.substr(open + 1));
                D3D11_SHADER_VARIABLE_DESC var{};
                if (FAILED(layout->GetVariableByName(expression.substr(0, open).c_str())->GetDesc(&var))) continue;
                const auto type = name[0] == 'c' ? material::Bank::Float : name[0] == 'i' ? material::Bank::Int : material::Bank::Bool;
                expected.push_back({type, reg, 1, var.StartOffset + packed * 16});
            }
        }
    }
    void Verify(Rig& rig,UINT iteration) {
        rig.textures.Verify(*samplers,reflection.Get(),rig.legacy,rig.context,vertex,iteration);
        Require(transfer->Bind(rig.legacy, rig.context), "Capture and bind runtime registers");
        Com<ID3D11Buffer> bound;
        if (vertex) rig.context->VSGetConstantBuffers(0, 1, bound.Put());
        else rig.context->PSGetConstantBuffers(0, 1, bound.Put());
        ++rig.bindings;
        Check(bool(bound.Get()) == bool(readback.Get()), "Stale shader constants");
        if (!bound.Get()) return;
        rig.context->CopyResource(readback.Get(), bound.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(rig.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read native material buffer");
        std::vector<BYTE> bytes(transfer->Layout().ByteSize());
        std::memcpy(bytes.data(), mapped.pData, bytes.size());
        rig.context->Unmap(readback.Get(), 0);
        for (const auto& alias : expected) {
            std::array<float,4> f{};
            std::array<int,4> n{};
            if (alias.bank == material::Bank::Float) {
                Require(vertex ? rig.legacy->GetVertexShaderConstantF(alias.first, f.data(), 1) :
                                 rig.legacy->GetPixelShaderConstantF(alias.first, f.data(), 1), "Read reference float");
            } else if (alias.bank == material::Bank::Int) {
                Require(vertex ? rig.legacy->GetVertexShaderConstantI(alias.first, n.data(), 1) :
                                 rig.legacy->GetPixelShaderConstantI(alias.first, n.data(), 1), "Read reference int");
            } else {
                Require(vertex ? rig.legacy->GetVertexShaderConstantB(alias.first, n.data(), 1) :
                                 rig.legacy->GetPixelShaderConstantB(alias.first, n.data(), 1), "Read reference bool");
            }
            const auto* reference = alias.bank == material::Bank::Float ? static_cast<void*>(f.data()) : static_cast<void*>(n.data());
            Check(alias.byteOffset + 16 <= bytes.size() && !std::memcmp(bytes.data() + alias.byteOffset, reference, 16),
                  "Wrong legacy register in GPU material constants");
            ++rig.aliases;
        }
        if (!previous.empty() && previous != bytes) ++rig.changes;
        previous.swap(bytes);
    }
    std::string source;
    bool vertex;
    std::vector<material::Span> expected;
    Com<ID3DBlob> code;
    Com<ID3D11ShaderReflection> reflection;
    Com<ID3D11Buffer> readback;
    std::vector<BYTE> previous;
    std::unique_ptr<material::StageConstants11> transfer;
    std::unique_ptr<material::Samplers11> samplers;
};

void VerifyRuntimeTextureTypes(Rig& rig,HMODULE image) {
    const auto resource=FindResourceW(image,L"IDI_FE_FX",MAKEINTRESOURCEW(10));
    Com<ID3DXEffect> effect;
    Require(D3DXCreateEffect(rig.legacy,LockResource(LoadResource(image,resource)),SizeofResource(image,resource),
                             nullptr,nullptr,0,nullptr,effect.Put(),nullptr),"Load runtime material variants");
    UINT count{};
    const auto technique=effect->GetTechnique(0);
    Require(effect->SetTechnique(technique),"Use FE material topology");
    Require(effect->Begin(&count,0),"Begin FE material variants");
    Require(effect->BeginPass(0),"Bind FE material for topology change");
    D3DXPASS_DESC original{};
    Require(effect->GetPassDesc(effect->GetPass(technique,0),&original),"Read FE topology program");
    const auto vs=original.pVertexShaderFunction,ps=original.pPixelShaderFunction;
    const auto producer=legacy_program::Translate(vs,D3DXGetShaderSize(vs));
    const legacy_pixel::TextureShape shapes[]{legacy_pixel::TextureShape::Flat,legacy_pixel::TextureShape::Cube,legacy_pixel::TextureShape::Volume};
    for (unsigned kind=0;kind<3;++kind) {
        for (UINT projection : {0u,4u}) {
            rig.textures.SetTopology(rig.legacy,kind,projection);
            legacy_pixel::Stages live{};
            Require(material::ReadPixel1TextureStages(rig.legacy,live),"Capture PS1 runtime texture types");
            Check(live[0].shape==shapes[kind] && live[0].projectionCount==projection,"Wrong runtime PS1 sampler or projection");
            auto pixel=legacy_program::LinkPixelInputs(producer,legacy_program::Translate(ps,D3DXGetShaderSize(ps),live));
            Stage stage(rig,ps,pixel,false);
            rig.textures.Verify(*stage.samplers,stage.reflection.Get(),rig.legacy,rig.context,false,projection?1:0);
        }
    }
    Require(effect->EndPass(),"End FE topology variants");Require(effect->End(),"End FE topology effect");
}

void EditParameters(ID3DXEffect* effect, const D3DXEFFECT_DESC& description, UINT iteration) {
    for (UINT i = 0; i < description.Parameters; ++i) {
        auto param = effect->GetParameter(nullptr, i);
        D3DXPARAMETER_DESC desc{};
        Require(effect->GetParameterDesc(param, &desc), "Read effect parameter");
        if (desc.Type != D3DXPT_FLOAT || desc.Class > D3DXPC_MATRIX_COLUMNS || !desc.Bytes || desc.Bytes % 4) continue;
        std::vector<float> values(desc.Bytes/4);
        for (size_t j = 0; j < values.size(); ++j) values[j] = (float(i+1)*.013f + float(j+1)*.007f) * iteration;
        Require(effect->SetValue(param, values.data(), desc.Bytes), "Change material parameter");
    }
}

BOOL CALLBACK Inspect(HMODULE image, LPCWSTR type, LPWSTR name, LONG_PTR user) try {
    auto& rig = *reinterpret_cast<Rig*>(user);
    const auto resource = FindResourceW(image, name, type);
    const auto bytes = SizeofResource(image, resource);
    const auto* data = static_cast<const DWORD*>(LockResource(LoadResource(image, resource)));
    if (!data || bytes < 4 || *data != 0xfeff0901) return TRUE;
    Com<ID3DXEffect> fx;
    Require(D3DXCreateEffect(rig.legacy, data, bytes, nullptr, nullptr, 0, nullptr, fx.Put(), nullptr),
            "Load original effect");
    D3DXEFFECT_DESC desc{};
    Require(fx->GetDesc(&desc), "Read material schema");
    ++rig.effects;
    for (UINT i = 0; i < desc.Techniques; ++i) {
        const auto tech = fx->GetTechnique(i);
        Require(fx->SetTechnique(tech), "Select retail technique");
        EditParameters(fx.Get(), desc, 1);
        UINT count;
        Require(fx->Begin(&count, 0), "Begin material");
        for (UINT j = 0; j < count; ++j) {
            D3DXPASS_DESC pass{};
            Require(fx->GetPassDesc(fx->GetPass(tech,j), &pass), "Describe material pass");
            const auto* vs = pass.pVertexShaderFunction;
            const auto* ps = pass.pPixelShaderFunction;
            if (!vs || !ps) continue;
            auto vertex = legacy_program::Translate(vs, D3DXGetShaderSize(vs));
            auto pixel = legacy_program::LinkPixelInputs(vertex, legacy_program::Translate(ps, D3DXGetShaderSize(ps)));
            Stage v(rig, vs, vertex, true), p(rig, ps, pixel, false);
            Require(fx->BeginPass(j), "Begin material pass");
            for (UINT k = 1; k <= 2; ++k) {
                EditParameters(fx.Get(), desc, k);
                Require(fx->CommitChanges(), "Commit dynamic material");
                v.Verify(rig,k-1);
                p.Verify(rig,k-1);
            }
            Require(fx->EndPass(), "End material pass");
            ++rig.passes;
        }
        Require(fx->End(), "End material");
    }
    return TRUE;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%ls: %s\n", name, e.what());
    return FALSE;
}
} // namespace

int wmain(int argc, wchar_t** argv) try {
    if (argc != 2) return 1;
    HMODULE exe = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
    Check(exe != nullptr, "Open game resources");
    const HWND window = CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Com<IDirect3D9Ex> factory;
    Require(Direct3DCreate9Ex(D3D_SDK_VERSION, factory.Put()), "Create effect device factory");
    D3DPRESENT_PARAMETERS params{};
    params.hDeviceWindow = window;
    params.BackBufferWidth = params.BackBufferHeight = 32;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.Windowed = TRUE;
    Com<IDirect3DDevice9Ex> legacy;
    Require(factory->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &params,nullptr,legacy.Put()), "Create D3D9Ex material device");
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Require(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
                              device.Put(),nullptr,context.Put()), "Create native D3D11 device");
    sampler_test::Textures textures(legacy.Get(),device.Get());
    Rig rig{legacy.Get(), device.Get(), context.Get(),textures};
    Check(EnumResourceNamesW(exe, MAKEINTRESOURCEW(10), Inspect, reinterpret_cast<LONG_PTR>(&rig)), "Enumerate retail materials");
    std::printf("%u effects, %u explicit passes, %u stage bindings, %u register comparisons, %u changing stage buffers\n",
                rig.effects,rig.passes,rig.bindings,rig.aliases,rig.changes);
    Check(rig.effects == 31 && rig.passes == 71 && rig.bindings == 284 && rig.aliases && rig.changes, "Incomplete coverage");
    VerifyRuntimeTextureTypes(rig,exe);
    std::printf("%u sampled resource+sampler pairs, %u checks for stale views, %u 2D, %u cube, %u volume unit checks\n",
                textures.checks,textures.clears,textures.shapeCounts[0],textures.shapeCounts[1],textures.shapeCounts[2]);
    Check(textures.checks && textures.clears && textures.shapeCounts[0] && textures.shapeCounts[1] && textures.shapeCounts[2],"Missing material texture coverage");
    context->ClearState();
    legacy.Reset();
    DestroyWindow(window);
    FreeLibrary(exe);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 2;
}
