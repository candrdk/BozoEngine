#pragma once

#include "Common.h"
#include "Device.h"

class UIOverlay {
public:
	Buffer drawDataBuffer;
	void* vertexBufferStart;
	void* indexBufferStart;
	VkDeviceSize vertexBufferOffset;
	VkDeviceSize indexBufferOffset;

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

	// Initialize all vulkan resources needed to draw the ui overlay
	void Initialize(const Device& device, VkSampleCountFlagBits rasterizationSamples, VkFormat colorFormat, VkFormat depthFormat);

	// Update the ui overlay draw data. Should be called after ImGui::Render();
	void Update(const Device& device);

	// Draw the ui overlay in the specified VkCommandBuffer. 
	// Caller must have begun rendering with VkBeginRendering();
	void Draw(VkCommandBuffer cmdBuffer);

	void Free(const Device& device);
};