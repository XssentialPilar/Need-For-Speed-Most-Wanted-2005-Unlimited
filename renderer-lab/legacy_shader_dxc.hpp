#pragma once
#include "movie_d3d.hpp"
#include <dxcapi.h>
#include <vector>

namespace legacy_program {

class ModernCompiler {
public:
    explicit ModernCompiler(const wchar_t* library) {
        module.handle = LoadLibraryExW(library, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module.handle) throw std::runtime_error("Could not load DXC library of matching process architecture");
        auto factory = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module.handle, "DxcCreateInstance"));
        if (!factory) throw std::runtime_error("Missing DxcCreateInstance");
        movie_pass::Require(factory(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), compiler.AsVoid()), "Create DXC compiler");
    }
    ModernCompiler(const ModernCompiler&) = delete;
    ModernCompiler& operator=(const ModernCompiler&) = delete;

    void Compile(const std::string& hlsl, bool vertex, bool vulkan, movie_pass::Com<IDxcBlob>& bytecode) {
        DxcBuffer source{hlsl.data(), hlsl.size(), DXC_CP_UTF8};
        std::vector<LPCWSTR> args{L"-E", L"UnlimitedShader", L"-T", vertex ? L"vs_6_0" : L"ps_6_0",
                                  L"-HV", L"2016", L"-Ges"};
        if (vulkan) {
            // D3D9 has independent vertex/pixel constants and sampler spaces.
            // Reserve 32 binding slots per class and 96 per stage in set zero.
            const wchar_t* b = vertex ? L"0" : L"96";
            const wchar_t* t = vertex ? L"32" : L"128";
            const wchar_t* s = vertex ? L"64" : L"160";
            args.insert(args.end(), {L"-spirv", L"-fspv-target-env=vulkan1.1", L"-fvk-use-dx-layout",
                L"-fvk-stage-io-order=decl", L"-fvk-auto-shift-bindings", L"-fvk-b-shift", b, L"0",
                L"-fvk-t-shift", t, L"0", L"-fvk-s-shift", s, L"0"});
            if (vertex) args.push_back(L"-fvk-invert-y");
        }
        movie_pass::Com<IDxcResult> result;
        movie_pass::Require(compiler->Compile(&source, args.data(), static_cast<UINT32>(args.size()), nullptr,
                                              __uuidof(IDxcResult), result.AsVoid()), "Compile native shader");
        HRESULT status;
        movie_pass::Require(result->GetStatus(&status), "Read DXC status");
        if (FAILED(status)) {
            movie_pass::Com<IDxcBlobEncoding> errors;
            result->GetErrorBuffer(errors.Put());
            if (errors.Get() && errors->GetBufferSize())
                throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
            movie_pass::Require(status, "DXC reported an error");
        }
        movie_pass::Require(result->GetResult(bytecode.Put()), "Get native shader bytecode");
        if (!bytecode.Get() || bytecode->GetBufferSize() < 16) throw std::runtime_error("Empty native shader");
    }
private:
    struct Module { HMODULE handle = nullptr; ~Module() { if (handle) FreeLibrary(handle); } } module;
    movie_pass::Com<IDxcCompiler3> compiler;
};

} // namespace legacy_program

