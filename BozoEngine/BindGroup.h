#pragma once

#include "Common.h"
#include "Device.h"
#include "Buffer.h"
#include "Texture.h"

// TODO: Figure out how we want to handle lifetimes of bindgrouplayouts + bindgroups.
// Will probabily wait untill a proper resource manager has been implemented...
//
// Should probably also stop leaking VK enums in Binding.

struct Binding {
    enum Type {
        TEXTURE = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        BUFFER = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    };

    u32 binding;
    u32 type;
    ShaderStage stages = ShaderStage::VERTEX | ShaderStage::FRAGMENT;
    u32 count = 1;
};

struct BindGroupLayout {
    std::vector<Binding> bindingDescs;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    static BindGroupLayout Create(const Device& device, std::vector<Binding> bindings);

    void Destroy(const Device& device);
};

struct BindGroupDesc {
    span<const Buffer::Binding>  buffers;
    span<const Texture::Binding> textures;
};

struct BindGroup {
    BindGroupLayout layout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    static BindGroup Create(const Device& device, const BindGroupLayout& layout, const BindGroupDesc&& desc);

    void Update(const Device& device, const BindGroupDesc&& desc);
};