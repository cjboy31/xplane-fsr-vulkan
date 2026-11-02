// SPDX-License-Identifier: MIT
// Portable checks for the native-motion SPIR-V interface classifier. Uses the
// original stand-in shaders (src/native_motion_fixture.comp), never simulator
// shaders. Usage: classify_tests FIXTURE_2D.spv FIXTURE_ARRAY.spv
#include "shader_classify.h"
#include <cstdio>
#include <fstream>
#include <vector>

static std::vector<uint32_t> load(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::vector<uint32_t> words;
    if (!file)
        return words;
    const auto bytes = size_t(file.tellg());
    if (bytes < 20 || bytes % 4)
        return words;
    words.resize(bytes / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(words.data()), std::streamsize(bytes));
    return words;
}
static int failures = 0;
static void expect(bool condition, const char *what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
// Every changed uniform member offset must be rejected (fail closed).
static unsigned rejectChangedOffsets(const std::vector<uint32_t> &code) {
    unsigned rejected = 0;
    for (size_t i = 5; i < code.size();) {
        const uint32_t count = code[i] >> 16, op = code[i] & 65535;
        if (!count)
            break;
        if (op == 72 && count >= 5 && code[i + 3] == 35 /* Offset */ && code[i + 4] >= 16) {
            auto changed = code;
            changed[i + 4] += 16;
            expect(classifyShader(changed.data(), changed.size()) == ShaderRole::None,
                   "changed uniform offset accepted");
            ++rejected;
        }
        i += count;
    }
    return rejected;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::printf("usage: classify_tests FIXTURE_2D.spv FIXTURE_ARRAY.spv\n");
        return 2;
    }
    const auto flat = load(argv[1]), array = load(argv[2]);
    expect(!flat.empty() && !array.empty(), "fixtures load");
    if (failures)
        return 1;
    expect(classifyShader(flat.data(), flat.size()) == ShaderRole::SsrScene2D, "2D SSR interface");
    expect(classifyShader(array.data(), array.size()) == ShaderRole::SsrSceneArray,
           "array SSR interface");
    expect(rejectChangedOffsets(flat) > 0 && rejectChangedOffsets(array) > 0,
           "offset mutations exercised");
    std::vector<uint32_t> truncated(flat.begin(), flat.begin() + flat.size() / 2);
    expect(classifyShader(truncated.data(), truncated.size()) == ShaderRole::None,
           "truncated module rejected");
    auto badMagic = flat;
    badMagic[0] ^= 1;
    expect(classifyShader(badMagic.data(), badMagic.size()) == ShaderRole::None,
           "bad magic rejected");
    expect(classifyShader(nullptr, 0) == ShaderRole::None, "empty module rejected");
    if (failures)
        return 1;
    std::printf("PASS: native-motion SSR interface classifier (fails closed on changes)\n");
    return 0;
}
