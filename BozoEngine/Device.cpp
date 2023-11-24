#include "Device.h"

// SGR escape sequences: https://docs.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
#define SGR_SET_BG_GRAY  "\x1B[100;1m"
#define SGR_SET_BG_BLUE	 "\x1B[44;1m"
#define SGR_SET_BG_RED	 "\x1B[41;1m"
#define SGR_SET_TXT_BLUE "\x1B[34;1m"
#define SGR_SET_DEFAULT  "\x1B[0m"

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   printf(SGR_SET_BG_RED  "[ERROR]" SGR_SET_DEFAULT "   ");
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) printf(SGR_SET_BG_BLUE "[WARNING]" SGR_SET_DEFAULT " ");
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) printf("[VERBOSE] ");

	const char* vkSpecLinkBegin = strstr(pCallbackData->pMessage, "https");
	if (vkSpecLinkBegin) {
		const char* vkSpecLinkEnd = strchr(vkSpecLinkBegin, ')');
		printf("%.*s", (int)(vkSpecLinkBegin - pCallbackData->pMessage), pCallbackData->pMessage);
		printf(SGR_SET_TXT_BLUE "%.*s" SGR_SET_DEFAULT ")\n", (int)(vkSpecLinkEnd - vkSpecLinkBegin), vkSpecLinkBegin);
	}
	else {
		printf("%s\n", pCallbackData->pMessage);
	}

	return VK_FALSE;
}

static VkDebugUtilsMessengerCreateInfoEXT GetDebugMessengerCreateInfo() {
	return {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
						 | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
						 | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
					 | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
					 | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = DebugCallback
	};
}

static VkInstance CreateInstance() {
	VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Bozo Engine Application",
		.applicationVersion = VK_API_VERSION_1_3,
		.pEngineName = "Bozo Engine",
		.engineVersion = VK_API_VERSION_1_3,
		.apiVersion = VK_API_VERSION_1_3
	};

	const char* validationLayers[] = {
		"VK_LAYER_KHRONOS_validation"
	};
	const char* extensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
	};

	VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = GetDebugMessengerCreateInfo();

	VkInstanceCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifdef _DEBUG
		.pNext = &debugMessengerCreateInfo,
#endif
		.pApplicationInfo = &appInfo,
#ifdef _DEBUG
		.enabledLayerCount = arraysize(validationLayers),
		.ppEnabledLayerNames = validationLayers,
#endif
		.enabledExtensionCount = arraysize(extensions),
		.ppEnabledExtensionNames = extensions,
	};

	VkInstance instance;
	VkCheck(vkCreateInstance(&createInfo, nullptr, &instance), "Failed to create instance");
	
	return instance;
}

static VkDebugUtilsMessengerEXT CreateDebugMessenger(VkInstance instance) {
	VkDebugUtilsMessengerCreateInfoEXT createInfo = GetDebugMessengerCreateInfo();
	VkDebugUtilsMessengerEXT debugMessenger;
	VkCheck(vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger), "Failed to create debug messenger");
	return debugMessenger;
}

static VkSurfaceKHR CreateSurface(GLFWwindow* window, VkInstance instance) {
	VkSurfaceKHR surface;
	VkCheck(glfwCreateWindowSurface(instance, window, nullptr, &surface), "Failed to create window surface");
	return surface;
}

static VkPhysicalDevice CreatePhysicalDevice(VkInstance instance) {
	u32 deviceCount = 0;
	VkAssert(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));

	std::vector<VkPhysicalDevice> devices(deviceCount);
	VkAssert(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

	Check(deviceCount > 0, "Failed to find GPUs with Vulkan support");

	for (u32 i = 0; i < deviceCount; i++) {
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(devices[i], &properties);
		VkPhysicalDeviceFeatures features;
		vkGetPhysicalDeviceFeatures(devices[i], &features);

		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && features.samplerAnisotropy) {
			printf(SGR_SET_BG_GRAY "[INFO]" SGR_SET_DEFAULT "    Found suitable GPU: `%s`.\n", properties.deviceName);
			return devices[i];
		}
	}

	Check(false, "Failed to find a suitable GPU");
}

u32 Device::GetQueueFamilyIndex(VkQueueFlags queueFlags) const {
	// Find dedicated queue for compute
	if ((queueFlags & VK_QUEUE_COMPUTE_BIT) == queueFlags) {
		for (u32 i = 0; i < queueFamilyProperties.size(); i++) {
			VkQueueFlags flags = queueFamilyProperties[i].queueFlags;
			if ((flags & VK_QUEUE_COMPUTE_BIT) && ((flags & VK_QUEUE_GRAPHICS_BIT) == 0)) {
				return i;
			}
		}
	}

	// Find dedicated queue for transfer
	if ((queueFlags & VK_QUEUE_TRANSFER_BIT) == queueFlags) {
		for (u32 i = 0; i < queueFamilyProperties.size(); i++) {
			VkQueueFlags flags = queueFamilyProperties[i].queueFlags;
			if ((flags & VK_QUEUE_TRANSFER_BIT) && ((flags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((flags & VK_QUEUE_COMPUTE_BIT) == 0)) {
				return i;
			}
		}
	}

	// For other queue types or if no dedicated queue is found, return the first one to support the requested flag
	for (u32 i = 0; i < queueFamilyProperties.size(); i++) {
		VkQueueFlags flags = queueFamilyProperties[i].queueFlags;
		if ((flags & queueFlags) == queueFlags) {
			return i;
		}
	}

	Check(false, "Could not find a queue family with flags: %u", queueFlags);
}

static VkQueue CreateQueue(VkDevice device, u32 queueFamilyIndex) {
	VkQueue queue;
	vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
	return queue;
}

static VkDevice CreateLogicalDevice(VkPhysicalDevice physicalDevice, u32 queueFamilyIndex) {
	float queuePriority[] = { 1.0f };
	VkDeviceQueueCreateInfo queueCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = queueFamilyIndex,
		.queueCount = 1,
		.pQueuePriorities = queuePriority
	};

	const char* extensions[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	// TODO: fill these out later as needed
	VkPhysicalDeviceVulkan13Features features13 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE
	};
	VkPhysicalDeviceVulkan12Features features12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &features13
	};
	VkPhysicalDeviceVulkan11Features features11 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
		.pNext = &features12
	};
	VkPhysicalDeviceFeatures2 features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features11,
		.features = {
			.depthClamp = VK_TRUE,
			.depthBiasClamp = VK_TRUE,
			.samplerAnisotropy = VK_TRUE
		}
	};

	VkDeviceCreateInfo deviceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueCreateInfo,
		.enabledExtensionCount = arraysize(extensions),
		.ppEnabledExtensionNames = extensions
	};

	VkDevice device;
	VkCheck(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device), "Failed to create logical device");

	return device;
}

static VkCommandPool CreateCommandPool(VkDevice device, u32 queueFamilyIndex) {
	VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = queueFamilyIndex
	};

	VkCommandPool commandPool;
	VkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "Failed to create command pool");

	return commandPool;
}

static VkDescriptorPool CreateDescriptorPool(VkDevice device, u32 maxBufferDescriptors, u32 maxDynamicBufferDescriptors, u32 maxImageDescriptors) {
	VkDescriptorPoolSize poolSizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,			.descriptorCount = maxBufferDescriptors },
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,	.descriptorCount = maxDynamicBufferDescriptors },
		{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	.descriptorCount = maxImageDescriptors }
	};

	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = maxBufferDescriptors + maxImageDescriptors,
		.poolSizeCount = arraysize(poolSizes),
		.pPoolSizes = poolSizes
	};

	VkDescriptorPool descriptorPool;
	VkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "Failed to create descriptor pool");

	return descriptorPool;
}

void Device::CreateDevice(GLFWwindow* window) {
	VkCheck(volkInitialize(), "Failed to initialize volk");

	instance = CreateInstance();
	volkLoadInstance(instance);

	debugMessenger = CreateDebugMessenger(instance);

	surface = CreateSurface(window, instance);
	physicalDevice = CreatePhysicalDevice(instance);

	enabledFeatures = {
		.depthClamp			= VK_TRUE,
		.depthBiasClamp		= VK_TRUE,
		.samplerAnisotropy	= VK_TRUE
	};

	vkGetPhysicalDeviceProperties(physicalDevice, &properties);
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	u32 queueCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, nullptr);
	queueFamilyProperties.resize(queueCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueFamilyProperties.data());

	queueIndex = {
		.graphics = GetQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT),
		.compute  = GetQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT),
		.transfer = GetQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT)
	};

	logicalDevice = CreateLogicalDevice(physicalDevice, queueIndex.graphics);
	volkLoadDevice(logicalDevice);
	graphicsQueue = CreateQueue(logicalDevice, queueIndex.graphics);

	commandPool = CreateCommandPool(logicalDevice, queueIndex.graphics);
	descriptorPool = CreateDescriptorPool(logicalDevice, 100, 1, 100);
}

void Device::DestroyDevice() {
	vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
	vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
	vkDestroyDevice(logicalDevice, nullptr);

#if _DEBUG
	vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
#endif

	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);
}

VkCommandBuffer Device::CreateCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, bool begin) const {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = level,
		.commandBufferCount = 1
	};

	VkCommandBuffer commandBuffer;
	VkCheck(vkAllocateCommandBuffers(logicalDevice, &allocInfo, &commandBuffer), "Failed to allocate command buffer");

	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	if (begin) {
		VkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer");
	}

	return commandBuffer;
}

VkCommandBuffer Device::CreateCommandBuffer(VkCommandBufferLevel level, bool begin) const {
	return CreateCommandBuffer(level, commandPool, begin);
}

void Device::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue) const {
	Check(commandBuffer != VK_NULL_HANDLE, "Buffer was null");

	VkCheck(vkEndCommandBuffer(commandBuffer), "Failed to end command buffer");

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = commandBuffer
	};

	VkSubmitInfo2 submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &commandBufferSubmitInfo
	};

	// Create fence to ensure the command buffer has finished executing
	VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence;
	VkCheck(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence), "Failed to create fence");

	VkCheck(vkQueueSubmit2(queue, 1, &submitInfo, fence), "Failed to submit command buffer to queue");

	// Wait for the fence to signal that the command buffer has finished executing
	VkCheck(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, 1ull << 32), "Wait for fence failed");	// NOTE: maybe define a default timeout macro. For now, 1 << 32 ~ 5 seconds.

	vkDestroyFence(logicalDevice, fence, nullptr);
	vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
}

u32 Device::GetMemoryType(u32 memoryTypeBits, VkMemoryPropertyFlags properties) const {
	for (u32 i = 0; i < memoryProperties.memoryTypeCount; i++) {
		bool hasProperties = (properties & memoryProperties.memoryTypes[i].propertyFlags) == properties;
		bool matchesMemoryType = memoryTypeBits & (1 << i);
		if (matchesMemoryType && hasProperties) {
			return i;
		}
	}

	Check(false, "Failed to find a memory type with:\n\tproperties: %x.\n\tMemory bits: %x\n", properties, memoryTypeBits);
}

VkSampleCountFlagBits Device::GetMaxUsableSampleCount() const {
	VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts
		& properties.limits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}