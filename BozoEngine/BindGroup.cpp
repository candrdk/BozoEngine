#include "BindGroup.h"

static VkShaderStageFlags ConvertShaderStage(ShaderStage value) {
    VkShaderStageFlags stages = 0;

    if (HasFlag(value, ShaderStage::VERTEX))     stages |= VK_SHADER_STAGE_VERTEX_BIT;
    if (HasFlag(value, ShaderStage::FRAGMENT))   stages |= VK_SHADER_STAGE_FRAGMENT_BIT;

    return stages;
}

BindGroupLayout BindGroupLayout::Create(const Device& device, std::vector<Binding> bindings) {
    std::vector<VkDescriptorSetLayoutBinding> descriptorBindings;
    for (u32 i = 0; i < bindings.size(); i++) {
        descriptorBindings.push_back({
            .binding = bindings[i].binding,
            .descriptorType = (VkDescriptorType)bindings[i].type,
            .descriptorCount = bindings[i].count,
            .stageFlags = ConvertShaderStage(bindings[i].stages)
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

void BindGroupLayout::Destroy(const Device& device) {
    vkDestroyDescriptorSetLayout(device.logicalDevice, descriptorSetLayout, nullptr);
}

BindGroup BindGroup::Create(const Device& device, const BindGroupLayout& layout, const BindGroupDesc&& desc) {
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = device.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout.descriptorSetLayout
    };

    BindGroup bindGroup = { .layout = layout };
    vkAllocateDescriptorSets(device.logicalDevice, &allocInfo, &bindGroup.descriptorSet);

    bindGroup.Update(device, std::forward<const BindGroupDesc>(desc));
    return bindGroup;
}

void BindGroup::Update(const Device& device, const BindGroupDesc&& desc) {
    VkWriteDescriptorSet descriptorUpdates[16];
    VkDescriptorBufferInfo bufferDescriptors[8];
    VkDescriptorImageInfo imageDescriptors[8];

    for (int i = 0; i < desc.buffers.size(); i++) {
        bufferDescriptors[i] = {
            .buffer = desc.buffers[i].buffer,
            .offset = desc.buffers[i].offset,
            .range = desc.buffers[i].size
        };
        descriptorUpdates[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = desc.buffers[i].binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferDescriptors[i]
        };
    }

    for (int i = desc.buffers.size(); i < desc.textures.size(); i++) {
        imageDescriptors[i] = {
            .sampler = desc.textures[i].sampler,
            .imageView = desc.textures[i].view,
            .imageLayout = desc.textures[i].layout
        };

        descriptorUpdates[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = desc.textures[i].binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageDescriptors[i]
        };
    }

    vkUpdateDescriptorSets(device.logicalDevice, desc.buffers.size() + desc.textures.size(), descriptorUpdates, 0, nullptr);
}