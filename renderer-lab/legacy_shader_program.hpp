#pragma once
#include "legacy_pixel_shader.hpp"
#include <mojoshader.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>

namespace legacy_program {

inline bool IsPixel1(std::uint32_t profile) { return profile == 0xffff0101 || profile == 0xffff0104; }

inline std::string Translate(const void* shader, size_t size, const legacy_pixel::Stages& stages = {}) {
    if (!shader || size < 8 || size > 1024 * 1024 || size % 4) throw std::invalid_argument("Invalid shader bytecode size");
    std::uint32_t profile;
    std::memcpy(&profile, shader, sizeof(profile));
    if (IsPixel1(profile)) return legacy_pixel::Translate(shader, size, stages);
    auto parsed = MOJOSHADER_parse(MOJOSHADER_PROFILE_HLSL, legacy_pixel::Entry, static_cast<const unsigned char*>(shader),
                                   static_cast<unsigned>(size), nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr);
    std::unique_ptr<const MOJOSHADER_parseData, decltype(&MOJOSHADER_freeParseData)> hlsl(parsed, &MOJOSHADER_freeParseData);
    if (!hlsl || hlsl->error_count || !hlsl->output || !hlsl->mainfn) {
        std::string messages;
        if (hlsl) for (int i = 0; i < hlsl->error_count; ++i) {
            const auto& error = hlsl->errors[i];
            if (i) messages += " | ";
            messages += error.error ? error.error : "Unknown MojoShader error";
            if (error.error_position >= 0) messages += " @ " + std::to_string(error.error_position);
        }
        throw std::runtime_error(messages.empty() ? "No HLSL emitted by MojoShader" : messages);
    }
    return {hlsl->output, static_cast<size_t>(hlsl->output_len)};
}

namespace detail {

struct Field { std::string type, name, semantic; };
struct Layout {
    size_t first = std::string::npos, last = std::string::npos;
    std::vector<Field> fields;
};

inline Layout ParseLayout(const std::string& source, const char* name) {
    Layout result;
    auto position = source.find(std::string("struct ") + name);
    if (position == std::string::npos) return result;
    result.first = source.find('{', position);
    if (result.first == std::string::npos) throw std::runtime_error("Missing stage interface body");
    ++result.first;
    result.last = source.find('}', result.first);
    if (result.last == std::string::npos) throw std::runtime_error("Unterminated stage interface");
    for (size_t start = result.first; start < result.last;) {
        const auto end = source.find(';', start);
        if (end == std::string::npos || end >= result.last) break;
        auto line = source.substr(start, end - start);
        for (auto comment = line.find("//"); comment != std::string::npos; comment = line.find("//")) {
            const auto newline = line.find('\n', comment);
            line.erase(comment, newline == std::string::npos ? std::string::npos : newline - comment);
        }
        std::istringstream declaration(line);
        Field field;
        char colon = 0;
        if (!(declaration >> field.type >> field.name >> colon >> field.semantic) || colon != ':')
            throw std::runtime_error("Unrecognized generated stage field");
        result.fields.push_back(std::move(field));
        start = end + 1;
    }
    return result;
}

inline bool ReadsMember(const std::string& program, size_t after, const std::string& name) {
    const std::string access = "input." + name;
    for (auto pos = program.find(access, after); pos != std::string::npos; pos = program.find(access, pos + access.size())) {
        const auto end = pos + access.size();
        if (end == program.size() || !(std::isalnum(static_cast<unsigned char>(program[end])) || program[end] == '_')) return true;
    }
    return false;
}

} // namespace detail

// Reproduce the producer's physical register order before compiling this pair.
// TEXCOORDn and COLORn alone do not cause the D3D11 driver to reorder registers.
inline std::string LinkPixelInputs(const std::string& vertex, std::string pixel) {
    const auto outputs = detail::ParseLayout(vertex, "UnlimitedShader_Output");
    auto inputs = detail::ParseLayout(pixel, "UnlimitedShader_Input");
    if (inputs.first == std::string::npos) inputs = detail::ParseLayout(pixel, "Interpolants");
    if (inputs.first == std::string::npos) return pixel; // Constant-only pixel program.
    if (outputs.fields.empty()) throw std::runtime_error("Missing vertex output signature");
    std::string body = "\n";
    std::vector<bool> connected(inputs.fields.size());
    for (const auto& out : outputs.fields) {
        const auto it = std::find_if(inputs.fields.begin(), inputs.fields.end(), [&](const auto& in) {
            return in.semantic == out.semantic;
        });
        std::string name = "unused_" + out.semantic;
        if (it != inputs.fields.end()) {
            if (it->type != out.type) throw std::runtime_error("Mismatched interface type for " + out.semantic);
            name = it->name;
            connected[static_cast<size_t>(it - inputs.fields.begin())] = true;
        }
        body += out.type + " " + name + " : " + out.semantic + ";\n";
    }
    for (size_t i = 0; i < inputs.fields.size(); ++i) {
        if (!connected[i] && detail::ReadsMember(pixel, inputs.last, inputs.fields[i].name))
            throw std::runtime_error("Vertex shader does not provide " + inputs.fields[i].semantic);
    }
    pixel.replace(inputs.first, inputs.last - inputs.first, body);
    return pixel;
}

} // namespace legacy_program

