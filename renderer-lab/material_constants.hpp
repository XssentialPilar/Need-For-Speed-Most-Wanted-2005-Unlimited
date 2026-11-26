#pragma once

#include "movie_d3d.hpp"
#include "legacy_shader_program.hpp"
#include <d3d9.h>
#include <d3d11shader.h>

namespace material {

enum class Bank { Float, Int, Bool };

struct Span {
    Bank bank;
    UINT first;
    UINT count;
    UINT byteOffset;
};

// Build once for each linked program. D3DX evaluates effect parameters,
// preshaders and shared pool values before these register reads take place.
class ConstantLayout {
public:
    ConstantLayout(const DWORD* tokens, size_t length, ID3DBlob* compiled) {
        if (!tokens || length < 8 || !compiled) throw std::invalid_argument("Missing material program");
        vertex = (*tokens >> 16) == 0xfffe;
        movie_pass::Com<ID3D11ShaderReflection> shader;
        movie_pass::Require(D3DReflect(compiled->GetBufferPointer(), compiled->GetBufferSize(),
                                      IID_ID3D11ShaderReflection, shader.AsVoid()), "Reflect material constants");
        D3D11_SHADER_DESC desc{};
        movie_pass::Require(shader->GetDesc(&desc), "Read native shader layout");
        if (!desc.ConstantBuffers) return;
        if (desc.ConstantBuffers != 1) throw std::runtime_error("Unexpected material constant buffer count");
        auto* buffer = shader->GetConstantBufferByIndex(0);
        D3D11_SHADER_BUFFER_DESC cb{};
        movie_pass::Require(buffer->GetDesc(&cb), "Read native constant buffer");
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        movie_pass::Require(shader->GetResourceBindingDescByName(cb.Name, &binding), "Read material buffer slot");
        if (binding.Type != D3D_SIT_CBUFFER || binding.BindPoint || cb.Size % 16)
            throw std::runtime_error("Unexpected native material buffer binding");
        bytes = cb.Size;
        if (legacy_program::IsPixel1(*tokens)) {
            if (bytes != 128) throw std::runtime_error("Unexpected PS1 register array");
            spans.push_back({Bank::Float, 0, 8, 0});
            return;
        }

        using Parsed = std::unique_ptr<const MOJOSHADER_parseData, decltype(&MOJOSHADER_freeParseData)>;
        Parsed parsed(MOJOSHADER_parse(MOJOSHADER_PROFILE_HLSL, legacy_pixel::Entry,
                      reinterpret_cast<const unsigned char*>(tokens), static_cast<unsigned>(length),
                      nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr), MOJOSHADER_freeParseData);
        if (!parsed || parsed->error_count) throw std::runtime_error("Could not reflect legacy registers");
        constexpr const char* names[]{"uniforms_float4", "uniforms_int4", "uniforms_bool"};
        std::array<UINT, 3> used{};
        for (int i = 0; i < parsed->uniform_count; ++i) {
            const auto& entry = parsed->uniforms[i];
            if (entry.constant) continue;
            if (entry.type < 0 || entry.type > 2 || entry.index < 0 || entry.array_count < 0)
                throw std::runtime_error("Invalid legacy constant range");
            const UINT type = static_cast<UINT>(entry.type);
            const UINT count = static_cast<UINT>(std::max(1, entry.array_count));
            D3D11_SHADER_VARIABLE_DESC var{};
            if (FAILED(buffer->GetVariableByName(names[type])->GetDesc(&var))) continue;
            const UINT offset = var.StartOffset + used[type] * 16;
            used[type] += count;
            if (used[type] > (var.Size + 15) / 16 || offset + count * 16 > bytes)
                throw std::runtime_error("Legacy material constants exceed native layout");
            Append({static_cast<Bank>(type), static_cast<UINT>(entry.index), count, offset});
        }
    }

    UINT ByteSize() const { return bytes; }
    const std::vector<Span>& Ranges() const { return spans; }

    // Output belongs to the caller. Publish it only after a successful capture.
    HRESULT Capture(IDirect3DDevice9* device, void* dest, size_t size) const {
        if (!device || size < bytes || (bytes && !dest)) return E_INVALIDARG;
        if (!bytes) return S_OK;
        auto* output = static_cast<BYTE*>(dest);
        std::memset(output, 0, bytes);
        for (const auto& span : spans) {
            auto* target = output + span.byteOffset;
            HRESULT result;
            if (span.bank == Bank::Float) {
                auto* values = reinterpret_cast<float*>(target);
                result = vertex ? device->GetVertexShaderConstantF(span.first, values, span.count)
                                : device->GetPixelShaderConstantF(span.first, values, span.count);
            } else if (span.bank == Bank::Int) {
                auto* values = reinterpret_cast<int*>(target);
                result = vertex ? device->GetVertexShaderConstantI(span.first, values, span.count)
                                : device->GetPixelShaderConstantI(span.first, values, span.count);
            } else {
                std::array<BOOL, 16> values{};
                if (span.count > values.size()) return E_INVALIDARG;
                result = vertex ? device->GetVertexShaderConstantB(span.first, values.data(), span.count)
                                : device->GetPixelShaderConstantB(span.first, values.data(), span.count);
                for (UINT i = 0; i < span.count; ++i) std::memcpy(target + 16 * i, &values[i], sizeof(BOOL));
            }
            if (FAILED(result)) return result;
        }
        return S_OK;
    }

private:
    void Append(Span next) {
        if (!spans.empty()) {
            auto& last = spans.back();
            if (next.bank == last.bank && next.first == last.first + last.count &&
                next.byteOffset == last.byteOffset + last.count * 16) {
                last.count += next.count;
                return;
            }
        }
        spans.push_back(next);
    }
    bool vertex = false;
    UINT bytes = 0;
    std::vector<Span> spans;
};

// Reuses the allocation across passes and binds b0 independently in VS and PS.
class StageConstants11 {
public:
    StageConstants11(ID3D11Device* device, const DWORD* program, size_t length, ID3DBlob* compiled)
        : layout(program, length, compiled), vertex((*program >> 16) == 0xfffe), staging(layout.ByteSize()) {
        if (staging.empty()) return;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = layout.ByteSize();
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        movie_pass::Require(device->CreateBuffer(&desc, nullptr, buffer.Put()), "Allocate material constants");
    }
    HRESULT Bind(IDirect3DDevice9* source, ID3D11DeviceContext* destination) {
        if (!destination) return E_INVALIDARG;
        const auto result = layout.Capture(source, staging.data(), staging.size());
        if (FAILED(result)) return result;
        if (buffer.Get() && staging != uploaded) {
            destination->UpdateSubresource(buffer.Get(), 0, nullptr, staging.data(), 0, 0);
            uploaded = staging;
        }
        auto* bound = buffer.Get();
        if (vertex) destination->VSSetConstantBuffers(0, 1, &bound);
        else destination->PSSetConstantBuffers(0, 1, &bound);
        return S_OK;
    }
    const ConstantLayout& Layout() const { return layout; }
private:
    ConstantLayout layout;
    bool vertex;
    std::vector<BYTE> staging, uploaded;
    movie_pass::Com<ID3D11Buffer> buffer;
};
} // namespace material
