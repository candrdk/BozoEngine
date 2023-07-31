#include "Shader.h"

#include <spirv_reflect.h>

// TODO: move this to some util header
static std::vector<u8> ReadFile(const char* path) {
    FILE* fp = fopen(path, "rb");
    Check(fp != nullptr, "File: `%s` failed to open", path);

    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    Check(length > 0, "File: `%s` was empty", path);

    std::vector<u8> buffer(length);
    size_t read = fread(buffer.data(), 1, length, fp);
    Check(read == length, "Failed to read all contents of `%s`", path);
    fclose(fp);

    return buffer;
}

Shader Shader::Create(const Device& device, const char* path, const char* entry) {
    std::vector<u8> spv = ReadFile(path);

    SpvReflectShaderModule spvModule;
    SpvReflectResult res = spvReflectCreateShaderModule(spv.size(), spv.data(), &spvModule);

    Shader shader = { .stage = (VkShaderStageFlagBits)spvModule.shader_stage, .pEntry = entry };

    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spv.size(),
        .pCode = (u32*)spv.data()
    };

    vkCreateShaderModule(device.logicalDevice, &createInfo, nullptr, &shader.module);

    Check(spvModule.descriptor_set_count <= 4, "Only 4 descriptor sets can be bound at a time");
    for (u32 i = 0; i < spvModule.descriptor_binding_count; i++) {
        SpvReflectDescriptorBinding descriptorBinding = spvModule.descriptor_bindings[i];
        shader.shaderBindings.push_back({
            .slot = descriptorBinding.set,
            .desc = {
                .binding = descriptorBinding.binding,
                .type = (u32)descriptorBinding.descriptor_type,
                .stages = (VkShaderStageFlags)shader.stage
            }
            });
    }

    if (spvModule.push_constant_block_count > 0) {
        shader.pushConstants = {
            .stageFlags = (VkShaderStageFlags)shader.stage,
            .offset = spvModule.push_constant_blocks->offset,
            .size = spvModule.push_constant_blocks->size
        };
    }

    spvReflectDestroyShaderModule(&spvModule);

    return shader;
}

void Shader::Destroy(const Device& device) {
    vkDestroyShaderModule(device.logicalDevice, module, nullptr);
}

VkPipelineShaderStageCreateInfo Shader::GetShaderStageCreateInfo() const {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = module,
        .pName = pEntry
    };
}