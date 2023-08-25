#pragma once

#include "Common.h"
#include "Device.h"
#include "BindGroup.h"

struct Shader {
    VkShaderModule module;
    VkShaderStageFlags stage;
    const char* pEntry;

    std::vector<Binding> shaderBindings[4];
    VkPushConstantRange pushConstants;

    // TODO: parse these. SPIRV-Reflect currently doesn't support them?
    // std::vector<VkSpecializationMapEntry> specializationEntries;

    static Shader Create(const Device& device, const char* path, const char* entry = "main");

    void Destroy(const Device& device);

    VkPipelineShaderStageCreateInfo GetShaderStageCreateInfo() const;
};