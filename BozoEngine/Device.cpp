#include "Common.h"

#include "Logging.h"
#include "Device.h"

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
	VkCheck(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));

	std::vector<VkPhysicalDevice> devices(deviceCount);
	VkCheck(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

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

static u32 GetGraphicsQueueIndex(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
	u32 queueCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, nullptr);
	std::vector<VkQueueFamilyProperties> queues(queueCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queues.data());

	for (u32 i = 0; i < queueCount; i++) {
		VkBool32 presentSupport = false;
		VkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport));

		if (presentSupport && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			return i;
		}
	}

	Check(false, "No queue family supporting graphics + present found");
}

static VkQueue CreateQueue(VkDevice device, u32 queueFamilyIndex) {
	VkQueue queue;
	vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
	return queue;
}

static VkDevice CreateDevice(VkPhysicalDevice physicalDevice, u32 queueFamilyIndex) {
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
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
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

Device CreateDevice(GLFWwindow* window) {
	VkCheck(volkInitialize(), "Failed to initialize volk");

	VkInstance instance = CreateInstance();
	volkLoadInstance(instance);

	VkDebugUtilsMessengerEXT debugMessenger = CreateDebugMessenger(instance);
	VkSurfaceKHR surface = CreateSurface(window, instance);
	VkPhysicalDevice physicalDevice = CreatePhysicalDevice(instance);
	u32 queueFamilyIndex = GetGraphicsQueueIndex(physicalDevice, surface);

	VkDevice device = CreateDevice(physicalDevice, queueFamilyIndex);
	volkLoadDevice(device);

	VkQueue queue = CreateQueue(device, queueFamilyIndex);
	VkCommandPool commandPool = CreateCommandPool(device, queueFamilyIndex);

	return {
		.instance = instance,
		.debugMessenger = debugMessenger,
		.surface = surface,
		.physicalDevice = physicalDevice,
		.device = device,
		.graphicsQueue = {
			.queue = queue,
			.index = queueFamilyIndex
		},
		.commandPool = commandPool
	};
}

void DestroyDevice(Device& device) {
	vkDestroyCommandPool(device.device, device.commandPool, nullptr);
	vkDestroyDevice(device.device, nullptr);

#if _DEBUG
	vkDestroyDebugUtilsMessengerEXT(device.instance, device.debugMessenger, nullptr);
#endif

	vkDestroySurfaceKHR(device.instance, device.surface, nullptr);
	vkDestroyInstance(device.instance, nullptr);
}