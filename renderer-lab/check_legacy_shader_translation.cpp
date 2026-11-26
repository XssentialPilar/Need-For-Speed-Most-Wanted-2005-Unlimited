#include "movie_d3d.hpp"
#include "legacy_shader_program.hpp"
#include "legacy_shader_dxc.hpp"
#ifdef UNLIMITED_NATIVE_PIPELINE_TEST
#include "legacy_pipeline_check.hpp"
#endif
#include "legacy_pixel_comparison.hpp"

#include <d3dx9.h>


#include <algorithm>
#include <map>
#include <memory>
#include <string>

namespace {

using movie_pass::Com;
using movie_pass::Require;
using Bytecode = std::vector<unsigned char>;

struct Use {
    std::wstring resource;
    std::string technique;
    std::string pass;
    unsigned index;
};

struct Inventory {
    IDirect3DDevice9* reflectionDevice;
    std::map<Bytecode, std::vector<Use>> shaders;
    struct Pass { Use use; const Bytecode* vertex; const Bytecode* pixel; };
    std::vector<Pass> explicitPairs;
    struct Fixed { Use use; unsigned techniqueIndex; };
    std::vector<Fixed> fixed;
    struct FixedVariant { Use use; std::string vertex, pixel; };
    std::vector<FixedVariant> fixedVariants;
    unsigned effects = 0, passes = 0, nativeStages = 0, rejectedEffects = 0;
};

const Bytecode* AddShader(Inventory& inventory, const DWORD* program, const Use& use) {
    if (!program) {
        ++inventory.nativeStages;
        return nullptr;
    }
    const auto size = D3DXGetShaderSize(program);
    if (size < 8 || size > 1024 * 1024 || size % 4) throw std::runtime_error("Invalid extracted shader size");
    const auto* start = reinterpret_cast<const unsigned char*>(program);
    auto result = inventory.shaders.try_emplace(Bytecode(start, start + size));
    result.first->second.push_back(use);
    return &result.first->first;
}

BOOL CALLBACK CollectEffect(HMODULE image, LPCWSTR type, LPWSTR name, LONG_PTR context) try {
    auto& inventory = *reinterpret_cast<Inventory*>(context);
    const auto found = FindResourceW(image, name, type);
    const DWORD size = SizeofResource(image, found);
    const void* content = LockResource(LoadResource(image, found));
    std::uint32_t signature = 0;
    if (content && size >= sizeof(signature)) std::memcpy(&signature, content, sizeof(signature));
    if (signature != 0xfeff0901) return TRUE;

    std::wstring label = IS_INTRESOURCE(name)
        ? std::wstring(L"#") + std::to_wstring(reinterpret_cast<ULONG_PTR>(name)) : std::wstring(name);
    Com<ID3DXEffect> effect;
    const auto loaded = D3DXCreateEffect(inventory.reflectionDevice, content, size, nullptr, nullptr, 0, nullptr,
                                         effect.Put(), nullptr);
    D3DXEFFECT_DESC description{};
    if (FAILED(loaded) || FAILED(effect->GetDesc(&description))) {
        std::wprintf(L"EffectReflectionError\t%ls\t%08lx\n", label.c_str(), static_cast<unsigned long>(loaded));
        ++inventory.rejectedEffects;
        return TRUE;
    }
    ++inventory.effects;
    for (unsigned i = 0; i < description.Techniques; ++i) {
        const auto technique = effect->GetTechnique(i);
        D3DXTECHNIQUE_DESC techniqueInfo{};
        Require(effect->GetTechniqueDesc(technique, &techniqueInfo), "GetTechniqueDesc");
        for (unsigned j = 0; j < techniqueInfo.Passes; ++j) {
            D3DXPASS_DESC pass{};
            Require(effect->GetPassDesc(effect->GetPass(technique, j), &pass), "GetPassDesc");
            const Use use{label, techniqueInfo.Name ? techniqueInfo.Name : "", pass.Name ? pass.Name : "", j};
            const auto* vertex = AddShader(inventory, pass.pVertexShaderFunction, use);
            const auto* pixel = AddShader(inventory, pass.pPixelShaderFunction, use);
            if (vertex && pixel) inventory.explicitPairs.push_back({use, vertex, pixel});
            if (!vertex && !pixel) inventory.fixed.push_back({use, i});
            ++inventory.passes;
        }
    }
    return TRUE;
} catch (const std::exception& error) {
    std::wprintf(L"EffectReflectionError\t%hs\n", error.what());
    return FALSE;
}

struct CompileResult {
    const char* phase;
    std::string diagnostics;
    bool compiled = false;
    bool accepted = false;
};

CompileResult TestShader(const Bytecode& shader, ID3D11Device* device, bool vertex) {
    std::string program;
    try {
        program = legacy_program::Translate(shader.data(), shader.size());
    } catch (const std::exception& error) {
        return {"HLSL", error.what()};
    }
    Com<ID3DBlob> dxbc;
    Com<ID3DBlob> messages;
    const auto result = D3DCompile(program.data(), program.size(), "legacy_program.hlsl", nullptr, nullptr,
                                   legacy_pixel::Entry, vertex ? "vs_5_0" : "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS,
                                   0, dxbc.Put(), messages.Put());
    if (FAILED(result) || !dxbc.Get()) {
        std::string diagnostics = messages.Get()
            ? static_cast<const char*>(messages->GetBufferPointer()) : "No DXBC emitted";
        std::replace_if(diagnostics.begin(), diagnostics.end(), [](char character) {
            return character == '\r' || character == '\n' || character == '\t';
        }, ' ');
        return {"D3DCompile", std::move(diagnostics)};
    }
    HRESULT accepted;
    if (vertex) {
        Com<ID3D11VertexShader> object;
        accepted = device->CreateVertexShader(dxbc->GetBufferPointer(), dxbc->GetBufferSize(), nullptr, object.Put());
    } else {
        Com<ID3D11PixelShader> object;
        accepted = device->CreatePixelShader(dxbc->GetBufferPointer(), dxbc->GetBufferSize(), nullptr, object.Put());
    }
    if (FAILED(accepted)) {
        char error[16]{};
        std::snprintf(error, sizeof(error), "0x%08lx", static_cast<unsigned long>(accepted));
        return {"D3D11", error, true, false};
    }
    return {"Accepted", {}, true, true};
}

std::uint64_t Fingerprint(const Bytecode& program) {
    std::uint64_t value = 0xcbf29ce484222325ull;
    for (auto byte : program) {
        value ^= byte;
        value *= 0x100000001b3ull;
    }
    return value;
}

struct Totals {
    unsigned unique = 0, compiled = 0, accepted = 0, uses = 0;
};

bool PrintCoverage(const Inventory& inventory, ID3D11Device* device) {
    std::map<std::uint32_t, Totals> models;
    Totals vertexTotal, pixelTotal;
    for (const auto& shader : inventory.shaders) {
        const auto& bytes = shader.first;
        const auto& uses = shader.second;
        std::uint32_t token = 0;
        std::memcpy(&token, bytes.data(), sizeof(token));
        const bool vertex = token >> 16 == 0xfffe;
        if (!vertex && token >> 16 != 0xffff) throw std::runtime_error("Unrecognized shader profile");
        const auto result = TestShader(bytes, device, vertex);
        auto& byType = vertex ? vertexTotal : pixelTotal;
        for (auto* totals : {&byType, &models[token]}) {
            ++totals->unique;
            totals->compiled += result.compiled;
            totals->accepted += result.accepted;
            totals->uses += static_cast<unsigned>(uses.size());
        }
        if (!result.accepted) {
            std::wprintf(L"Failure\t%ls_%u_%u\t%016llx\t%hs\t%hs\n", vertex ? L"vs" : L"ps",
                         (token >> 8) & 255, token & 255, static_cast<unsigned long long>(Fingerprint(bytes)),
                         result.phase, result.diagnostics.c_str());
            for (const auto& use : uses) {
                std::wprintf(L"Use\t%ls\t%hs\t%hs\t%u\n", use.resource.c_str(), use.technique.c_str(),
                             use.pass.c_str(), use.index);
            }
        }
    }
    std::wprintf(L"SourceModel\tUnique\tCompiled\tAccepted\tPassUses\n");
    for (const auto& profile : models) {
        const auto token = profile.first;
        const auto& counts = profile.second;
        std::wprintf(L"%ls_%u_%u\t%u\t%u\t%u\t%u\n", token >> 16 == 0xfffe ? L"vs" : L"ps",
                     (token >> 8) & 255, token & 255, counts.unique, counts.compiled, counts.accepted, counts.uses);
    }
    std::wprintf(L"Vertex\t%u\t%u\t%u\t%u\n", vertexTotal.unique, vertexTotal.compiled, vertexTotal.accepted, vertexTotal.uses);
    std::wprintf(L"Pixel\t%u\t%u\t%u\t%u\n", pixelTotal.unique, pixelTotal.compiled, pixelTotal.accepted, pixelTotal.uses);
    std::wprintf(L"Inventory\t%u effects\t%u passes\t%u fixed or absent stages\t%u rejected effects\n",
                 inventory.effects, inventory.passes, inventory.nativeStages, inventory.rejectedEffects);
    return vertexTotal.unique == vertexTotal.accepted && pixelTotal.unique == pixelTotal.accepted;
}

bool CheckLinks(const Inventory& inventory, ID3D11Device* device) {
    unsigned succeeded = 0;
    for (const auto& pair : inventory.explicitPairs) {
        try {
            const auto vertex = legacy_program::Translate(pair.vertex->data(), pair.vertex->size());
            const auto pixel = legacy_program::LinkPixelInputs(vertex, legacy_program::Translate(pair.pixel->data(), pair.pixel->size()));
            Com<ID3DBlob> vs, ps;
            legacy_comparison::Compile(vertex, legacy_pixel::Entry, "vs_5_0", vs);
            legacy_comparison::Compile(pixel, legacy_pixel::Entry, "ps_5_0", ps);
            Com<ID3D11ShaderReflection> v, p;
            Require(D3DReflect(vs->GetBufferPointer(), vs->GetBufferSize(), IID_ID3D11ShaderReflection, v.AsVoid()), "Reflect VS linkage");
            Require(D3DReflect(ps->GetBufferPointer(), ps->GetBufferSize(), IID_ID3D11ShaderReflection, p.AsVoid()), "Reflect PS linkage");
            D3D11_SHADER_DESC vd{}, pd{};
            Require(v->GetDesc(&vd), "Read VS signature");
            Require(p->GetDesc(&pd), "Read PS signature");
            for (UINT i = 0; i < pd.InputParameters; ++i) {
                D3D11_SIGNATURE_PARAMETER_DESC in{};
                Require(p->GetInputParameterDesc(i, &in), "Read PS interpolant");
                if (!in.ReadWriteMask) continue;
                bool found = false;
                for (UINT j = 0; j < vd.OutputParameters; ++j) {
                    D3D11_SIGNATURE_PARAMETER_DESC out{};
                    Require(v->GetOutputParameterDesc(j, &out), "Read VS interpolant");
                    if (_stricmp(in.SemanticName, out.SemanticName) || in.SemanticIndex != out.SemanticIndex) continue;
                    found = in.Register == out.Register && in.ComponentType == out.ComponentType &&
                            (out.Mask & in.ReadWriteMask) == in.ReadWriteMask;
                    break;
                }
                if (!found) throw std::runtime_error("Native input register mismatch for " + std::string(in.SemanticName) +
                                                      std::to_string(in.SemanticIndex));
            }
            Com<ID3D11VertexShader> nativeVS;
            Com<ID3D11PixelShader> nativePS;
            Require(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, nativeVS.Put()), "Realize linked VS");
            Require(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, nativePS.Put()), "Realize linked PS");
            ++succeeded;
        } catch (const std::exception& ex) {
            std::wprintf(L"InterfaceFailure\t%ls\t%hs/%hs\t%hs\n", pair.use.resource.c_str(), pair.use.technique.c_str(),
                         pair.use.pass.c_str(), ex.what());
        }
    }
    std::wprintf(L"LinkedVS-PS\t%u accepted pass pairs\t%u explicit pairs\n", succeeded, UINT(inventory.explicitPairs.size()));
    return succeeded > 0 && succeeded == inventory.explicitPairs.size();
}

bool ComparePixelOutputs(const Inventory& inventory, ID3D11Device* nativeDevice) {
    legacy_comparison::Legacy legacy(inventory.reflectionDevice);
    legacy_comparison::Modern modern(nativeDevice);
    unsigned images = 0, failed = 0, largestError = 0;
    for (const auto& item : inventory.shaders) {
        const auto& bytes = item.first;
        DWORD version;
        std::memcpy(&version, bytes.data(), sizeof(version));
        if (version != 0xffff0101 && version != 0xffff0104) continue;
        for (unsigned variant = 0; variant < 2; ++variant) {
            const auto values = legacy_comparison::Inputs(variant);
            const auto original = legacy.Draw(bytes.data(), values);
            const auto converted = modern.Draw(bytes.data(), bytes.size(), values);
            unsigned channelError = 0, differentPixels = 0;
            for (size_t p = 0; p < original.size(); p += 4) {
                unsigned pixelError = 0;
                for (unsigned c = 0; c < 4; ++c) {
                    pixelError = std::max(pixelError, unsigned(std::abs(int(original[p+c]) - int(converted[p+c]))));
                }
                channelError = std::max(channelError, pixelError);
                if (pixelError > 1) ++differentPixels;
            }
            largestError = std::max(largestError, channelError);
            if (differentPixels) {
                ++failed;
                std::wprintf(L"PixelDifference\t%ls\t%016llx\t%u\t%u pixels\tMaxDelta %u\n",
                             item.second.front().resource.c_str(), static_cast<unsigned long long>(Fingerprint(bytes)),
                             variant, differentPixels, channelError);
            }
            ++images;
        }
    }
    std::wprintf(L"OriginalD3D9-vs-D3D11\t%u images\t%u pixels\t%u failed images\tmax channel error %u\n",
                 images, images * legacy_comparison::Side * legacy_comparison::Side, failed, largestError);
    return images > 0 && failed == 0;
}

bool CheckModernPrograms(const Inventory& inventory, const wchar_t* compilerPath, bool vulkan) {
    legacy_program::ModernCompiler compiler(compilerPath);
#ifdef UNLIMITED_NATIVE_PIPELINE_TEST
    using legacy_pipeline::D3D12;
    using legacy_pipeline::Vulkan;
    std::unique_ptr<legacy_pipeline::VkHost> host;
    std::vector<std::unique_ptr<D3D12>> dxDevices;
    std::vector<std::unique_ptr<Vulkan>> vkDevices;
    if (vulkan) {
        host = std::make_unique<legacy_pipeline::VkHost>();
        uint32_t count = 0;
        legacy_pipeline::RequireVk(vkEnumeratePhysicalDevices(host->instance, &count, nullptr), "Enumerate Vulkan GPUs");
        if (!count) throw std::runtime_error("No Vulkan GPUs");
        std::vector<VkPhysicalDevice> gpus(count);
        legacy_pipeline::RequireVk(vkEnumeratePhysicalDevices(host->instance, &count, gpus.data()), "Get Vulkan GPUs");
        for (auto gpu : gpus) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(gpu, &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
            vkDevices.push_back(std::make_unique<Vulkan>(gpu));
            std::wprintf(L"VulkanAdapter\t%hs\n", properties.deviceName);
        }
        if (vkDevices.empty()) throw std::runtime_error("No Vulkan hardware device");
    } else {
        Com<IDXGIFactory1> factory;
        Require(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.AsVoid()), "Enumerate DX12 GPUs");
        Com<IDXGIAdapter1> gpu;
        for (unsigned i = 0;; ++i) {
            auto result = factory->EnumAdapters1(i, gpu.Put());
            if (result == DXGI_ERROR_NOT_FOUND) break;
            Require(result, "Get DX12 GPU");
            DXGI_ADAPTER_DESC1 info{};
            Require(gpu->GetDesc1(&info), "Get DX12 GPU information");
            if (info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            dxDevices.push_back(std::make_unique<D3D12>(gpu.Get()));
            std::wprintf(L"DX12Adapter\t%ls\n", info.Description);
        }
        if (dxDevices.empty()) throw std::runtime_error("No DX12 hardware device");
    }
    unsigned psos = 0;
#endif
    unsigned checked = 0;
    auto checkPair = [&](const std::string& vertex, const std::string& pixel) {
        Com<IDxcBlob> vs, ps;
        compiler.Compile(vertex, true, vulkan, vs);
        compiler.Compile(pixel, false, vulkan, ps);
#ifdef UNLIMITED_NATIVE_PIPELINE_TEST
        for (auto& d : dxDevices) { d->Create(vertex, vs.Get(), ps.Get()); ++psos; }
        for (auto& d : vkDevices) { d->Create(vs.Get(), ps.Get()); ++psos; }
#endif
    };
    for (const auto& pair : inventory.explicitPairs) {
        try {
            const auto vertex = legacy_program::Translate(pair.vertex->data(), pair.vertex->size());
            const auto pixel = legacy_program::LinkPixelInputs(vertex, legacy_program::Translate(pair.pixel->data(), pair.pixel->size()));
            checkPair(vertex, pixel);
            ++checked;
        } catch (const std::exception& error) {
            std::wprintf(L"NativeFailure\t%ls\t%ls\t%hs/%hs\t%hs\n", vulkan ? L"SPIR-V" : L"DXIL",
                         pair.use.resource.c_str(), pair.use.technique.c_str(), pair.use.pass.c_str(), error.what());
        }
    }
    std::wprintf(L"%ls\t%u compiled pass pairs\t%u explicit pairs\n", vulkan ? L"SPIR-V" : L"DXIL",
                 checked, UINT(inventory.explicitPairs.size()));
    unsigned fixedChecked = 0;
    for (const auto& variant : inventory.fixedVariants) {
        try {
            checkPair(variant.vertex,variant.pixel);
            ++fixedChecked;
        } catch (const std::exception& error) {
            std::wprintf(L"NativeFixedFailure\t%ls\t%ls\t%hs\t%hs\n", vulkan ? L"SPIR-V" : L"DXIL",
                         variant.use.resource.c_str(),variant.use.technique.c_str(),error.what());
        }
    }
    std::wprintf(L"%ls-fixed\t%u compiled state variants\t%u captured variants\n", vulkan ? L"SPIR-V" : L"DXIL",
                 fixedChecked, UINT(inventory.fixedVariants.size()));
#ifdef UNLIMITED_NATIVE_PIPELINE_TEST
    std::wprintf(L"%ls-hardware-pipelines\t%u created\t%u devices\n", vulkan ? L"SPIR-V" : L"DXIL",
                 psos, UINT(vkDevices.size() + dxDevices.size()));
#endif
    return checked == inventory.explicitPairs.size() && fixedChecked == inventory.fixedVariants.size();
}

bool CheckFixedPasses(Inventory& inventory, HMODULE image, ID3D11Device* nativeDevice) {
    auto* d = inventory.reflectionDevice;
    legacy_comparison::Legacy source(d);
    legacy_comparison::Modern target(nativeDevice);
    Com<IDirect3DStateBlock9> baseline;
    Require(d->CreateStateBlock(D3DSBT_ALL, baseline.Put()), "Snapshot fixture baseline");
    unsigned checked = 0, failed = 0, maxError = 0, vertexImages = 0, vertexFailures = 0, vertexError = 0;
    for (const auto& pass : inventory.fixed) {
        const auto resource = FindResourceW(image, pass.use.resource.c_str(), MAKEINTRESOURCEW(10));
        const void* content = LockResource(LoadResource(image, resource));
        Com<ID3DXEffect> effect;
        Require(D3DXCreateEffect(d, content, SizeofResource(image, resource), nullptr, nullptr, 0, nullptr,
                                 effect.Put(), nullptr), "Load fixture effect");
        D3DXEFFECT_DESC description{};
        Require(effect->GetDesc(&description), "Read fixed effect fields");
        for (unsigned variant = 0; variant < 2; ++variant) {
            Require(baseline->Apply(), "Restore common D3D9 baseline");
            Require(d->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2),
                    "Seed geometry for passes that inherit their declaration");
            D3DXMATRIX world, view, projection, texture;
            D3DXMatrixIdentity(&world); D3DXMatrixIdentity(&view); D3DXMatrixIdentity(&projection); D3DXMatrixIdentity(&texture);
            if (variant) { world._11=.87f; world._22=1.08f; world._41=.04f; projection._14=.125f; texture._31=.13f; }
            Require(d->SetTransform(D3DTS_WORLD,&world), "Seed world matrix");
            Require(d->SetTransform(D3DTS_VIEW,&view), "Seed view matrix");
            Require(d->SetTransform(D3DTS_PROJECTION,&projection), "Seed projection matrix");
            Require(d->SetTransform(D3DTS_TEXTURE0,&texture), "Seed texture matrix");
            D3DMATERIAL9 material{};
            material.Diffuse={.87f,.63f,.54f,.75f}; material.Ambient={.21f,.32f,.18f,1};
            material.Specular={.3f,.4f,.2f,1}; material.Power=4;
            Require(d->SetMaterial(&material), "Seed fixture material");
            D3DLIGHT9 light{};
            light.Type=D3DLIGHT_DIRECTIONAL; light.Diffuse={.64f,.52f,.72f,1}; light.Ambient={.22f,.09f,.13f,1};
            light.Specular={.3f,.42f,.15f,1}; light.Direction={-.25f,.09f,1};
            Require(d->SetLight(0,&light), "Seed directional light");
            Require(d->LightEnable(0,TRUE), "Enable fixture light");
            Require(d->SetRenderState(D3DRS_AMBIENT,D3DCOLOR_XRGB(48,31,29)), "Seed global ambient");
            for (unsigned j = 0; j < description.Parameters; ++j) {
                auto handle = effect->GetParameter(nullptr, j);
                D3DXPARAMETER_DESC f{};
                Require(effect->GetParameterDesc(handle, &f), "Read parameter");
                if (f.Type >= D3DXPT_TEXTURE && f.Type <= D3DXPT_TEXTURECUBE &&
                    f.Type != D3DXPT_TEXTURE3D && f.Type != D3DXPT_TEXTURECUBE)
                    Require(effect->SetTexture(handle, source.Texture(0)), "Supply material texture");
            }
            const DWORD color = variant ? D3DCOLOR_ARGB(205,120,70,32) : D3DCOLOR_ARGB(142,85,210,93);
            Require(d->SetRenderState(D3DRS_TEXTUREFACTOR, color), "Seed inherited texture factor");
            Require(d->SetTextureStageState(1, D3DTSS_COLOROP, variant ? D3DTOP_ADD : D3DTOP_DISABLE), "Seed inherited color op");
            Require(d->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CONSTANT), "Seed inherited argument");
            Require(d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1), "Seed inherited alpha op");
            Require(d->SetTextureStageState(1, D3DTSS_CONSTANT, color), "Seed inherited stage constant");
            // Seed programmable shaders. The pass must explicitly clear both.
            Com<IDirect3DVertexShader9> beforeVS;
            Require(d->GetVertexShader(beforeVS.Put()), "Read fixture vertex stage");
            if (!beforeVS.Get()) throw std::runtime_error("Missing seeded programmable vertex stage");
            const DWORD marker[] = {0xffff0101, D3DSIO_MOV, 0x800f0000, 0x90e40000, D3DSIO_END};
            Com<IDirect3DPixelShader9> beforePS;
            Require(d->CreatePixelShader(marker, beforePS.Put()), "Create seed PS");
            Require(d->SetPixelShader(beforePS.Get()), "Seed programmable fragment stage");

            Require(effect->SetTechnique(effect->GetTechnique(pass.techniqueIndex)), "Select fixed technique");
            UINT passes;
            Require(effect->Begin(&passes, D3DXFX_DONOTSAVESTATE), "Begin fixed technique");
            Require(effect->BeginPass(pass.use.index), "Apply actual fixed effect state");
            Com<IDirect3DVertexShader9> vs;
            Com<IDirect3DPixelShader9> ps;
            Require(d->GetVertexShader(vs.Put()), "Read fixed VS");
            Require(d->GetPixelShader(ps.Put()), "Read fixed PS");
            if (vs.Get() || ps.Get()) throw std::runtime_error("Pass retained a programmable shader");
            const auto state = legacy_fixed::CapturePixel(d);
            const auto vertex = legacy_fixed::CaptureVertex(d,state,{0,1,2});
            const legacy_comparison::FixedGeometry geometry(d);
            const auto originalGeometry = source.DrawFixedGeometry(state,geometry);
            const auto nativeGeometry = target.DrawFixedGeometry(vertex,state,geometry);
            unsigned fullError = 0, shaded = 0;
            for (size_t i = 0; i < originalGeometry.size(); ++i)
                fullError = std::max(fullError,unsigned(std::abs(int(originalGeometry[i])-int(nativeGeometry[i]))));
            for (size_t i = 0; i < originalGeometry.size(); i+=4)
                if (originalGeometry[i]!=255 || originalGeometry[i+1]!=0 || originalGeometry[i+2]!=255 || originalGeometry[i+3]!=255) ++shaded;
            if (!shaded || shaded >= legacy_comparison::Side*legacy_comparison::Side) throw std::runtime_error("Degenerate fixed pass stimulus");
            vertexError=std::max(vertexError,fullError);
            if(fullError>1) {
                ++vertexFailures;
                std::wprintf(L"FixedGeometryDifference\t%ls\t%hs\tvariant=%u\tmax channel error=%u\n",
                             pass.use.resource.c_str(), pass.use.technique.c_str(), variant, fullError);
            }
            ++vertexImages;
            inventory.fixedVariants.push_back({pass.use,legacy_fixed::VertexShader(vertex),legacy_fixed::PixelShader(state)});
            Require(effect->EndPass(), "Finish fixed pass");
            Require(effect->End(), "Finish fixed technique");
            Require(baseline->Apply(), "Restore fixture raster state");
            try {
                const auto old = source.DrawCombiner(state);
                const auto current = target.DrawHlsl(legacy_fixed::PixelShader(state), &state.constants, sizeof(state.constants));
                unsigned error = 0;
                for (size_t i = 0; i < old.size(); ++i)
                    error = std::max(error, unsigned(std::abs(int(current[i]) - int(old[i]))));
                ++checked;
                maxError = std::max(maxError, error);
                if (error > 1) {
                    ++failed;
                    std::wprintf(L"FixedPixelDifference\t%ls\t%hs\tvariant=%u\tmax channel error=%u\n",
                                 pass.use.resource.c_str(), pass.use.technique.c_str(), variant, error);
                }
            } catch (const std::exception& error) {
                ++failed;
                std::wprintf(L"FixedPixelFailure\t%ls\t%hs\t%hs\n",
                             pass.use.resource.c_str(), pass.use.technique.c_str(), error.what());
            }
        }
    }
    std::wprintf(L"FixedCombiners\t%u passes\t%u images\t%u failed\tmax channel error %u\n",
                 UINT(inventory.fixed.size()), checked, failed, maxError);
    std::wprintf(L"FixedGeometryAndCombiners\t%u images\t%u failed\tmax channel error %u\n",vertexImages,vertexFailures,vertexError);
    return !vertexFailures && !failed && checked == inventory.fixed.size() * 2 && checked > 0;
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
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr, "Usage: check_legacy_shader_translation.exe <speed.exe> [<DXIL compiler DLL> <SPIR-V compiler DLL>]\n");
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
        Com<IDirect3DDevice9Ex> reflectionDevice;
        Require(api->CreateDeviceEx(0, D3DDEVTYPE_HAL, window.handle, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &present, nullptr, reflectionDevice.Put()), "Create D3D9Ex reflection device");
        Com<ID3D11Device> nativeDevice;
        Require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                  D3D11_SDK_VERSION, nativeDevice.Put(), nullptr, nullptr), "Create native D3D11 device");
        Inventory inventory{};
        inventory.reflectionDevice = reflectionDevice.Get();
        if (!EnumResourceNamesW(game.handle, MAKEINTRESOURCEW(10), &CollectEffect,
                                reinterpret_cast<LONG_PTR>(&inventory))) {
            throw std::runtime_error("Could not enumerate executable RCDATA");
        }
        const bool complete = PrintCoverage(inventory, nativeDevice.Get());
        const bool parity = ComparePixelOutputs(inventory, nativeDevice.Get());
        const bool linked = CheckLinks(inventory, nativeDevice.Get());
        const bool fixed = CheckFixedPasses(inventory, game.handle, nativeDevice.Get());
        bool modern = true;
        if (argc == 4) {
            const bool dxil = CheckModernPrograms(inventory, argv[2], false);
            const bool spirv = CheckModernPrograms(inventory, argv[3], true);
            modern = dxil && spirv;
        }
        return modern && fixed && linked && parity && complete && inventory.effects && !inventory.rejectedEffects ? 0 : 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 3;
    }
}
