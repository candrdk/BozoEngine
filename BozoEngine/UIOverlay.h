#pragma once

#include "Common.h"
#include "Device.h"
#include "Texture.h"
#include "Pipeline.h"

class UIOverlay {
	Device& device;
	void (*RenderImGui)(void);

	Buffer drawDataBuffer	= {};
	Texture2D font			= {};

	void* vertexBufferStart			= nullptr;
	void* indexBufferStart			= nullptr;
	VkDeviceSize vertexBufferOffset	= {};
	VkDeviceSize indexBufferOffset	= {};

	BindGroupLayout bindGroupLayout;
	BindGroup bindGroup;
	Pipeline pipeline;

	struct PushConstantBlock {
		glm::vec2 scale;
		glm::vec2 translate;
	} pushConstantBlock	= {};

public:
	// Create a uioverlay for the given window.
	UIOverlay(GLFWwindow* window, Device& device, VkFormat colorFormat, VkFormat depthFormat, void (*RenderFunction)());
	~UIOverlay();

	// Update the ui overlay draw data. Should be called every frame before Draw.
	void Update();

	// Records draw commands to render the ui overlay into the specified VkCommandBuffer.
	void Draw(VkCommandBuffer cmdBuffer);

private:
	// Initialize all vulkan resources needed to draw the ui overlay
	void InitializeVulkanResources();

	// Initialize the vulkan pipeline needed to render the ui overlay
	void InitializeVulkanPipeline(VkFormat colorFormat, VkFormat depthFormat);

	// Draws the ImGui frame. 
	void RenderFrame();				// TODO: maybe allow the user to set this with a lambda?
};