#pragma once

#include "Common.h"
#include "Logging.h"
#include "Device.h"

struct Buffer {
	VkBuffer buffer;
	VkDeviceMemory memory;
	void* mapped;

	VkResult map(VkDevice device, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0) {
		return vkMapMemory(device, memory, offset, size, 0, &mapped);
	}
	void unmap(VkDevice device) {
		if (mapped) {
			vkUnmapMemory(device, memory);
			mapped = nullptr;
		}
	}
	VkResult bind(VkDevice device, VkDeviceSize offset) {
		return vkBindBufferMemory(device, buffer, memory, offset);
	}
	void destroy(VkDevice device) {
		if (buffer) {
			vkDestroyBuffer(device, buffer, nullptr);
		}
		if (memory) {
			vkFreeMemory(device, memory, nullptr);
		}
	}
};

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

	UIOverlay();
	~UIOverlay();

	void Initialize(const Device& device, VkSampleCountFlagBits rasterizationSamples, VkFormat colorFormat, VkFormat depthFormat);

	bool Update(const Device& device);
	void Draw(VkCommandBuffer cmdBuffer);
	void Resize(u32 width, u32 height);

	void Free(const Device& device);
};