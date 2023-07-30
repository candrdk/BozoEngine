#pragma once

#include "Common.h"
#include "Device.h"
#include "BindGroup.h"
#include "Shader.h"

struct PipelineDesc {
    // TODO: fix these up as needed - this is just a rough estimate of what PipelineDesc might look like.
    struct GraphicsPipelineStateDesc {
        struct {
            std::vector<VkFormat> formats;
            VkFormat depthStencilFormat;

            VkBool32 blendEnable = VK_FALSE;
            std::vector<VkPipelineColorBlendAttachmentState> blendStates = std::vector<VkPipelineColorBlendAttachmentState>(formats.size(), { .blendEnable = blendEnable, .colorWriteMask = 0xF });
        } attachments;

        struct {
            VkCullModeFlags cullMode;
            VkFrontFace frontFace;
        } rasterization;

        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

        struct {
            std::vector<VkVertexInputBindingDescription> bindingDesc = {};
            std::vector<VkVertexInputAttributeDescription> attributeDesc = {};
        } vertexInput = {};

        struct {
            bool depthTestEnable = true;
            bool depthWriteEnable = true;
            VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        } depthStencil = {};

        struct {
            std::vector<VkSpecializationMapEntry> mapEntries = {};
            size_t dataSize = 0;
            void* pData = nullptr;
        } specialization = {};
    };

    const char* debugName = nullptr;
    std::vector<Shader> shaders;
    std::vector<BindGroupLayout> bindGroups;

    GraphicsPipelineStateDesc graphicsState;
};

struct Pipeline {
    VkPipelineBindPoint type;
    std::vector<BindGroupLayout> bindGroupLayouts;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkPushConstantRange pushConstants;

    static Pipeline Create(const Device& device, VkPipelineBindPoint bindPoint, const PipelineDesc&& desc) {
        std::vector<BindGroupLayout> bindGroupLayouts = desc.bindGroups;

        // TODO: If bindgroup layouts are passed in the desc, we don't need to generate layouts ourselves,
        //       and it is the responsibility of the caller to destroy the bindgroup layouts given in the desc.
        //       However, if they aren't passed in the desc, we have to generate them *and* destroy them on
        //       pipeline destruction. This feels like a bad way to do this, so for now we just don't generate
        //       bindgroup layouts from reflection data - the caller has to pass valid bindgroup layouts in the desc.
        //       
        //       Possible solutions:
        //       * Forego descriptor set layout creation based on shader reflection entirely
        //       * Move the descriptor set layout creation code to the BindGroupLayout class
        //          - BindGroupLayout::Create(std::vector<Shader> shaders)
        //
        //       Idk what the best solution is here, so just going to force the user to specify bindgroup layouts for now.
        if (bindGroupLayouts.size() == 0) {
            Check(bindGroupLayouts.size() > 0, "Generating bindgroup layouts from shader reflection data is disabled until a cleaner interface is figured out. User code should manually create bindgroup layouts that match the shaders and pass them in PipelineDesc.");

            std::vector<ShaderBinding> mergedShaderBindings = MergeShaderBindings(device, desc.shaders);
            bindGroupLayouts = CreatePipelineBindGroupLayouts(device, mergedShaderBindings);
        }

        VkPushConstantRange pushConstants = MergePushConstants(desc.shaders);

        VkPipelineLayout pipelineLayout = CreatePipelineLayout(device, bindGroupLayouts, &pushConstants);
        VkPipeline pipeline = CreatePipeline(device, pipelineLayout, desc.shaders, desc.graphicsState);

        if (desc.debugName) {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = VK_OBJECT_TYPE_PIPELINE,
                .objectHandle = (u64)pipeline,
                .pObjectName = desc.debugName
            };
            VkCheck(vkSetDebugUtilsObjectNameEXT(device.logicalDevice, &nameInfo), "Failed to set debug name for pipeline");
        }

        return {
            .type = bindPoint,
            .bindGroupLayouts = bindGroupLayouts,
            .pipelineLayout = pipelineLayout,
            .pipeline = pipeline,
            .pushConstants = pushConstants
        };
    }

    void Destroy(const Device& device) {
#if 0   // TODO: see comment in Create
        for (BindGroupLayout& layout : bindGroupLayouts) {
            layout.Destroy(device);
        }
#endif

        vkDestroyPipelineLayout(device.logicalDevice, pipelineLayout, nullptr);
        vkDestroyPipeline(device.logicalDevice, pipeline, nullptr);
    }

    BindGroup CreateBindGroup(const Device& device, u32 bindGroupSlot, const BindGroupDesc&& desc) {
        Check(bindGroupSlot < bindGroupLayouts.size(), "Pipeline does not support bindgroups for slot %i", bindGroupSlot);
        return BindGroup::Create(device, bindGroupLayouts[bindGroupSlot], std::forward<const BindGroupDesc&&>(desc));
    }

private:
    static bool TryUpdateShaderBindingStage(std::vector<ShaderBinding> existingBindings, ShaderBinding newBinding) {
        for (ShaderBinding& binding : existingBindings) {
            if (binding.desc.binding == newBinding.desc.binding && binding.slot == newBinding.slot) {
                Check(binding.desc.type == newBinding.desc.type, "Overlapping descriptors when creating pipeline");
                binding.desc.stages |= newBinding.desc.stages;
                return true;
            }
        }
        return false;
    }

    static std::vector<ShaderBinding> MergeShaderBindings(const Device& device, const std::vector<Shader> shaders) {
        std::vector<ShaderBinding> mergedShaderBindings{};

        for (const Shader& shader : shaders) {
            for (const ShaderBinding& shaderBinding : shader.shaderBindings) {
                if (TryUpdateShaderBindingStage(mergedShaderBindings, shaderBinding)) {
                    continue;
                }

                mergedShaderBindings.push_back(shaderBinding);
            }
        }

        return mergedShaderBindings;
    }

    static VkPushConstantRange MergePushConstants(const std::vector<Shader> shaders) {
        VkPushConstantRange mergedPushConstants{};

        for (const Shader& shader : shaders) {
            if (shader.pushConstants.stageFlags) {
                mergedPushConstants = shader.pushConstants;
            }

            mergedPushConstants.stageFlags |= shader.pushConstants.stageFlags;
        }

        return mergedPushConstants;
    }

    static std::vector<BindGroupLayout> CreatePipelineBindGroupLayouts(const Device& device, std::vector<ShaderBinding> shaderBindings) {
        std::vector<Binding> bindings[4];
        u32 maxBindGroupSlot = 0;
        for (u32 i = 0; i < arraysize(bindings); i++) {
            for (const ShaderBinding& shaderBinding : shaderBindings) {
                if (shaderBinding.slot == i) {
                    bindings[i].push_back(shaderBinding.desc);
                    maxBindGroupSlot = i + 1;
                }
            }
        }

        std::vector<BindGroupLayout> bindGroupLayouts(maxBindGroupSlot);
        for (u32 i = 0; i < maxBindGroupSlot; i++) {
            bindGroupLayouts[i] = BindGroupLayout::Create(device, bindings[i]);
        }

        return bindGroupLayouts;
    }

    static VkPipelineLayout CreatePipelineLayout(const Device& device, std::vector<BindGroupLayout> bindGroupLayouts, VkPushConstantRange* pushConstants) {
        std::vector<VkDescriptorSetLayout> setLayouts;
        for (const BindGroupLayout& layout : bindGroupLayouts) {
            setLayouts.push_back(layout.descriptorSetLayout);
        }
        
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = (u32)setLayouts.size(),
            .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = pushConstants->stageFlags != 0,
            .pPushConstantRanges = pushConstants->stageFlags != 0 ? pushConstants : nullptr
        };

        VkPipelineLayout pipelineLayout;
        VkCheck(vkCreatePipelineLayout(device.logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout), "Failed to create pipeline layout");

        return pipelineLayout;
    }
    
    static VkPipeline CreatePipeline(const Device& device, VkPipelineLayout pipelineLayout, std::vector<Shader> shaders, PipelineDesc::GraphicsPipelineStateDesc desc) {
        VkDynamicState dynamicState[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicStateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = arraysize(dynamicState), .pDynamicStates = dynamicState };
        VkPipelineViewportStateCreateInfo viewportStateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };

        // TODO: MapEntries should not b passed through desc, but reflected from the shader spir-v. This is not supported by SPIRV-Reflect yet...
        // TODO: Once we start generating these entries, we should add some asserts to make sure mapentries match the specialization data from desc.
        // std::vector<VkSpecializationMapEntry> specializationEntries = MergeSpecializationEntries(desc.shaders);
        VkSpecializationInfo specializationInfo = {
            .mapEntryCount = (u32)desc.specialization.mapEntries.size(),
            .pMapEntries = desc.specialization.mapEntries.data(),
            .dataSize = desc.specialization.dataSize,
            .pData = desc.specialization.pData
        };

        std::vector<VkPipelineShaderStageCreateInfo> shaderStageCreateInfos;
        for (const Shader& shader : shaders) {
            shaderStageCreateInfos.push_back({
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = shader.stage,
                .module = shader.module,
                .pName = shader.pEntry,
                .pSpecializationInfo = &specializationInfo
            });
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = (u32)desc.vertexInput.bindingDesc.size(),
            .pVertexBindingDescriptions = desc.vertexInput.bindingDesc.data(),
            .vertexAttributeDescriptionCount = (u32)desc.vertexInput.attributeDesc.size(),
            .pVertexAttributeDescriptions = desc.vertexInput.attributeDesc.data()
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
        };

        VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,		// depth clamp discards fragments outside the near/far planes. Usefull for shadow maps, requires enabling a GPU feature.
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = desc.rasterization.cullMode,
            .frontFace = desc.rasterization.frontFace,
            .depthBiasEnable = VK_FALSE,
            .lineWidth = 1.0f,
        };

        VkPipelineMultisampleStateCreateInfo multisampeInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = desc.sampleCount,
            .sampleShadingEnable = VK_FALSE
        };

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL, // inverse z
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f
        };

        VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = desc.attachments.blendEnable,
            .attachmentCount = (u32)desc.attachments.blendStates.size(),
            .pAttachments = desc.attachments.blendStates.data()
        };

        VkPipelineRenderingCreateInfo renderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = (u32)desc.attachments.formats.size(),
            .pColorAttachmentFormats = desc.attachments.formats.data(),
            .depthAttachmentFormat = desc.attachments.depthStencilFormat,
            .stencilAttachmentFormat = desc.attachments.depthStencilFormat
        };

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderingCreateInfo,
            .stageCount = (u32)shaderStageCreateInfos.size(),
            .pStages = shaderStageCreateInfos.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportStateInfo,
            .pRasterizationState = &rasterizationInfo,
            .pMultisampleState = &multisampeInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlendStateInfo,
            .pDynamicState = &dynamicStateInfo,
            .layout = pipelineLayout
        };

        VkPipeline pipeline;
        VkCheck(vkCreateGraphicsPipelines(device.logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "Failed to create graphics pipeline");
        
        return pipeline;
    }
};