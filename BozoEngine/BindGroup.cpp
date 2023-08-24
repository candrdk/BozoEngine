#include "BindGroup.h"

BindGroupLayout BindGroupLayout::Create(const Device& device, std::vector<Binding> bindings) {
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

// TODO: should probablt batch these descriptor set updates instead of calling
// vkUpdateDescriptorSets once for every binding...
void BindGroup::Update(const Device& device, const BindGroupDesc&& desc) {
    for (const Buffer::Binding& buffer : desc.buffers) {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = buffer.buffer,
            .offset = buffer.offset,
            .range = buffer.size
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = buffer.binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfo
        };
        vkUpdateDescriptorSets(device.logicalDevice, 1, &write, 0, nullptr);
    }

    for (const Texture::Binding& texture : desc.textures) {
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