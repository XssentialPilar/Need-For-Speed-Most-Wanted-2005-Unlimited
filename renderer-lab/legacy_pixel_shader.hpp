#pragma once

#include <d3d9types.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace legacy_pixel {

// D3D9 PS 1.x infers the texture type and pre-1.4 projection from device state.
// Include both in the program cache key when a material changes its bindings.
enum class TextureShape { Flat, Cube, Volume };
struct Stage {
    TextureShape shape = TextureShape::Flat;
    unsigned projectionCount = 0; // 0, or D3DTTFF_COUNT{2,3,4} with PROJECTED
};
using Stages = std::array<Stage, 6>;
inline constexpr char Entry[] = "UnlimitedShader";

namespace detail {

using Word = std::uint32_t;
using std::string;
using std::to_string;

inline void Check(bool valid, const char* reason) {
    if (!valid) throw std::invalid_argument(reason);
}

class Reader {
public:
    Reader(const void* bytes, size_t size)
        : next(static_cast<const unsigned char*>(bytes)), remaining(size) {
        Check(bytes && size >= 8 && size % 4 == 0, "Invalid PS 1.x bytecode size");
    }
    Word Read() {
        Check(remaining >= 4, "Truncated PS 1.x bytecode");
        Word value;
        std::memcpy(&value, next, 4);
        next += 4;
        remaining -= 4;
        return value;
    }
    void SkipWords(unsigned count) {
        Check(count <= remaining / 4, "Truncated shader comment");
        next += count * 4;
        remaining -= count * 4;
    }
    bool Empty() const { return remaining == 0; }
private:
    const unsigned char* next;
    size_t remaining;
};

struct Instruction {
    Word opcode = 0, destination = 0;
    std::array<Word, 4> args{};
    unsigned count = 0;
    bool coissued = false;
};

inline unsigned SourceCount(Word op, bool phaseModel) {
    switch (op) {
        case D3DSIO_NOP: case D3DSIO_PHASE: return 0;
        case D3DSIO_TEXCOORD: case D3DSIO_TEX: return phaseModel;
        case D3DSIO_MOV: case D3DSIO_TEXREG2AR: case D3DSIO_TEXREG2GB: return 1;
        case D3DSIO_ADD: case D3DSIO_SUB: case D3DSIO_DP3: case D3DSIO_MUL: return 2;
        case D3DSIO_LRP: case D3DSIO_CMP: case D3DSIO_MAD: return 3;
        case D3DSIO_DEF: return 4;
        default: throw std::invalid_argument("Unsupported PS 1.x opcode " + to_string(op));
    }
}

inline unsigned RegType(Word value) {
    Check((value & 0x80000000u) != 0 && (value & D3DSHADER_ADDRESSMODE_MASK) == 0, "Invalid PS 1.x register");
    return ((value & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT) |
           ((value & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2);
}
inline unsigned Index(Word value) { return value & D3DSP_REGNUM_MASK; }

inline string WriteMask(Word reg) {
    string lanes;
    for (unsigned i = 0; i < 4; ++i) if (reg & (D3DSP_WRITEMASK_0 << i)) lanes += "xyzw"[i];
    Check(!lanes.empty(), "Empty PS 1.x destination mask");
    return "." + lanes;
}

class Emitter {
public:
    Emitter(bool phaseModel, const Stages& stages) : phaseModel(phaseModel), stages(stages) {}

    string Emit(const std::vector<Instruction>& ops) {
        string body;
        for (size_t i = 0; i < ops.size(); ++i) {
            const auto& first = ops[i];
            Check(!first.coissued, "Orphan PS 1.x co-issue");
            if (first.opcode == D3DSIO_NOP) continue;
            if (first.opcode == D3DSIO_DEF) {
                Check(RegType(first.destination) == D3DSPR_CONST && Index(first.destination) < 8,
                      "Invalid PS 1.x literal constant");
                const auto index = Index(first.destination);
                literals[index] = true;
                body += "float4 c" + to_string(index) + " = asfloat(uint4(";
                for (unsigned n = 0; n < 4; ++n) body += (n ? "," : "") + to_string(first.args[n]) + "u";
                body += "));\n";
                continue;
            }
            if (first.opcode == D3DSIO_PHASE) {
                Check(phaseModel, "Unexpected PS 1.x phase marker");
                // Alpha components are undefined after the phase transition.
                for (unsigned r = 0; r < 6; ++r) body += "r" + to_string(r) + ".w = 0;\n";
                continue;
            }
            const bool pair = i + 1 < ops.size() && ops[i + 1].coissued;
            body += "{\nfloat4 a = " + Expression(first) + ";\n";
            if (pair) body += "float4 b = " + Expression(ops[i + 1]) + ";\n";
            body += Commit(first.destination, "a");
            if (pair) body += Commit(ops[++i].destination, "b");
            body += "}\n";
        }

        string hlsl = "cbuffer PixelConstants : register(b0) { float4 constants[8]; };\n";
        // DXBC connects stages by register. Keep the producer's canonical
        // output signature, including the register occupied by SV_Position.
        string inputs = "float4 pos : SV_Position;\nfloat4 v0 : COLOR0;\nfloat4 v1 : COLOR1;\n";
        for (unsigned n = 0; n < 6; ++n) {
            const string id = to_string(n);
            if (textures & (1u << n)) {
                const auto shape = stages[n].shape;
                const char* type = shape == TextureShape::Cube ? "TextureCube" :
                                   shape == TextureShape::Volume ? "Texture3D" : "Texture2D";
                hlsl += string(type) + "<float4> image" + id + " : register(t" + id + ");\n";
                hlsl += "SamplerState sampler" + id + " : register(s" + id + ");\n";
            }
            inputs += "float4 uv" + id + " : TEXCOORD" + id + ";\n";
        }
        hlsl += "struct Interpolants {\n" + inputs + "};\n";
        hlsl += string("float4 ") + Entry + "(Interpolants input) : SV_Target {\n";
        for (unsigned n = 0; n < 6; ++n) hlsl += "float4 r" + to_string(n) + "=0, t" + to_string(n) + "=0;\n";
        return hlsl + body + "return r0;\n}\n";
    }

private:
    string UV(unsigned n) {
        Check(n < 6, "Invalid PS 1.x texture coordinate");
        return "input.uv" + to_string(n);
    }

    string Register(Word reg, bool write) {
        const auto index = Index(reg);
        switch (RegType(reg)) {
            case D3DSPR_TEMP:
                Check(index < (phaseModel ? 6u : 2u), "Invalid PS 1.x temporary");
                return "r" + to_string(index);
            case D3DSPR_TEXTURE:
                Check(index < (phaseModel ? 6u : 4u), "Invalid PS 1.x texture register");
                if (phaseModel) {
                    Check(!write, "Attempted to write PS 1.4 input");
                    return UV(index);
                }
                return "t" + to_string(index);
            case D3DSPR_INPUT:
                Check(!write && index < 2, "Invalid PS 1.x color");
                return "saturate(input.v" + to_string(index) + ")";
            case D3DSPR_CONST:
                Check(!write && index < 8, "Invalid PS 1.x constant");
                // PS 1.x constant registers (including DEF) are restricted
                // to [-1,1]; e.g. the world bone pass contains a literal 2.
                return "clamp(" + (literals[index] ? "c" + to_string(index) : "constants[" + to_string(index) + "]") + ",-1,1)";
            default: throw std::invalid_argument("Unsupported PS 1.x register class");
        }
    }

    string Source(Word reg) {
        const string regName = Register(reg, false);
        string value = regName + ".";
        for (unsigned i = 0; i < 4; ++i) value += "xyzw"[(reg >> (D3DSP_SWIZZLE_SHIFT + 2 * i)) & 3];
        switch (reg & D3DSP_SRCMOD_MASK) {
            case D3DSPSM_NONE: return value;
            case D3DSPSM_NEG: return "(-" + value + ")";
            case D3DSPSM_BIAS: return "(" + value + "-0.5)";
            case D3DSPSM_BIASNEG: return "(0.5-" + value + ")";
            case D3DSPSM_SIGN: return "(" + value + "*2-1)";
            case D3DSPSM_SIGNNEG: return "(1-" + value + "*2)";
            case D3DSPSM_COMP: return "(1-" + value + ")";
            case D3DSPSM_X2: return "(" + value + "*2)";
            case D3DSPSM_X2NEG: return "(" + value + "*-2)";
            case D3DSPSM_DZ: return "(" + value + "/" + regName + ".z)";
            case D3DSPSM_DW: return "(" + value + "/" + regName + ".w)";
            default: throw std::invalid_argument("Unsupported PS 1.x source modifier");
        }
    }

    string Sample(unsigned stage, const string& coords, bool projected) {
        Check(stage < 6, "Invalid PS 1.x sampler stage");
        textures |= 1u << stage;
        const string id = to_string(stage);
        string location = "(" + coords + ")" + (stages[stage].shape == TextureShape::Flat ? ".xy" : ".xyz");
        if (projected && stages[stage].projectionCount) {
            const auto count = stages[stage].projectionCount;
            Check(count >= 2 && count <= 4, "Invalid texture projection component");
            location += "/(" + coords + ")." + "xyzw"[count - 1];
        }
        return "image" + id + ".Sample(sampler" + id + "," + location + ")";
    }

    string Expression(const Instruction& op) {
        std::array<string, 3> s;
        for (unsigned i = 0; i < op.count; ++i) s[i] = Source(op.args[i]);
        const auto index = Index(op.destination);
        switch (op.opcode) {
            case D3DSIO_MOV: return s[0];
            case D3DSIO_ADD: return s[0] + "+" + s[1];
            case D3DSIO_SUB: return s[0] + "-" + s[1];
            case D3DSIO_MUL: return s[0] + "*" + s[1];
            case D3DSIO_MAD: return "mad(" + s[0] + "," + s[1] + "," + s[2] + ")";
            case D3DSIO_LRP: return "lerp(" + s[2] + "," + s[1] + "," + s[0] + ")";
            case D3DSIO_DP3: return "dot((" + s[0] + ").xyz,(" + s[1] + ").xyz)";
            case D3DSIO_CMP: return "lerp(" + s[2] + "," + s[1] + ",step(0," + s[0] + "))";
            case D3DSIO_TEXCOORD:
                Check(!phaseModel, "Unsupported PS 1.4 texcrd variant");
                return "float4(saturate(" + UV(index) + ".xyz),1)";
            case D3DSIO_TEX:
                return Sample(index, phaseModel ? s[0] : UV(index), !phaseModel);
            case D3DSIO_TEXREG2AR: case D3DSIO_TEXREG2GB: {
                Check(stages.at(index).shape == TextureShape::Flat, "Dependent AR/GB lookup requires a 2D sampler");
                const char* channels = op.opcode == D3DSIO_TEXREG2AR ? "wx" : "yz";
                return Sample(index, "float4((" + s[0] + ")." + channels + ",0,1)", false);
            }
            default: throw std::invalid_argument("Invalid PS 1.x expression");
        }
    }

    string Commit(Word dst, const char* result) {
        const string reg = Register(dst, true), mask = WriteMask(dst);
        string value = result;
        const unsigned shift = (dst & D3DSP_DSTSHIFT_MASK) >> D3DSP_DSTSHIFT_SHIFT;
        if (shift) {
            Check(shift <= 3 || shift >= 13, "Invalid PS 1.x destination scale");
            value = "(" + value + (shift <= 3 ? "*" : "/") + to_string(1u << (shift <= 3 ? shift : 16 - shift)) + ")";
        }
        const auto modifier = dst & D3DSP_DSTMOD_MASK;
        Check(!modifier || modifier == D3DSPDM_SATURATE, "Unsupported PS 1.x destination modifier");
        if (modifier) value = "saturate(" + value + ")";
        return reg + mask + "=" + value + mask + ";\n";
    }

    bool phaseModel;
    const Stages& stages;
    std::array<bool, 8> literals{};
    unsigned textures = 0;
};

} // namespace detail

// Produces modern HLSL with COLORn/TEXCOORDn inputs, D3D9 stage numbers at
// tn/sn, and the eight PS 1.x float4 constants at b0. Rejects unknown opcodes.
inline std::string Translate(const void* bytecode, size_t byteCount, const Stages& stages = {}) {
    using namespace detail;
    Reader reader(bytecode, byteCount);
    const auto version = reader.Read();
    Check(version == 0xffff0101 || version == 0xffff0104, "Expected PS 1.1 or PS 1.4 bytecode");
    const bool phaseModel = version == 0xffff0104;
    std::vector<Instruction> ops;
    for (;;) {
        const auto token = reader.Read();
        if (token == D3DSIO_END) break;
        const Word opcode = token & D3DSI_OPCODE_MASK;
        if (opcode == D3DSIO_COMMENT) {
            reader.SkipWords((token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT);
            continue;
        }
        Check(!(token & ~(Word(D3DSI_OPCODE_MASK) | D3DSI_COISSUE)), "Unexpected PS 1.x instruction flags");
        Instruction op{};
        op.opcode = opcode;
        op.coissued = (token & D3DSI_COISSUE) != 0;
        op.count = SourceCount(opcode, phaseModel);
        if (opcode != D3DSIO_PHASE && opcode != D3DSIO_NOP) op.destination = reader.Read();
        for (unsigned i = 0; i < op.count; ++i) op.args[i] = reader.Read();
        if (op.coissued) {
            Check(opcode != D3DSIO_DEF && opcode != D3DSIO_NOP && !ops.empty(),
                  "Illegal PS 1.x co-issue");
        }
        ops.push_back(op);
    }
    Check(reader.Empty(), "Data following PS 1.x end token");
    return Emitter(phaseModel, stages).Emit(ops);
}

} // namespace legacy_pixel

