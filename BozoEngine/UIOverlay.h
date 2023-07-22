#pragma once

#include "Common.h"
#include "Device.h"
#include "Texture.h"

class UIOverlay {
	Device* device;

	Buffer drawDataBuffer	= {};
	Texture2D font			= {};

	void* vertexBufferStart			= nullptr;
	void* indexBufferStart			= nullptr;
	VkDeviceSize vertexBufferOffset	= {};
	VkDeviceSize indexBufferOffset	= {};

	VkDescriptorPool descriptorPool				= VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout	= VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet				= VK_NULL_HANDLE;

	VkPipelineLayout pipelineLayout				= VK_NULL_HANDLE;
	VkPipeline pipeline							= VK_NULL_HANDLE;

	struct PushConstantBlock {
		glm::vec2 scale;
		glm::vec2 translate;
	} pushConstantBlock	= {};

public:
	VkPipelineShaderStageCreateInfo vertShader	= {};
	VkPipelineShaderStageCreateInfo fragShader	= {};

	UIOverlay();
	~UIOverlay();

	// Initialize all vulkan resources needed to draw the ui overlay
	void Initialize(GLFWwindow* window, Device* device, VkFormat colorFormat, VkFormat depthFormat);

	// Update thr ui overlay draw data. Should be called every frame before Draw.
	void Update();

	// Records draw commands to render the ui overlay into the specified VkCommandBuffer.
	void Draw(VkCommandBuffer cmdBuffer);

	void Free();

private:
	// Draws the ImGui frame. 
	// TODO: maybe allow the user to set this with a lambda?
	void RenderFrame();
};