#pragma once

#include "Common.h"
#include "Device.h"
#include "Buffer.h"
#include "Texture.h"

// TODO: Figure out if user should be responsible for freeing bindgroups / how to handle their reuse
// TODO: should BindGroup keep a reference/pointer to the BindGroupLayout instead?
//          - want to make ownership/responsibility for destroying the layout clearer.
// TODO: move these enums to enum classes

struct Binding {
    enum Type {
        TEXTURE = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        BUFFER = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    };

    enum Stage {
        VERTEX = VK_SHADER_STAGE_VERTEX_BIT,
        FRAGMENT = VK_SHADER_STAGE_FRAGMENT_BIT
    };

    u32 binding;
    u32 type;
    u32 stages = VERTEX | FRAGMENT;
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