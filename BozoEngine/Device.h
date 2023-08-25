#pragma once

#include "Common.h"

// TODO: should these enums really be here?

enum class Memory {
	DEFAULT				= 1 << 0,	// GPU only			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	UPLOAD				= 1 << 1,	// CPU --> GPU		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	READBACK			= 1 << 2	// CPU <-> GPU		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
};

enum class ShaderStage {
	NONE				= 0,
	VERTEX				= 1 << 0,
	FRAGMENT			= 1 << 1
};

enum class Format {
	UNDEFINED			= 1 << 0,
	RGBA8_UNORM			= 1 << 1,
	RGBA8_SRGB			= 1 << 2,
	D24_UNORM_S8_UINT	= 1 << 3
};

enum class Usage {
	NONE				= 0,
	SHADER_RESOURCE		= 1 << 0,
	TRANSFER_SRC		= 1 << 1,
	TRANSFER_DST		= 1 << 2,

	RENDER_TARGET		= 1 << 3,
	DEPTH_STENCIL		= 1 << 4,

	VERTEX_BUFFER		= 1 << 5,
	INDEX_BUFFER		= 1 << 6,
	UNIFORM_BUFFER		= 1 << 7
};

class Device {
public:
	VkInstance					instance		= VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT	debugMessenger	= VK_NULL_HANDLE;
	VkSurfaceKHR				surface			= VK_NULL_HANDLE;
	VkPhysicalDevice			physicalDevice	= VK_NULL_HANDLE;
	VkDevice					logicalDevice	= VK_NULL_HANDLE;
	VkQueue						graphicsQueue	= VK_NULL_HANDLE;
	VkCommandPool				commandPool		= VK_NULL_HANDLE;
	VkDescriptorPool			descriptorPool	= VK_NULL_HANDLE;

	struct {
		u32 graphics;
		u32 compute;
		u32 transfer;
	} queueIndex;

	std::vector<VkQueueFamilyProperties>	queueFamilyProperties	= {};
	VkPhysicalDeviceFeatures				enabledFeatures			= {};
	VkPhysicalDeviceProperties				properties				= {};
	VkPhysicalDeviceMemoryProperties		memoryProperties		= {};

	void CreateDevice(GLFWwindow* window);
	void DestroyDevice();

	// TODO: Maybe make the command pool owned by Device *_TRANSIENT_BIT to indicate that buffers allocated
	//		 from the Device command pool usually have very short lifetimes (e.g. a single one-time transfer)

	// Note: The created buffer will have the ONE_TIME_SUBMIT flag set.
	VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, bool begin = false) const;

	VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin = false) const;

	// Note: This will also free the commandBuffer
	void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue) const;

	u32 GetMemoryType(u32 memoryTypeBits, VkMemoryPropertyFlags properties) const;
	VkSampleCountFlagBits GetMaxUsableSampleCount() const;
	u32 GetQueueFamilyIndex(VkQueueFlags queueFlags) const;
};