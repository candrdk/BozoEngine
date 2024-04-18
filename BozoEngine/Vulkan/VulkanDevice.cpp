// We are loading vulkan functions through volk
#define VMA_STATIC_FULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

// This needs to be defined in one and only one place
// Consider moving this to a separate VmaUsage.cpp file
#define VMA_IMPLEMENTATION

#include <glm/glm.hpp>
#include <volk/volk.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

#include "VulkanDevice.h"
#include "VulkanResourceManager.h"
#include "VulkanHelpers.h"

#include "../Core/Graphics.h"

Device* Device::ptr = nullptr;

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
	if      (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   printf(SGR_SET_BG_RED  "[ERROR]" SGR_SET_DEFAULT "   ");
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
		.pApplicationName = "Bozo Application",
		.applicationVersion = VK_MAKE_VERSION(0, 1, 0),
		.pEngineName = "Bozo Engine",
		.engineVersion = VK_MAKE_VERSION(0, 1, 0),
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

static VkPhysicalDevice GetPhysicalDevice(VkInstance instance) {
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

static u32 GetQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags queueFlags) {
    // Get queue family properties
    u32 queueCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueFamilyProperties.data());

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

static VkDevice CreateLogicalDevice(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures features, span<const u32> queueFamilyIndices) {
	std::vector<float> queuePriorities(queueFamilyIndices.size(), 1.0f);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (u32 i = 0; i < queueFamilyIndices.size(); i++) {
        queueCreateInfos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamilyIndices[i],
            .queueCount = 1,
            .pQueuePriorities = &queuePriorities[i]
        });
    }

	const char* extensions[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	// NOTE: fill these out later as needed
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
	VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features11,
		.features = features
	};

	VkDeviceCreateInfo deviceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &physicalDeviceFeatures,
		.queueCreateInfoCount = (u32)queueCreateInfos.size(),
		.pQueueCreateInfos = queueCreateInfos.data(),
		.enabledExtensionCount = arraysize(extensions),
		.ppEnabledExtensionNames = extensions
	};

	VkDevice device;
	VkCheck(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device), "Failed to create logical device");

	return device;
}

static VmaAllocator CreateVmaAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
    // Since we fetch vulkan pointers using volk, we have to pass everything to vma here
    VmaVulkanFunctions vulkanFunctions = {
		.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
		.vkGetDeviceProcAddr = vkGetDeviceProcAddr,
		.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
		.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
		.vkAllocateMemory = vkAllocateMemory,
		.vkFreeMemory = vkFreeMemory,
		.vkMapMemory = vkMapMemory,
		.vkUnmapMemory = vkUnmapMemory,
		.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
		.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
		.vkBindBufferMemory = vkBindBufferMemory,
		.vkBindImageMemory = vkBindImageMemory,
		.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
		.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
		.vkCreateBuffer = vkCreateBuffer,
		.vkDestroyBuffer = vkDestroyBuffer,
		.vkCreateImage = vkCreateImage,
		.vkDestroyImage = vkDestroyImage,
		.vkCmdCopyBuffer = vkCmdCopyBuffer,
		.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
		.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
		.vkBindBufferMemory2KHR = vkBindBufferMemory2,
		.vkBindImageMemory2KHR = vkBindImageMemory2,
		.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2,
		.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
		.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements
    };

    VmaAllocatorCreateInfo createInfo = {
        .physicalDevice = physicalDevice,
        .device = device,
        .pVulkanFunctions = &vulkanFunctions,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_3
    };

    VmaAllocator allocator;
    VkCheck(vmaCreateAllocator(&createInfo, &allocator), "Failed to create vma allocator");

    return allocator;
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

VulkanDevice::VulkanDevice(Window* window) {
	m_window = window;

	// Create vulkan instance and initialize volk
    VkCheck(volkInitialize(), "Failed to initialize volk");
    m_instance = CreateInstance();
    volkLoadInstance(m_instance);

#ifdef _DEBUG
	// Create the debug messenger for the vulkan instance
    VkDebugUtilsMessengerCreateInfoEXT createInfo = GetDebugMessengerCreateInfo();
	VkCheck(vkCreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger), "Failed to create debug messenger");
#endif

	// Create a VkSurface for the glfw window
	VkCheck(glfwCreateWindowSurface(m_instance, window->m_window, nullptr, &m_surface), "Failed to create window surface");

	// Get a physical device supporting vulkan
    m_gpu = GetPhysicalDevice(m_instance);

	// Specify the vulkan features we want to enable
    features = {
        .depthClamp         = VK_TRUE,
        .depthBiasClamp     = VK_TRUE,
        .samplerAnisotropy  = VK_TRUE
    };

	// Get the physical device properties. Easy access to device limits, such as minUniformBufferOffsetAlignment.
    vkGetPhysicalDeviceProperties(m_gpu, &properties);
    
	// Get queue indices
    m_graphics.index = GetQueueFamilyIndex(m_gpu, VK_QUEUE_GRAPHICS_BIT);
    m_compute.index  = GetQueueFamilyIndex(m_gpu, VK_QUEUE_COMPUTE_BIT);
    m_transfer.index = GetQueueFamilyIndex(m_gpu, VK_QUEUE_TRANSFER_BIT);

	// Create the vulkan device
    vkDevice = CreateLogicalDevice(m_gpu, features, { m_graphics.index, m_compute.index, m_transfer.index });
    volkLoadDevice(vkDevice);

	// Get the VkQueues
    vkGetDeviceQueue(vkDevice, m_graphics.index, 0, &m_graphics.queue);
    vkGetDeviceQueue(vkDevice, m_compute.index,  0, &m_compute.queue );
    vkGetDeviceQueue(vkDevice, m_transfer.index, 0, &m_transfer.queue);

	// Create the global persistant descriptor pool
	descriptorPool = CreateDescriptorPool(vkDevice, 100, 100, 100);

	// Create the VMA allocator
    vmaAllocator   = CreateVmaAllocator(m_instance, m_gpu, vkDevice);

    // Create the render frames
    VkSemaphoreCreateInfo semaphoreInfo = { 
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO 
	};

	VkFenceCreateInfo fenceInfo = { 
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, 
        .flags = VK_FENCE_CREATE_SIGNALED_BIT 
    };

    VkCommandPoolCreateInfo poolInfo = { 
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = m_graphics.index
	};

    for (Frame& frame : m_frames) {
        VkCheck(vkCreateSemaphore  (vkDevice, &semaphoreInfo, nullptr, &frame.imageAvailable), "Failed to create RenderFrame imageAvailable semaphore");
		VkCheck(vkCreateSemaphore  (vkDevice, &semaphoreInfo, nullptr, &frame.renderFinished), "Failed to create RenderFrame renderFinished semaphore");
		VkCheck(vkCreateFence      (vkDevice, &fenceInfo,     nullptr, &frame.inFlight),       "Failed to create RenderFrame inFlight fence");
	    VkCheck(vkCreateCommandPool(vkDevice, &poolInfo,      nullptr, &frame.commandPool),    "Failed to create RenderFrame command pool");
        frame.descriptorPool = CreateDescriptorPool(vkDevice, 100, 1, 100);
    }

	// Create the swapchain
    CreateSwapchain(true);
}

VulkanDevice::~VulkanDevice() {
    DestroySwapchain();

    for (Frame& frame : m_frames) {
        vkDestroySemaphore(vkDevice, frame.imageAvailable, nullptr);
        vkDestroySemaphore(vkDevice, frame.renderFinished, nullptr);
        vkDestroyFence(vkDevice, frame.inFlight, nullptr);
        vkDestroyCommandPool(vkDevice, frame.commandPool, nullptr);
        vkDestroyDescriptorPool(vkDevice, frame.descriptorPool, nullptr);
    }

	vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
    
    vmaDestroyAllocator(vmaAllocator);
    vkDestroyDevice(vkDevice, nullptr);

#ifdef _DEBUG
    vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
#endif

    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);    
}

static VkSurfaceFormatKHR ChooseSwapchainSurfaceFormat(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice) {
	u32 surfaceFormatCount = 0;
	VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr), "Failed to get surface format count");
	Check(surfaceFormatCount > 0, "Could not find any supported surface formats");

	std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
	VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data()), "Failed to get surface formats");

	// Look for a B8G8R8A8 SRGB format w/ SRGB color space
	for (u32 i = 0; i < surfaceFormatCount; i++) {
		if (surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return surfaceFormats[i];
		}
	}

	// If no such format exists, just choose the first available format
	return surfaceFormats[0];
}

static VkPresentModeKHR ChooseSwapchainPresentMode(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, bool enableVSync) {
	// If VSync is requested, the FIFO present mode is used. 
	// This waits for the next vertical blanking period before updating the current image.
	if (enableVSync) {
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	// Otherwise, get the available present modes and check if IMMEDIATE is supported.
	u32 presentModeCount = 0;
	VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr), "Failed to get surface present mode count");

	std::vector<VkPresentModeKHR> availablePresentModes(presentModeCount);
	VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, availablePresentModes.data()), "Failed to get surface present modes");

	for (u32 i = 0; i < presentModeCount; i++) {
		if (availablePresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
			return availablePresentModes[i];
		}
	}

	// If not, we just crash...
	Check(false, "Surface does not support immediate present mode");
}

static VkExtent2D ChooseSwapchainExtent(Window* window, VkSurfaceCapabilitiesKHR surfaceCapabilities) {
	// 0xFFFFFFFF indicates that the surface size will be determined by the extent of the swapchain
	// that is targeting the surface. In this case we just set the extent to the glfw window size
	if (surfaceCapabilities.currentExtent.width == 0xFFFFFFFF) {
		int width = 0, height = 0;
		window->GetWindowSize(&width, &height);

		return VkExtent2D {
			.width  = glm::clamp((u32)width,  surfaceCapabilities.minImageExtent.width,  surfaceCapabilities.maxImageExtent.width),
			.height = glm::clamp((u32)height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height),
		};
	}
	else {
		// Otherwise, we match the swapchain extent with the surface extents
		return surfaceCapabilities.currentExtent;
	}
}

void VulkanDevice::CreateSwapchain(bool VSync) {
    Check(m_swapchain.swapchain == nullptr, "Swapchain has already been created. Destroy the old one before creating a new.");

	// Get the surface capatilities, i.e. minImageCount and minExtent
	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	VkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpu, m_surface, &surfaceCapabilities), "Failed to get surface capabilities");

	// Choose the appropriate surface format, extent and present mode
    VkSurfaceFormatKHR surfaceFormat = ChooseSwapchainSurfaceFormat(m_surface, m_gpu);
	VkExtent2D         extent        = ChooseSwapchainExtent(m_window, surfaceCapabilities);
	VkPresentModeKHR   presentMode   = ChooseSwapchainPresentMode(m_surface, m_gpu, VSync);

	// Actually create the VkSwapchain object
	VkSwapchainCreateInfoKHR createInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = m_surface,
		.minImageCount = glm::clamp(MaxFramesInFlight, surfaceCapabilities.minImageCount, surfaceCapabilities.maxImageCount),
		.imageFormat = surfaceFormat.format,
		.imageColorSpace = surfaceFormat.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = surfaceCapabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = presentMode,
		.clipped = VK_TRUE
	};

	VkCheck(vkCreateSwapchainKHR(vkDevice, &createInfo, nullptr, &m_swapchain.swapchain), "Failed to create swapchain");

	// We only specified the minimum number of images we want, so we have to check how many were actually created
	u32 imageCount = 0;
	vkGetSwapchainImagesKHR(vkDevice, m_swapchain.swapchain, &imageCount, nullptr);

	// Update swapchain properties
	m_swapchain.window = m_window->m_window;
	m_swapchain.extent = extent;
	m_swapchain.format = surfaceFormat.format;
	m_swapchain.VSync  = VSync;

	// Update swapchain images
	m_swapchain.images.resize(imageCount);
	m_swapchain.imageViews.resize(imageCount);
	m_swapchain.attachmentInfos.reserve(imageCount);

	vkGetSwapchainImagesKHR(vkDevice, m_swapchain.swapchain, &imageCount, m_swapchain.images.data());

	// Create a VkImageView and attachment info for each swapchain image
	for (u32 i = 0; i < imageCount; i++) {
		VkImageViewCreateInfo viewInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = m_swapchain.images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = surfaceFormat.format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		VkCheck(vkCreateImageView(vkDevice, &viewInfo, nullptr, &m_swapchain.imageViews[i]), "Failed to create image view");

		m_swapchain.attachmentInfos.push_back({
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = m_swapchain.imageViews[i],
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE
		});
	}
}

void VulkanDevice::RecreateSwapchain() {
	m_window->WaitResizeComplete();

	vkDeviceWaitIdle(vkDevice);

	DestroySwapchain();
	CreateSwapchain(m_swapchain.VSync);
}

void VulkanDevice::DestroySwapchain() {
    for (VkImageView imageView : m_swapchain.imageViews) {
		vkDestroyImageView(vkDevice, imageView, nullptr);
	}

	vkDestroySwapchainKHR(vkDevice, m_swapchain.swapchain, nullptr);

	m_swapchain = {};
}

bool VulkanDevice::BeginFrame() {
	vkWaitForFences(vkDevice, 1, &frame().inFlight, VK_TRUE, UINT32_MAX);

	VkResult res = vkAcquireNextImageKHR(vkDevice, m_swapchain.swapchain, UINT32_MAX, frame().imageAvailable, VK_NULL_HANDLE, &m_swapchain.imageIndex);
	if (res == VK_ERROR_OUT_OF_DATE_KHR) {
		RecreateSwapchain();
		return false;
	}
	else if ((res != VK_SUCCESS) && (res != VK_SUBOPTIMAL_KHR)) {
		VkCheck(res, "Failed to acquire swapchain image.");
	}

	VkCheck(vkResetFences(vkDevice, 1, &frame().inFlight), "Failed to reset inFlight fence");
	VkCheck(vkResetCommandPool(vkDevice, frame().commandPool, 0), "Failed to reset frame command pool.");
	VkCheck(vkResetDescriptorPool(vkDevice, frame().descriptorPool, 0), "Failed to reset frame descriptor pool");

	// Also reset the VulkanCommandBuffer vector
	frame().commandBuffers.clear();
	Check(GetCommandBuffer().m_index == 0, "Main rendering command buffer should always be at index 0");

	VkImageSubresourceRange subresourceRange = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0, .levelCount = 1,
		.baseArrayLayer = 0, .layerCount = 1
	};

	ImageBarrier(frame().commandBuffers[0].m_cmd, m_swapchain.images[m_swapchain.imageIndex], subresourceRange,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_NONE,						VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	return true;
}

void VulkanDevice::EndFrame() {
	VkCommandBuffer cmd = frame().commandBuffers[0].m_cmd;

	VkImageSubresourceRange subresourceRange = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0, .levelCount = 1,
		.baseArrayLayer = 0, .layerCount = 1
	};

	ImageBarrier(cmd, m_swapchain.images[m_swapchain.imageIndex], subresourceRange,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_NONE,
		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VkCheck(vkEndCommandBuffer(cmd), "Failed to end command buffer");

	VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = frame().imageAvailable,
		.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};

	VkSemaphoreSubmitInfo signalSemaphoreSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = frame().renderFinished,
		.stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT		// signal once all commandbuffers have been executed.
	};

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, 
		.commandBuffer = cmd
	};

	VkSubmitInfo2 submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &waitSemaphoreSubmitInfo,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &commandBufferSubmitInfo,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &signalSemaphoreSubmitInfo
	};

	vkQueueSubmit2(m_graphics.queue, 1, &submitInfo, frame().inFlight);

	VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &frame().renderFinished,
		.swapchainCount = 1,
		.pSwapchains = &m_swapchain.swapchain,
		.pImageIndices = &m_swapchain.imageIndex
	};

	VkResult res = vkQueuePresentKHR(m_graphics.queue, &presentInfo);

	if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || windowResized) {
		windowResized = false;
		RecreateSwapchain();
	}
	else if (res != VK_SUCCESS) {
		VkCheck(res, "Failed to present swapchain image");
	}

	m_frameIndex = (m_frameIndex + 1) % MaxFramesInFlight;
}

u32 VulkanDevice::FrameIdx() {
	return m_frameIndex;
}

void VulkanDevice::WaitIdle() {
	vkDeviceWaitIdle(vkDevice);
}

CommandBuffer& VulkanDevice::GetCommandBuffer() {
	return frame().commandBuffers.emplace_back(GetCommandBufferVK(), (u32)frame().commandBuffers.size());
}

CommandBuffer& VulkanDevice::GetFrameCommandBuffer() { 
	return frame().commandBuffers[0]; 
};

Format VulkanDevice::GetSwapchainFormat() { 
	return ConvertFormatVK(m_swapchain.format); 
}

Extent2D VulkanDevice::GetSwapchainExtent() {
	return { m_swapchain.extent.width, m_swapchain.extent.height };
}

void VulkanDevice::FlushCommandBuffer(CommandBuffer& commandBuffer) {
	VkCommandBuffer cmd = frame().commandBuffers[commandBuffer.m_index].m_cmd;
	FlushCommandBufferVK(cmd);
}

VkCommandBuffer VulkanDevice::GetCommandBufferVK() {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = frame().commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VkCommandBuffer commandBuffer;
	VkCheck(vkAllocateCommandBuffers(vkDevice, &allocInfo, &commandBuffer), "Failed to allocate command buffer");
	VkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer");

	return commandBuffer;
}

void VulkanDevice::FlushCommandBufferVK(VkCommandBuffer cmd) {
	VkCheck(vkEndCommandBuffer(cmd), "Failed to end command buffer");

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = cmd
	};

	VkSubmitInfo2 submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &commandBufferSubmitInfo
	};

	// Create fence to ensure the command buffer has finished executing
	VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence;
	VkCheck(vkCreateFence(vkDevice, &fenceInfo, nullptr, &fence), "Failed to create fence");

	VkCheck(vkQueueSubmit2(m_graphics.queue, 1, &submitInfo, fence), "Failed to submit command buffer to queue");

	// Wait for the fence to signal that the command buffer has finished executing
	// NOTE: maybe define a default timeout macro. For now, 1 << 32 ~ 5 seconds.
	VkCheck(vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, 1ull << 32), "Wait for fence failed");

	vkDestroyFence(vkDevice, fence, nullptr);
}

VkRenderingAttachmentInfo* VulkanDevice::GetSwapchainAttachmentInfo() {
	return &m_swapchain.attachmentInfos[m_swapchain.imageIndex];
}

void VulkanCommandBuffer::BeginRendering(Handle<Texture> depth, u32 layer, u32 width, u32 height) {
	VulkanResourceManager* rm = VulkanResourceManager::impl();

	VkRenderingAttachmentInfo depthAttachmentInfo = rm->GetTexture(depth)->GetAttachmentInfo(layer);

	VkRenderingInfo renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { .extent = { width, height } },
		.layerCount = 1,
		.pDepthAttachment = &depthAttachmentInfo
	};

	vkCmdBeginRendering(m_cmd, &renderingInfo);
}

void VulkanCommandBuffer::BeginRendering(Extent2D extent, const span<const Handle<Texture>>&& attachments, Handle<Texture> depth) {
	VulkanResourceManager* rm = VulkanResourceManager::impl();

	// TODO: add proper handle check
	VkRenderingAttachmentInfo depthAttachmentInfo = rm->GetTexture(depth)->GetAttachmentInfo();

	std::vector<VkRenderingAttachmentInfo> colorAttachments(attachments.size());
	for (u32 i = 0; i < attachments.size(); i++)
		colorAttachments[i] = rm->GetTexture(attachments[i])->GetAttachmentInfo();
	
	VkRenderingInfo renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { .extent = { extent.width, extent.height} },
		.layerCount = 1,
		.colorAttachmentCount = (u32)colorAttachments.size(),
		.pColorAttachments = colorAttachments.data(),
		.pDepthAttachment = &depthAttachmentInfo
	};

	vkCmdBeginRendering(m_cmd, &renderingInfo);
}


void VulkanCommandBuffer::BeginRenderingSwapchain() {
	VulkanDevice* device = VulkanDevice::impl();

	Extent2D extent = device->GetSwapchainExtent();
	VkRenderingInfo renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { .extent = { extent.width, extent.height } },
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = device->GetSwapchainAttachmentInfo(),
	};

	vkCmdBeginRendering(m_cmd, &renderingInfo);
}

void VulkanCommandBuffer::EndRendering() {
	vkCmdEndRendering(m_cmd);
}

void VulkanCommandBuffer::ImageBarrier(Handle<Texture> texture, Usage srcUsage, Usage dstUsage, u32 baseMip, u32 mipCount, u32 baseLayer, u32 layerCount) {
	VulkanResourceManager* rm = VulkanResourceManager::impl();

	VulkanTexture* image = rm->GetTexture(texture);

	VkImageMemoryBarrier2 barrier = GetVkImageBarrier(image->image, GetImageAspect(ConvertFormatVK(image->format)), srcUsage, dstUsage, baseMip, mipCount, baseLayer, layerCount);

	VkDependencyInfo dependencyInfo = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier
	};

	vkCmdPipelineBarrier2(m_cmd, &dependencyInfo);
}

void VulkanCommandBuffer::SetPipeline(Handle<Pipeline> handle) {
	VulkanResourceManager* rm = VulkanResourceManager::impl();

	VkPipeline pipeline = rm->GetPipeline(handle)->pipeline;
	vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	boundPipeline = handle;
}

void VulkanCommandBuffer::SetBindGroup(Handle<BindGroup> handle, u32 index, span<const u32> dynamicOffsets) {
	VulkanResourceManager* rm = VulkanResourceManager::impl();

	VkPipelineLayout pipelineLayout = rm->GetPipeline(boundPipeline)->layout;
	VkDescriptorSet  descriptorSet  = rm->GetBindGroup(handle)->set;
	vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, index, 1, &descriptorSet, (u32)dynamicOffsets.size(), dynamicOffsets.data());
}

void VulkanCommandBuffer::PushConstants(void* data, u32 offset, u32 size, u32 stages) {
	VulkanResourceManager* rm = VulkanResourceManager::impl();

	VkPipelineLayout pipelineLayout = rm->GetPipeline(boundPipeline)->layout;
	vkCmdPushConstants(m_cmd, pipelineLayout, ParseShaderStageFlags(stages), offset, size, data);
}

void VulkanCommandBuffer::SetVertexBuffer(Handle<Buffer> handle, u64 offset) {
	VulkanResourceManager* rm = VulkanResourceManager::impl();

	VkBuffer vertexBuffer = rm->GetBuffer(handle)->buffer;
	vkCmdBindVertexBuffers(m_cmd, 0, 1, &vertexBuffer, &offset);
}

void VulkanCommandBuffer::SetIndexBuffer(Handle<Buffer> handle, u64 offset, IndexType type) {
	VulkanResourceManager* rm = VulkanResourceManager::impl();

	VkBuffer indexBuffer = rm->GetBuffer(handle)->buffer;
	vkCmdBindIndexBuffer(m_cmd, indexBuffer, offset, ConvertIndexType(type));
}

void VulkanCommandBuffer::SetScissor(const Rect2D& scissor) {
	VkRect2D vkScissor = {
		.offset = {.x = scissor.offset.x, .y = scissor.offset.y },
		.extent = {.width = scissor.extent.width, .height = scissor.extent.height }
	};

	vkCmdSetScissor(m_cmd, 0, 1, &vkScissor);
}

void VulkanCommandBuffer::SetViewport(float width, float height) {
	VkViewport viewport = {
		.x		  = 0.0f,
		.y		  = 0.0f,
		.width	  = width,
		.height	  = height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	vkCmdSetViewport(m_cmd, 0, 1, &viewport);
}

void VulkanCommandBuffer::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) {
	vkCmdDraw(m_cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandBuffer::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, u32 vertexOffset, u32 firstInstance) {
	vkCmdDrawIndexed(m_cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}
