#pragma once

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

	u32 GetMemoryType(u32 memoryTypeBits, VkMemoryPropertyFlags properties) const;

	VkSampleCountFlagBits GetMaxUsableSampleCount() const;
	u32 GetQueueFamilyIndex(VkQueueFlags queueFlags) const;
};