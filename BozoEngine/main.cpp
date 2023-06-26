#include <assert.h>
#include <stdio.h>
#include <stdint.h>

typedef uint8_t	 u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t	i8;
typedef int16_t	i16;
typedef int32_t	i32;
typedef int64_t	i64;

#include <volk.h>
#include <GLFW/glfw3.h>

// SGR escape sequences: https://docs.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
#define SGR_SET_BG_GRAY  "\x1B[100;1m"
#define SGR_SET_BG_BLUE	 "\x1B[44;1m"
#define SGR_SET_BG_RED	 "\x1B[41;1m"
#define SGR_SET_TXT_BLUE "\x1B[34;1m"
#define SGR_SET_DEFAULT  "\x1B[0m"

#include <source_location>
#include <vulkan/vk_enum_string_helper.h>

#define Check(expression, message) if (!(expression)) { \
		std::source_location check_location = std::source_location::current(); \
		fprintf(stderr, SGR_SET_BG_RED "[CHECK]" SGR_SET_DEFAULT " %s: `" #expression "`\n\tfile: %s(%u:%u) in `%s`\n", \
			message,							\
			check_location.file_name(),			\
			check_location.line(),				\
			check_location.column(),			\
			check_location.function_name());	\
		abort();								\
	}

VkResult VkCheck(VkResult result, const char* message = "", std::source_location location = std::source_location::current()) {
	if (result != VK_SUCCESS) {
		fprintf(stderr, SGR_SET_BG_RED "[CHECK]" SGR_SET_DEFAULT " %s: `%s`\n\tfile: %s(%u:%u) in `%s`\n", 
			message, string_VkResult(result),
			location.file_name(),
			location.line(),
			location.column(),
			location.function_name());
		abort();
	}
	return result;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) 
{
	if      (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   printf(SGR_SET_BG_RED  "[ERROR]" SGR_SET_DEFAULT "   ");
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) printf(SGR_SET_BG_BLUE "[WARNING]" SGR_SET_DEFAULT " ");
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) printf("[VERBOSE] ");

	if      (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)		printf("Validation:  ");
	else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)		printf("Performance: ");
	else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) printf("Address binding: ");
	
	const char* vkSpecLinkBegin = strstr(pCallbackData->pMessage, "https");
	if (vkSpecLinkBegin) {
		const char* vkSpecLinkEnd = strchr(vkSpecLinkBegin, ')');
		printf("%.*s", (u32)ptrdiff_t(vkSpecLinkBegin - pCallbackData->pMessage), pCallbackData->pMessage);
		printf(SGR_SET_TXT_BLUE "%.*s" SGR_SET_DEFAULT ")\n", (u32)ptrdiff_t(vkSpecLinkEnd - vkSpecLinkBegin), vkSpecLinkBegin);
	}
	else {
		printf("%s\n", pCallbackData->pMessage);
	}

	return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT GetDebugMessengerCreateInfo() {
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

// Initialize glfw and create a window of width/height
GLFWwindow* InitWindow(int width, int height) {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	return glfwCreateWindow(width, height, "BozoEngine", nullptr, nullptr);
}

void CleanupWindow(GLFWwindow* window) {
	glfwDestroyWindow(window);
	glfwTerminate();
}

VkInstance instance;
VkDebugUtilsMessengerEXT debugMessenger;

VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice device;
VkQueue graphicsQueue;

void PrintAvailableVulkanExtensions() {
	u32 extensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	VkExtensionProperties extensions[32];

	assert(extensionCount < 32);
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions);

	printf("Available extensions:\n");
	for (int i = 0; i < extensionCount; i++) {
		printf("\t%s\n", extensions[i].extensionName);
	}
}

void CreateInstance() {
	VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Hello Triangle",
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
		.enabledLayerCount = sizeof(validationLayers) / sizeof(char*),
		.ppEnabledLayerNames = validationLayers,
#endif
		.enabledExtensionCount = sizeof(extensions) / sizeof(char*),
		.ppEnabledExtensionNames = extensions,
	};

	VkCheck(vkCreateInstance(&createInfo, nullptr, &instance), "Failed to create instance.");
	volkLoadInstance(instance);
}

void CreateDebugMessenger() {
	VkDebugUtilsMessengerCreateInfoEXT createInfo = GetDebugMessengerCreateInfo();
	VkCheck(vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger), "Failed to create debug messenger.");
}

void CreatePhysicalDevice() {
	u32 deviceCount = 8;
	VkPhysicalDevice devices[8];
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices);
	Check(deviceCount > 0, "Failed to find GPUs with Vulkan support.");

	for (u32 i = 0; i < deviceCount; i++) {
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(devices[i], &deviceFeatures);

		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			physicalDevice = devices[i];
			printf("   " SGR_SET_BG_GRAY "[INFO]" SGR_SET_DEFAULT " Picked GPU: %s\n", deviceProperties.deviceName);
			break;
		}
	}

	Check(physicalDevice != VK_NULL_HANDLE, "Failed to find a discrete GPU.");
}

u32 GetGraphicsQueueFamily() {
	u32 queueCount = 8;
	VkQueueFamilyProperties queues[8];
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queues);

	for (int i = 0; i < queueCount; i++) {
		if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			return i;
		}
	}

	Check(false, "No queue family supporting graphics found.");
}

void CreateLogicalDevice() {
	u32 queueFamilyIndex = GetGraphicsQueueFamily();
	float queuePriority[] = { 1.0f };
	VkDeviceQueueCreateInfo queueCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = queueFamilyIndex,
		.queueCount = 1,
		.pQueuePriorities = queuePriority
	};

	VkPhysicalDeviceFeatures deviceFeatures = {};

	// Note: enabledLayer fields are deprecated, but should probably still be set here for compatibility.
	VkDeviceCreateInfo deviceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueCreateInfo,
		.pEnabledFeatures = &deviceFeatures
	};

	VkCheck(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device), "Failed to create logical device.");

	vkGetDeviceQueue(device, queueFamilyIndex, 0, &graphicsQueue);
}

void InitVulkan() {
	volkInitialize();
	CreateInstance();
	CreateDebugMessenger();
	CreatePhysicalDevice();
	CreateLogicalDevice();
}

void CleanupVulkan() {
	vkDestroyDevice(device, nullptr);
	vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
	vkDestroyInstance(instance, nullptr);
}

int main(int argc, char* argv[]) {
	GLFWwindow* window = InitWindow(800, 600);

	InitVulkan();

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	CleanupVulkan();

	CleanupWindow(window);

	return 0;
}