// SPDX-License-Identifier: MIT
// Adapted from JonathanOrr/xplane-fsr3 fa51577c9bc2c3477e64dfdd358796aebb9ca45b.
// Copyright (c) 2026 JonathanOrr; licenses/xplane-fsr3-MIT.txt.
#include "shader_classify.h"

#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

enum : uint32_t {
    kMagic = 0x07230203,
    OpEntryPoint = 15,
    OpName = 5,
    OpMemberName = 6,
    OpDecorate = 71,
    OpMemberDecorate = 72,
    OpTypeImage = 25,
    OpTypeSampledImage = 27,
    OpTypePointer = 32,
    OpVariable = 59,
    OpImageGather = 96,
    ExecutionModelGLCompute = 5,
    DecRowMajor = 4,
    DecColMajor = 5,
    DecMatrixStride = 7,
    DecBinding = 33,
    DecDescriptorSet = 34,
    DecOffset = 35,
    Dim2D = 1,
};

struct ImageType {
    uint32_t dim = 0, arrayed = 0, ms = 0, scalar = 0;
};
struct Member {
    std::string name;
    int64_t offset = -1;
    bool colMajor = false, rowMajor = false;
    uint32_t matrixStride = 0;
};
struct Resource {
    uint32_t binding = 0;
    int64_t set = -1;
    const ImageType *image = nullptr;
    uint32_t block = 0;
};

uint64_t pairKey(uint32_t a, uint32_t b) { return (uint64_t(a) << 32) | b; }

// A literal string operand: nul-terminated, padded to a word boundary.
std::string literal(const uint32_t *w, size_t words) {
    const char *s = reinterpret_cast<const char *>(w);
    return std::string(s, strnlen(s, words * 4));
}
bool literalIs(const uint32_t *w, size_t words, const char *expected) {
    const size_t n = std::strlen(expected);
    return words * 4 > n && std::memcmp(w, expected, n + 1) == 0;
}

struct Module {
    std::unordered_map<uint32_t, std::string> names;
    std::unordered_map<uint64_t, Member> members; // (struct, index)
    std::unordered_map<uint32_t, uint32_t> bindings, sets, sampled, pointers, variables;
    std::unordered_map<uint32_t, ImageType> images;
    std::unordered_map<std::string, Resource> resources;
    uint32_t gathers = 0;
    // Validate types/array strides as well as member offsets before reading a UBO.
    std::unordered_map<uint32_t, std::vector<uint32_t>> types;
    std::unordered_map<uint32_t, uint32_t> constants, strides;
    bool scalar(uint32_t id, uint32_t opcode) const {
        auto it = types.find(id);
        return it != types.end() && it->second.size() >= 2 && it->second[0] == opcode &&
               it->second[1] == 32 &&
               (opcode != 21 || (it->second.size() == 3 && it->second[2] == 0));
    }
    bool vector(uint32_t id, uint32_t size) const {
        auto it = types.find(id);
        return it != types.end() && it->second.size() == 3 && it->second[0] == 23 &&
               scalar(it->second[1], 22) && it->second[2] == size;
    }
    bool array(uint32_t id, bool matrix) const {
        auto it = types.find(id);
        if (it == types.end() || it->second.size() != 3 || it->second[0] != 28)
            return false;
        auto len = constants.find(it->second[2]), stride = strides.find(id);
        if (len == constants.end() || len->second != 2 || stride == strides.end() ||
            stride->second != (matrix ? 64u : 16u))
            return false;
        id = it->second[1];
        if (!matrix)
            return vector(id, 4);
        auto m = types.find(id);
        return m != types.end() && m->second.size() == 3 && m->second[0] == 24 &&
               m->second[2] == 4 && vector(m->second[1], 4);
    }
    bool blockTypes(uint32_t block, bool ssr) const {
        auto it = types.find(block);
        if (it == types.end() || it->second.empty() || it->second[0] != 30)
            return false;
        auto &t = it->second;
        if (ssr)
            return t.size() >= 4 && array(t[1], true) && array(t[2], true) && array(t[3], true);
        return t.size() == 7 && vector(t[1], 4) && vector(t[2], 4) && array(t[3], false) &&
               array(t[4], false) && vector(t[5], 2) && array(t[6], true);
    }

    const Resource *resource(const char *name) const {
        auto it = resources.find(name);
        return it == resources.end() ? nullptr : &it->second;
    }
    const Member *member(uint32_t block, const char *name) const {
        for (const auto &[key, m] : members)
            if (uint32_t(key >> 32) == block && m.name == name)
                return &m;
        return nullptr;
    }
};

// Cheap first pass: a GLCompute entry point and the OpName of one of the shaders' resources.
bool candidate(const uint32_t *w, size_t count) {
    bool compute = false, named = false;
    for (size_t i = 5; i < count;) {
        const uint32_t n = w[i] >> 16, op = w[i] & 0xFFFF;
        if (n == 0 || i + n > count)
            return false;
        if (op == OpEntryPoint && n > 1)
            compute |= w[i + 1] == ExecutionModelGLCompute;
        if (op == OpName && n > 2)
            named |= literalIs(w + i + 2, n - 2, "u_fsr_data") ||
                     literalIs(w + i + 2, n - 2, "gbuffer_pos");
        i += n;
    }
    return compute && named;
}

bool parse(const uint32_t *w, size_t count, Module &m) {
    for (size_t i = 5; i < count;) {
        const uint32_t n = w[i] >> 16, op = w[i] & 0xFFFF;
        if (n == 0 || i + n > count)
            return false;
        const uint32_t *o = w + i + 1; // operands
        const size_t k = n - 1;        // operand count
        switch (op) {
        case 21:
        case 22:
        case 23:
        case 24:
        case 28:
        case 30:
            if (k >= 1) {
                auto &t = m.types[o[0]];
                t.push_back(op);
                t.insert(t.end(), o + 1, o + k);
            }
            break;
        case 43:
            if (k == 3)
                m.constants[o[1]] = o[2];
            break;
        case OpName:
            if (k >= 2)
                m.names[o[0]] = literal(o + 1, k - 1);
            break;
        case OpMemberName:
            if (k >= 3)
                m.members[pairKey(o[0], o[1])].name = literal(o + 2, k - 2);
            break;
        case OpDecorate:
            if (k >= 3 && o[1] == 6)
                m.strides[o[0]] = o[2];
            if (k >= 3 && o[1] == DecBinding)
                m.bindings[o[0]] = o[2];
            if (k >= 3 && o[1] == DecDescriptorSet)
                m.sets[o[0]] = o[2];
            break;
        case OpMemberDecorate:
            if (k >= 3) {
                Member &mem = m.members[pairKey(o[0], o[1])];
                if (o[2] == DecOffset && k >= 4)
                    mem.offset = o[3];
                if (o[2] == DecColMajor)
                    mem.colMajor = true;
                if (o[2] == DecRowMajor)
                    mem.rowMajor = true;
                if (o[2] == DecMatrixStride && k >= 4)
                    mem.matrixStride = o[3];
            }
            break;
        case OpTypeImage:
            if (k >= 6)
                m.images[o[0]] = ImageType{o[2], o[4], o[5], o[1]};
            break;
        case OpTypeSampledImage:
            if (k >= 2)
                m.sampled[o[0]] = o[1];
            break;
        case OpTypePointer:
            if (k >= 3)
                m.pointers[o[0]] = o[2];
            break;
        case OpVariable:
            if (k >= 2)
                m.variables[o[1]] = o[0];
            break;
        case OpImageGather:
            ++m.gathers;
            break;
        default:
            break;
        }
        i += n;
    }
    // Resources by name. Uniform blocks are named on their struct type; the variable itself is
    // often
    // "_" or "" (classify_shaders.py does the same).
    for (const auto &[var, ptrType] : m.variables) {
        auto b = m.bindings.find(var);
        if (b == m.bindings.end())
            continue;
        auto p = m.pointers.find(ptrType);
        const uint32_t pointee = p == m.pointers.end() ? 0 : p->second;
        std::string name;
        if (auto it = m.names.find(var); it != m.names.end())
            name = it->second;
        if (name.empty())
            if (auto it = m.names.find(pointee); it != m.names.end())
                name = it->second;
        if (name.empty())
            continue;
        Resource r;
        r.binding = b->second;
        if (auto s = m.sets.find(var); s != m.sets.end())
            r.set = s->second;
        uint32_t imageId = pointee;
        if (auto s = m.sampled.find(pointee); s != m.sampled.end())
            imageId = s->second;
        if (auto it = m.images.find(imageId); it != m.images.end())
            r.image = &it->second;
        r.block = pointee;
        m.resources[name] = r;
    }
    return true;
}

// The layout the layer reads: copy_in.comp (u_fsr_data) and inputs.comp (GBufferData, SsrData).
struct Expect {
    const char *name;
    uint32_t offset;
    bool matrix;
};
const char *checkBlock(const Module &m, const Resource &r, const Expect *expect, size_t n,
                       const char *failure) {
    for (size_t i = 0; i < n; ++i) {
        const Member *mem = m.member(r.block, expect[i].name);
        if (!mem || mem->offset != int64_t(expect[i].offset))
            return failure;
        if (expect[i].matrix && (mem->rowMajor || !mem->colMajor || mem->matrixStride != 16))
            return failure;
    }
    return nullptr;
}

const Expect kFsrData[] = {{"Const0", 0, false},
                           {"Const1", 16, false},
                           {"Const2", 32, false},
                           {"Const3", 48, false},
                           {"Sample", 64, false}};
const Expect kGBufferData[] = {{"u_viewport_size", 0, false},      {"u_gbuffer_size", 16, false},
                               {"u_proj_coefficients", 32, false}, {"u_clip_info", 64, false},
                               {"u_sample_offset", 96, false},     {"u_head_to_eye", 112, true}};
const Expect kSsrData[] = {
    {"u_proj", 0, true}, {"u_reproj", 128, true}, {"u_local_reproj", 256, true}};

bool at(const Resource *r, uint32_t binding) { return r && r->binding == binding && r->set == 0; }
bool arrayed2D(const Resource *r) {
    return r->image && r->image->dim == Dim2D && r->image->arrayed && !r->image->ms;
}

ShaderRole role(const Module &m, const char **reason) {
    const Resource *in = m.resource("u_input_texture");
    const Resource *out = m.resource("i_output_texture");
    const Resource *fsr = m.resource("u_fsr_data");
    if (in && out && fsr && m.gathers > 0) {
        const char *why = nullptr;
        if (!at(in, 1) || !at(out, 2) || !at(fsr, 3))
            why = "easu_bindings";
        else if (!arrayed2D(in) || !arrayed2D(out))
            why = "easu_images";
        else
            why = checkBlock(m, *fsr, kFsrData, std::size(kFsrData), "easu_u_fsr_data_layout");
        if (reason)
            *reason = why;
        return why ? ShaderRole::None : ShaderRole::Easu;
    }
    const Resource *pos = m.resource("gbuffer_pos");
    const Resource *vel = m.resource("gbuffer_vel");
    const Resource *gb = m.resource("u_gbuffer_data");
    const Resource *ssr = m.resource("u_ssr_data");
    if (pos && vel && gb && ssr) {
        if (!pos->image || pos->image->dim != Dim2D)
            return ShaderRole::None;
        // The MSAA variant: recognised by its bindings only, since none of its data is read.
        if (pos->image->ms)
            return at(pos, 3) && at(vel, 4) && at(gb, 11) && at(ssr, 12) ? ShaderRole::SsrSceneMsaa
                                                                         : ShaderRole::None;
        const char *why = nullptr;
        if (!at(pos, 3) || !at(vel, 4) || !at(gb, 11) || !at(ssr, 12))
            why = "ssr_bindings";
        else if (!vel->image || vel->image->dim != Dim2D || vel->image->ms ||
                 vel->image->arrayed != pos->image->arrayed || !m.scalar(pos->image->scalar, 22) ||
                 !m.scalar(vel->image->scalar, 21))
            why = "ssr_images";
        else if (!m.blockTypes(gb->block, false) || !m.blockTypes(ssr->block, true))
            why = "ssr_uniform_types";
        else if (!(why = checkBlock(m, *gb, kGBufferData, std::size(kGBufferData),
                                    "ssr_u_gbuffer_data_layout")))
            why = checkBlock(m, *ssr, kSsrData, std::size(kSsrData), "ssr_u_ssr_data_layout");
        if (reason)
            *reason = why;
        if (why)
            return ShaderRole::None;
        return pos->image->arrayed ? ShaderRole::SsrSceneArray : ShaderRole::SsrScene2D;
    }
    return ShaderRole::None;
}

} // namespace

const char *roleName(ShaderRole role) {
    switch (role) {
    case ShaderRole::Easu:
        return "Easu";
    case ShaderRole::SsrScene2D:
        return "SsrScene2D";
    case ShaderRole::SsrSceneArray:
        return "SsrSceneArray";
    case ShaderRole::SsrSceneMsaa:
        return "SsrSceneMsaa";
    default:
        return "None";
    }
}

ShaderRole classifyShader(const uint32_t *words, size_t count, const char **reason) {
    if (reason)
        *reason = nullptr;
    if (!words || count < 5 || words[0] != kMagic || !candidate(words, count))
        return ShaderRole::None;
    Module m;
    if (!parse(words, count, m)) {
        if (reason)
            *reason = "malformed";
        return ShaderRole::None;
    }
    return role(m, reason);
}
