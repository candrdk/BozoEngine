#pragma once

#include <imgui.h>

#include "Common.h"
#include "Logging.h"
#include "Device.h"

class UIOverlay {
public:
	
	Buffer vertexBuffer;
	int vertexCount;

	Buffer indexBuffer;
	int indexCount;

	VkImage fontImage;
	VkImageView fontView;
	VkDeviceMemory fontMemory;
	VkSampler sampler;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;

	struct PushConstantBlock {
		glm::vec2 scale;
		glm::vec2 translate;
	} pushConstantBlock;

	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;

	union {
		struct {
			VkPipelineShaderStageCreateInfo vertShader;
			VkPipelineShaderStageCreateInfo fragShader;
		};
		VkPipelineShaderStageCreateInfo shaders[2];
	};

	UIOverlay();
	~UIOverlay();

	void Initialize(const Device& device, VkSampleCountFlagBits rasterizationSamples, VkFormat colorFormat, VkFormat depthFormat);

	void Update(const Device& device);
	void Draw(VkCommandBuffer cmdBuffer);
	void Resize(u32 width, u32 height);

	void Free(const Device& device);
};