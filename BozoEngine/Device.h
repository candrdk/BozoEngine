#pragma once

struct Buffer {
	VkBuffer buffer;
	VkDeviceMemory memory;
	void* mapped;

	VkDeviceSize size;
	VkDeviceSize offset;

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

	VkResult Flush(VkDevice device) {
		VkMappedMemoryRange mappedRange = {
			.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
			.memory = memory,
			.offset = offset,
			.size = size
		};

		return vkFlushMappedMemoryRanges(device, 1, &mappedRange);
	}
};

class Device {
public:
	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice logicalDevice = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;

	struct {
		u32 graphics;
		u32 compute;
		u32 transfer;
	} queueIndex;

	std::vector<VkQueueFamilyProperties> queueFamilyProperties = {};
	VkPhysicalDeviceFeatures enabledFeatures = {};
	VkPhysicalDeviceProperties properties = {};
	VkPhysicalDeviceMemoryProperties memoryProperties = {};

	void CreateDevice(GLFWwindow* window);
	void DestroyDevice();

	// TODO: Should maybe allocate a separate command pool for these kinds of short-lived buffers.
	//		 When we do, use the VK_COMMAND_POOL_CREATE_TRANSIENT_BIT flag during command pool generation.
	// TODO: Maybe add bool param to conditionally call vkBeginCommandBuffer?
	// TODO: If we want this method to be generic, ONE_TIME_SUBMIT should not be set.
	//		 Likewise, we should only conditionally free the command buffer in FlushCommandBuffer.
	VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool) const;

	VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level) const;

	void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue) const;

	// TODO: Add a VkLog(VkResult, string) that simply prints if condition fails and returns VkResult
	//		 Also add a macro for the	`if(!VkResult) return VkLog(VkResult, string);`	  pattern.
	VkResult CreateBuffer(VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceSize size, Buffer* buffer) const {
		VkResult result = VK_SUCCESS;
		VkBufferCreateInfo bufferInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE
		};

		result = vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &buffer->buffer); // , "Failed to create buffer");
		if (result != VK_SUCCESS) return result;

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(logicalDevice, buffer->buffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memRequirements.size,
			.memoryTypeIndex = GetMemoryType(memRequirements.memoryTypeBits, properties)
		};

		result = vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &buffer->memory); // , "Failed to allocate buffer memory");
		if (result != VK_SUCCESS) return result;
		result = vkBindBufferMemory(logicalDevice, buffer->buffer, buffer->memory, 0); // , "Failed to bind DeviceMemory to VkBuffer");
		if (result != VK_SUCCESS) return result;

		return result;
	}

	u32 GetMemoryType(u32 memoryTypeBits, VkMemoryPropertyFlags properties) const;
	VkSampleCountFlagBits GetMaxUsableSampleCount() const;
	u32 GetQueueFamilyIndex(VkQueueFlags queueFlags) const;
};