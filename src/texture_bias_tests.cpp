// SPDX-License-Identifier: MIT
// Portable texture-bias checks. Build: c++ -std=c++17 -I<vulkan headers> src/texture_bias_tests.cpp
#include "texture_bias.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void check(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
int main() {
    using namespace xpfsr::lod;
    VkSamplerCreateInfo material{}; material.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    material.magFilter = material.minFilter = VK_FILTER_LINEAR; material.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    material.anisotropyEnable = VK_TRUE; material.maxAnisotropy = 16; material.maxLod = VK_LOD_CLAMP_NONE;
    check(eligible(material), "Anisotropic material sampler is eligible");
    auto shadow = material; shadow.compareEnable = VK_TRUE; check(!eligible(shadow), "Depth-compare sampler is never changed");
    auto target = material; target.anisotropyEnable = VK_FALSE; check(!eligible(target), "Render-target sampler (no anisotropy) is never changed");
    auto lut = material; lut.minFilter = VK_FILTER_NEAREST; check(!eligible(lut), "Nearest sampler is never changed");
    auto flat = material; flat.maxLod = 0; check(!eligible(flat), "Sampler without mips is never changed");
    auto pixels = material; pixels.unnormalizedCoordinates = VK_TRUE; check(!eligible(pixels), "Unnormalized sampler is never changed");
    check(std::fabs(biased(-0.58f, -1.f, 16.f) + 1.58f) < 1e-5f, "AMD formula: FSR 1-style -0.58 plus -1 gives -1.58 (Quality 1.5x)");
    check(biased(0.f, -0.5f, 16.f) == -0.5f && biased(-15.8f, -1.f, 16.f) == -16.f, "Bias adds and clamps to the device limit");
    check(biased(0.3f, 0.7f, 16.f) == 0.3f, "A positive extra never blurs");
    check(parse(L"1") == -0.5f && parse(L"2") == -1.f && parse(L"") == 0.f && parse(L"9") == 0.f && parse(nullptr) == 0.f, "Session values parse safely");
    std::puts("PASS: texture bias eligibility, AMD formula offset, clamping and parsing");
}
