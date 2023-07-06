#pragma once

struct Queue {
	VkQueue queue = VK_NULL_HANDLE;
	u32 index = UINT32_MAX;
};

struct Device {
	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	Queue graphicsQueue;
	VkCommandPool commandPool = VK_NULL_HANDLE;

	VkPhysicalDeviceFeatures enabledFeatures = {};
	VkPhysicalDeviceProperties properties = {};
	VkPhysicalDeviceMemoryProperties memoryProperties = {};

	// TODO: Should maybe allocate a separate command pool for these kinds of short-lived buffers.
	//		 When we do, use the VK_COMMAND_POOL_CREATE_TRANSIENT_BIT flag during command pool generation.
	// TODO: Maybe add bool param to conditionally call vkBeginCommandBuffer?
	// TODO: If we want this method to be generic, ONE_TIME_SUBMIT should not be set.
	//		 Likewise, we should only conditionally free the command buffer in FlushCommandBuffer.
	VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool) const {
		VkCommandBufferAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = pool,
			.level = level,
			.commandBufferCount = 1
		};

		VkCommandBuffer commandBuffer;
		VkCheck(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer), "Failed to allocate command buffer");

		VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};

		VkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer");

		return commandBuffer;
	}
	VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level) const {
		return CreateCommandBuffer(level, commandPool);
	}

	void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue) const {
		Check(commandBuffer != VK_NULL_HANDLE, "Buffer was null");

		VkCheck(vkEndCommandBuffer(commandBuffer), "Failed to end command buffer");

		VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &commandBuffer
		};

		// Create fence to ensure the command buffer has finished executing
		VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		VkFence fence;
		VkCheck(vkCreateFence(device, &fenceInfo, nullptr, &fence), "Failed to create fence");

		VkCheck(vkQueueSubmit(queue, 1, &submitInfo, fence), "Failed to submit command buffer to queue");

		// Wait for the fence to signal that the command buffer has finished executing
		VkCheck(vkWaitForFences(device, 1, &fence, VK_TRUE, 1ull << 32), "Wait for fence failed");	// TODO: define a default timeout macro. For now, 1 << 32 ~ 5 seconds.

		vkDestroyFence(device, fence, nullptr);
		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
	}

	u32 GetMemoryType(u32 memoryTypeBits, VkMemoryPropertyFlags properties) const {
		for (u32 i = 0; i < memoryProperties.memoryTypeCount; i++) {
			bool hasProperties = (properties & memoryProperties.memoryTypes[i].propertyFlags) == properties;
			bool matchesMemoryType = memoryTypeBits & (1 << i);
			if (matchesMemoryType && hasProperties) {
				return i;
			}
		}

		Check(false, "Failed to find a memory type with:\n\tproperties: %x.\n\tMemory bits: %x\n", properties, memoryTypeBits);
	}
};

Device CreateDevice(GLFWwindow* window);

void DestroyDevice(Device& device);