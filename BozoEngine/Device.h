#pragma once

#include "Common.h"

// TODO: move this to separate buffer.h file.
//		 Everything regarding buffers will likely be heavliy changed once we start working with larger scenes 
//		 and implement a resource manager. It's okay to elave it like this for now, though.
struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	void* mapped = nullptr;

	VkDeviceSize size = 0;

	VkResult map(VkDevice device, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0) {
		return vkMapMemory(device, memory, offset, size, 0, &mapped);
	}

	void unmap(VkDevice device) {
		if (mapped) {
			vkUnmapMemory(device, memory);
			mapped = nullptr;
		}
	}

	VkResult bind(VkDevice device, VkDeviceSize offset = 0) {
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

	VkResult Flush(VkDevice device, VkDeviceSize offset = 0) {
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

	// TODO: Maybe make the command pool owned by Device *_TRANSIENT_BIT to indicate that buffers allocated
	//		 from the Device command pool usually have very short lifetimes (e.g. a single one-time transfer)

	// Note: The created buffer will have the ONE_TIME_SUBMIT flag set.
	VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, bool begin = false) const;

	VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin = false) const;

	// Note: This will also free the commandBuffer
	void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue) const;

	// TODO: move to device.cpp, or buffer.cpp
	VkResult CreateBuffer(VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceSize size, Buffer* buffer, void* data = nullptr) const {
		VkBufferCreateInfo bufferInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE
		};

		VkCheck(vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &buffer->buffer), "Failed to create buffer");

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(logicalDevice, buffer->buffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memRequirements.size,
			.memoryTypeIndex = GetMemoryType(memRequirements.memoryTypeBits, properties)
		};

		VkCheck(vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &buffer->memory), "Failed to allocate buffer memory");
		buffer->size = memRequirements.size;

		if (data != nullptr) {
			VkAssert(buffer->map(logicalDevice));
			memcpy(buffer->mapped, data, size);
			if ((properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
				buffer->Flush(logicalDevice);
			}
			buffer->unmap(logicalDevice);
		}

		buffer->bind(logicalDevice);

		return VK_SUCCESS;
	}

	u32 GetMemoryType(u32 memoryTypeBits, VkMemoryPropertyFlags properties) const;
	VkSampleCountFlagBits GetMaxUsableSampleCount() const;
	u32 GetQueueFamilyIndex(VkQueueFlags queueFlags) const;
};