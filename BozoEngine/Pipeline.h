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

            //TODO: temporary hack
            std::vector<VkPipelineColorBlendAttachmentState> blendStates = std::vector<VkPipelineColorBlendAttachmentState>(formats.size(), { .blendEnable = VK_FALSE, .colorWriteMask = 0xF });
        } attachments;

        struct RasterizationState {
            VkCullModeFlags cullMode;
            VkFrontFace frontFace;
        } rasterization;

        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

        struct VertexInputState {
            std::vector<VkVertexInputBindingDescription> bindingDesc = {};
            std::vector<VkVertexInputAttributeDescription> attributeDesc = {};
        } vertexInput;

        struct DepthState {
            bool depthTestEnable = true;
            bool depthWriteEnable = true;
            VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        } depthStencil = {};

        struct SpecializationState {
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

    static Pipeline Create(const Device& device, VkPipelineBindPoint bindPoint, const PipelineDesc&& desc);

    void Destroy(const Device& device);

    BindGroup CreateBindGroup(const Device& device, u32 bindGroupSlot, const BindGroupDesc&& desc);
};