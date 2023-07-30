#pragma once

#include "Common.h"
#include "Device.h"

// TODO: Sample api.
// * Maybe make BINDGROUP strongly typed, and take it as a param for createBindGroup?
// * Find a way to create buffer/texture bindings by just giving a Buffer/Texture2D directly.
// TODO: figure out if user should be responsible for freeing bindgroups / how to handle their reuse
// TODO: should BindGroup keep a reference/pointer to the BindGroupLayout instead?
//          - want to make ownership/responsibility for destroying the layout clearer.
#if 0
enum BINDGROUP {
    GLOBALS = 0,
    MATERIAL = 1
};
#endif

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

    static BindGroupLayout Create(const Device& device, std::vector<Binding> bindings) {
        std::vector<VkDescriptorSetLayoutBinding> descriptorBindings;
        for (u32 i = 0; i < bindings.size(); i++) {
            descriptorBindings.push_back({
                .binding = bindings[i].binding,
                .descriptorType = (VkDescriptorType)bindings[i].type,
                .descriptorCount = bindings[i].count,
                .stageFlags = bindings[i].stages
            });
        }

        VkDescriptorSetLayoutCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = (u32)descriptorBindings.size(),
            .pBindings = descriptorBindings.data()
        };

        VkDescriptorSetLayout descriptorSetLayout;
        vkCreateDescriptorSetLayout(device.logicalDevice, &createInfo, nullptr, &descriptorSetLayout);

        return BindGroupLayout{
            .bindingDescs = bindings,
            .descriptorSetLayout = descriptorSetLayout
        };
    }

    void Destroy(const Device& device) {
        vkDestroyDescriptorSetLayout(device.logicalDevice, descriptorSetLayout, nullptr);
    }
};

struct BindGroupDesc {
    struct TextureBinding {
        u32 binding;

        VkSampler sampler;
        VkImageView view;
        VkImageLayout layout;
    };

    struct BufferBinding {
        u32 binding;

        VkBuffer buffer;
        VkDeviceSize offset;
        VkDeviceSize size;
    };

    std::vector<BufferBinding> buffers;
    std::vector<TextureBinding> textures;
};

struct BindGroup {
    BindGroupLayout layout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    static BindGroup Create(const Device& device, const BindGroupLayout& layout, const BindGroupDesc&& desc) {
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = device.descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &layout.descriptorSetLayout
        };

        BindGroup bindGroup = { .layout = layout };
        vkAllocateDescriptorSets(device.logicalDevice, &allocInfo, &bindGroup.descriptorSet);

        bindGroup.Update(device, std::forward<const BindGroupDesc&&>(desc));
        return bindGroup;
    }

    void Update(const Device& device, const BindGroupDesc&& desc) {
        for (const auto& bufferBinding : desc.buffers) {
            VkDescriptorBufferInfo bufferInfo = {
                .buffer = bufferBinding.buffer,
                .offset = bufferBinding.offset,
                .range = bufferBinding.size
            };
            VkWriteDescriptorSet write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSet,
                .dstBinding = bufferBinding.binding,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &bufferInfo
            };
            vkUpdateDescriptorSets(device.logicalDevice, 1, &write, 0, nullptr);
        }

        for (const auto& texture : desc.textures) {
            VkDescriptorImageInfo imageInfo = {
                .sampler = texture.sampler,
                .imageView = texture.view,
                .imageLayout = texture.layout
            };
            VkWriteDescriptorSet write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSet,
                .dstBinding = texture.binding,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfo
            };
            vkUpdateDescriptorSets(device.logicalDevice, 1, &write, 0, nullptr);
        }
    }
};