#pragma once

#include "Common.h"
#include "Device.h"
#include "Texture.h"
#include "Pipeline.h"
#include "Swapchain.h"

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
	struct FrameTimeHistory {
		float entries[1024] = {};
		u32 front = 0;
		u32 back = 0;
		u32 count = 0;
		bool freeze = false;

		float Get(u32 i) {
			i = (back + count - i - 1) % arraysize(entries);
			return entries[i];
		}

		void Post(float dt) {
			if (freeze) return;

			entries[front] = dt;
			front = (front + 1) % arraysize(entries);
			if (count == arraysize(entries)) {
				back = front;
			}
			else {
				count++;
			}
		}
	} frameTimeHistory;

	// Create a uioverlay for the given window.
	UIOverlay(GLFWwindow* window, Device& device, VkFormat colorFormat, VkFormat depthFormat, void (*RenderFunction)());
	~UIOverlay();

	// Update the ui overlay draw data. Should be called every frame before Draw.
	void Update();

	// Records draw commands to render the ui overlay into the specified VkCommandBuffer.
	void Draw(VkCommandBuffer cmdBuffer, VkExtent2D extent, const VkRenderingAttachmentInfo& colorAttachment);

	// Render the time graph widget - must be called inside a imgui window context.
	void ShowFrameTimeGraph();

private:
	// Initialize all vulkan resources needed to draw the ui overlay
	void InitializeVulkanResources();

	// Initialize the vulkan pipeline needed to render the ui overlay
	void InitializeVulkanPipeline(VkFormat colorFormat, VkFormat depthFormat);

	// Draws the ImGui frame. 
	void RenderFrame();				// TODO: maybe allow the user to set this with a lambda?
};