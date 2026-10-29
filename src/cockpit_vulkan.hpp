// SPDX-License-Identifier: MIT
// Independent Vulkan current-frame cockpit compositor; no SDK/MSFS code.
#pragma once
#include "cockpit_restore_spv.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>
namespace cockpit {
struct alignas(16) Params {
    float clip[8][4]{};
    float outputInfo[4]{}; // dispatch origin, quad count, debug tint
    float jitter[4]{};     // applied jitter / input size, Y sign, edge erosion
    float limits[4]{.02f, 1e-5f, 0, 0};
};
static_assert(sizeof(Params) == 176, "GLSL uniform layout");
inline void vkEnsure(VkResult r, const char *s) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(s) + ": " + std::to_string(r));
}
class Compositor;
class Frame {
    friend class Compositor;
    std::shared_ptr<Compositor> owner_;
    VkBuffer uniform_{};
    VkDeviceMemory memory_{};
    void *mapped_{};
    VkDescriptorPool pool_{};
    VkDescriptorSet set_{};
    VkFormat format_{};

  public:
    explicit Frame(std::shared_ptr<Compositor> owner, VkImageView current, VkImageView depth,
                   VkImageView output, VkFormat format);
    void release() noexcept;
    ~Frame();
    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;
};
class Compositor : public std::enable_shared_from_this<Compositor> {
    friend class Frame;
    VkDevice device_;
    PFN_vkGetDeviceProcAddr gdpa_;
    VkPhysicalDeviceMemoryProperties memory_;
    VkDescriptorSetLayout setLayout_{};
    VkPipelineLayout layout_{};
    std::array<VkPipeline, 2> pipelines_{};
    VkSampler linear_{}, nearest_{};
    PFN_vkCmdPipelineBarrier barrier_;
    PFN_vkCmdBindPipeline bind_;
    PFN_vkCmdBindDescriptorSets sets_;
    PFN_vkCmdDispatch dispatch_;
    template <class T> T fn(const char *name) {
        auto p = gdpa_(device_, name);
        if (!p)
            throw std::runtime_error(name);
        return reinterpret_cast<T>(p);
    }
    uint32_t hostType(uint32_t bits) {
        for (uint32_t n = 0; n < memory_.memoryTypeCount; ++n)
            if ((bits & (1u << n)) &&
                (memory_.memoryTypes[n].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return n;
        throw std::runtime_error("Cockpit uniform memory unavailable");
    }

  public:
    Compositor(VkDevice device, PFN_vkGetDeviceProcAddr gdpa,
               const VkPhysicalDeviceMemoryProperties &memory)
        : device_(device), gdpa_(gdpa), memory_(memory) {
        try {
            barrier_ = fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
            bind_ = fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
            sets_ = fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets");
            dispatch_ = fn<PFN_vkCmdDispatch>("vkCmdDispatch");
            VkDescriptorSetLayoutBinding b[4]{};
            for (unsigned i = 0; i < 4; ++i) {
                b[i].binding = i;
                b[i].descriptorCount = 1;
                b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                b[i].descriptorType = i == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                      : i == 3 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                               : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
            VkDescriptorSetLayoutCreateInfo s{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            s.bindingCount = 4;
            s.pBindings = b;
            vkEnsure(fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(
                         device_, &s, nullptr, &setLayout_),
                     "Cockpit set layout");
            VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            l.setLayoutCount = 1;
            l.pSetLayouts = &setLayout_;
            vkEnsure(fn<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device_, &l, nullptr,
                                                                              &layout_),
                     "Cockpit pipeline layout");
            const uint32_t *code[] = {cockpitRestore16Spv, cockpitRestore32Spv};
            const size_t bytes[] = {sizeof(cockpitRestore16Spv), sizeof(cockpitRestore32Spv)};
            for (unsigned i = 0; i < 2; ++i) {
                VkShaderModule module{};
                VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                c.codeSize = bytes[i];
                c.pCode = code[i];
                vkEnsure(fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device_, &c, nullptr,
                                                                              &module),
                         "Cockpit shader");
                VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                p.layout = layout_;
                p.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                p.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                p.stage.module = module;
                p.stage.pName = "main";
                auto result = fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(
                    device_, VK_NULL_HANDLE, 1, &p, nullptr, &pipelines_[i]);
                fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device_, module, nullptr);
                vkEnsure(result, "Cockpit compute pipeline");
            }
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            vkEnsure(
                fn<PFN_vkCreateSampler>("vkCreateSampler")(device_, &sampler, nullptr, &linear_),
                "Cockpit color sampler");
            sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
            vkEnsure(
                fn<PFN_vkCreateSampler>("vkCreateSampler")(device_, &sampler, nullptr, &nearest_),
                "Cockpit depth sampler");
        } catch (...) {
            release();
            throw;
        }
    }
    void release() noexcept {
        try {
            for (auto &p : pipelines_)
                if (p) {
                    fn<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device_, p, nullptr);
                    p = VK_NULL_HANDLE;
                }
            if (linear_) {
                fn<PFN_vkDestroySampler>("vkDestroySampler")(device_, linear_, nullptr);
                linear_ = VK_NULL_HANDLE;
            }
            if (nearest_) {
                fn<PFN_vkDestroySampler>("vkDestroySampler")(device_, nearest_, nullptr);
                nearest_ = VK_NULL_HANDLE;
            }
            if (layout_) {
                fn<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device_, layout_,
                                                                           nullptr);
                layout_ = VK_NULL_HANDLE;
            }
            if (setLayout_) {
                fn<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(
                    device_, setLayout_, nullptr);
                setLayout_ = VK_NULL_HANDLE;
            }
        } catch (...) {
        }
    }
    ~Compositor() { release(); }
    bool record(Frame &frame, VkCommandBuffer cb, Params p, unsigned width, unsigned height) {
        if (!cb || !width || !height || p.outputInfo[2] < 1 || p.outputInfo[2] > 2)
            return false;
        float left = float(width), top = float(height), right = 0, bottom = 0;
        for (unsigned q = 0; q < unsigned(p.outputInfo[2]); ++q) {
            bool valid = true;
            float l = float(width), t = float(height), r = 0, b = 0;
            for (unsigned i = 0; i < 4; ++i) {
                auto *v = p.clip[q * 4 + i];
                if (v[3] <= .0001f || !std::isfinite(v[3])) {
                    valid = false;
                    break;
                }
                float x = (v[0] / v[3] * .5f + .5f) * width,
                      y = (v[1] / v[3] * p.jitter[2] * .5f + .5f) * height, z = v[2] / v[3];
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z < 0 || z > 1) {
                    valid = false;
                    break;
                }
                l = std::min(l, x);
                t = std::min(t, y);
                r = std::max(r, x);
                b = std::max(b, y);
            }
            if (valid) {
                left = std::min(left, l);
                top = std::min(top, t);
                right = std::max(right, r);
                bottom = std::max(bottom, b);
            }
        }
        unsigned x = unsigned(std::max(0.f, std::floor(left))),
                 y = unsigned(std::max(0.f, std::floor(top)));
        unsigned endX = unsigned(std::min(float(width), std::max(0.f, std::ceil(right)))),
                 endY = unsigned(std::min(float(height), std::max(0.f, std::ceil(bottom))));
        if (x >= endX || y >= endY)
            return false;
        p.outputInfo[0] = float(x);
        p.outputInfo[1] = float(y);
        memcpy(frame.mapped_, &p, sizeof(p));
        VkMemoryBarrier dependency{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        dependency.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier_(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                 &dependency, 0, nullptr, 0, nullptr);
        bind_(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
              pipelines_[frame.format_ == VK_FORMAT_R32G32B32A32_SFLOAT ? 1 : 0]);
        sets_(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &frame.set_, 0, nullptr);
        dispatch_(cb, (endX - x + 7) / 8, (endY - y + 7) / 8, 1);
        return true;
    }
};
inline Frame::Frame(std::shared_ptr<Compositor> owner, VkImageView current, VkImageView depth,
                    VkImageView output, VkFormat format)
    : owner_(std::move(owner)), format_(format) {
    if (format != VK_FORMAT_R16G16B16A16_SFLOAT && format != VK_FORMAT_R32G32B32A32_SFLOAT)
        throw std::runtime_error("Unsupported cockpit output format");
    auto &c = *owner_;
    try {
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        b.size = sizeof(Params);
        b.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        vkEnsure(c.fn<PFN_vkCreateBuffer>("vkCreateBuffer")(c.device_, &b, nullptr, &uniform_),
                 "Cockpit constants");
        VkMemoryRequirements r{};
        c.fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(c.device_,
                                                                                 uniform_, &r);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        a.allocationSize = r.size;
        a.memoryTypeIndex = c.hostType(r.memoryTypeBits);
        vkEnsure(c.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(c.device_, &a, nullptr, &memory_),
                 "Cockpit constants memory");
        vkEnsure(
            c.fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(c.device_, uniform_, memory_, 0),
            "Cockpit constants bind");
        vkEnsure(c.fn<PFN_vkMapMemory>("vkMapMemory")(c.device_, memory_, 0, sizeof(Params), 0,
                                                      &mapped_),
                 "Cockpit constants map");
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 3;
        pool.pPoolSizes = sizes;
        vkEnsure(c.fn<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(c.device_, &pool,
                                                                            nullptr, &pool_),
                 "Cockpit descriptors");
        VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set.descriptorPool = pool_;
        set.descriptorSetCount = 1;
        set.pSetLayouts = &c.setLayout_;
        vkEnsure(
            c.fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(c.device_, &set, &set_),
            "Cockpit descriptor set");
        VkDescriptorBufferInfo buffer{uniform_, 0, sizeof(Params)};
        VkDescriptorImageInfo images[] = {
            {c.linear_, current, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {c.nearest_, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, output, VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet writes[4]{};
        for (unsigned i = 0; i < 4; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set_;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = i == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                       : i == 3 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (i)
                writes[i].pImageInfo = &images[i - 1];
            else
                writes[i].pBufferInfo = &buffer;
        }
        c.fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(c.device_, 4, writes, 0,
                                                                   nullptr);
    } catch (...) {
        release();
        throw;
    }
}
inline void Frame::release() noexcept {
    if (!owner_)
        return;
    try {
        auto &c = *owner_;
        if (pool_) {
            c.fn<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(c.device_, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
        if (mapped_) {
            c.fn<PFN_vkUnmapMemory>("vkUnmapMemory")(c.device_, memory_);
            mapped_ = nullptr;
        }
        if (uniform_) {
            c.fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(c.device_, uniform_, nullptr);
            uniform_ = VK_NULL_HANDLE;
        }
        if (memory_) {
            c.fn<PFN_vkFreeMemory>("vkFreeMemory")(c.device_, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    } catch (...) {
    }
}
inline Frame::~Frame() { release(); }
} // namespace cockpit
