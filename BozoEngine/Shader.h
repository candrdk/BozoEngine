#pragma once

#include "Common.h"
#include "Device.h"
#include "BindGroup.h"

struct ShaderBinding {
    u32 slot;
    Binding desc;
};

struct Shader {
    VkShaderModule module;
    VkShaderStageFlagBits stage;
    const char* pEntry;

    std::vector<ShaderBinding> shaderBindings;
    VkPushConstantRange pushConstants;
    // TODO: parse these. SPIRV-Reflect currently doesn't support them?
    // std::vector<VkSpecializationMapEntry> specializationEntries;

    static Shader Create(const Device& device, const char* path, const char* entry = "main");

    void Destroy(const Device& device);

    VkPipelineShaderStageCreateInfo GetShaderStageCreateInfo() const;
};