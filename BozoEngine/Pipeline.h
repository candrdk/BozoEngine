#pragma once

#include "Common.h"

#include "Device.h"
#include "BindGroup.h"
#include "Shader.h"

// TODO: PipelineDesc should start using our own enums instead of for example VkFormat.
struct PipelineDesc {
    // TODO: fix these up as needed - this is just a rough estimate of what PipelineDesc might look like.
    struct GraphicsPipelineStateDesc {
        struct {
            span<const VkFormat> formats;
            VkFormat depthStencilFormat;
            span<const VkPipelineColorBlendAttachmentState> blendStates;
        } attachments;

        struct RasterizationState {
            VkBool32 depthClampEnable = VK_FALSE;
            VkCullModeFlags cullMode;
            VkFrontFace frontFace;

            VkBool32 depthBiasEnable = VK_FALSE;
            float    depthBiasConstantFactor;
            float    depthBiasClamp;
            float    depthBiasSlopeFactor;
        } rasterization;

        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

        struct VertexInputState {
            span<const VkVertexInputBindingDescription> bindingDesc = {};
            span<const VkVertexInputAttributeDescription> attributeDesc = {};
        } vertexInput;

        struct DepthState {
            VkBool32 depthTestEnable = VK_TRUE;
            VkBool32 depthWriteEnable = VK_TRUE;
            VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        } depthState = {};

        struct StencilState {
            VkBool32 stencilTestEnable = VK_FALSE;
            VkStencilOpState frontBackOpState = {};
        } stencilState = {};

        struct SpecializationState {
            span<const VkSpecializationMapEntry> mapEntries = {};
            size_t dataSize = 0;
            void* pData = nullptr;
        } specialization = {};
    };

    const char* debugName = nullptr;
    span<const Shader> shaders;
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