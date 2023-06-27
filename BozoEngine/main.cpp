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

#include <vector>

#include <volk.h>
#include <GLFW/glfw3.h>

// Only used to quickly expand inline code when writing
// Shouldn't actually be left in code.
#define VK_GET(identifier, type, vkFunction, ...) \
	u32 identifier ## Count; \
	vkFunction(__VA_ARGS__, &identifier ## Count, nullptr); \
	std::vector<type> identifier ## s(identifier ## Count); \
	vkFunction(__VA_ARGS__, &identifier ## Count, identifier ## s.data());

// Note that stuff like min(a++, b++) wont work w/ these macros.
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(v, lo, hi) (MAX(MIN((v), (hi)), (lo)))

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
GLFWwindow* window;
void InitWindow(int width, int height) {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	window = glfwCreateWindow(width, height, "BozoEngine", nullptr, nullptr);
}

void CleanupWindow() {
	glfwDestroyWindow(window);
	glfwTerminate();
}

// Temporary namespace to contain globals
namespace bz {
	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device;
	VkQueue queue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapchain;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	VkFormat swapchainImageFormat;
	VkExtent2D swapchainExtent;
}

void PrintAvailableVulkanExtensions() {
	u32 extensionCount;
	VkCheck(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr));

	std::vector<VkExtensionProperties> extensions(extensionCount);
	VkCheck(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()));

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

	VkCheck(vkCreateInstance(&createInfo, nullptr, &bz::instance), "Failed to create instance.");
	volkLoadInstance(bz::instance);
}

void CreateSurface() {
	VkCheck(glfwCreateWindowSurface(bz::instance, window, nullptr, &bz::surface), "Failed to create window surface.");
}

void CreateDebugMessenger() {
	VkDebugUtilsMessengerCreateInfoEXT createInfo = GetDebugMessengerCreateInfo();
	VkCheck(vkCreateDebugUtilsMessengerEXT(bz::instance, &createInfo, nullptr, &bz::debugMessenger), "Failed to create debug messenger.");
}

void CreatePhysicalDevice() {
	u32 deviceCount = 8;
	VkPhysicalDevice devices[8];
	VkCheck(vkEnumeratePhysicalDevices(bz::instance, &deviceCount, devices));
	Check(deviceCount > 0, "Failed to find GPUs with Vulkan support.");

	for (u32 i = 0; i < deviceCount; i++) {
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			bz::physicalDevice = devices[i]; 
			printf(SGR_SET_BG_GRAY "[INFO]" SGR_SET_DEFAULT "    Found suitable GPU: %s\n", deviceProperties.deviceName);
			break;
		}
	}

	Check(bz::physicalDevice != VK_NULL_HANDLE, "Failed to find a suitable GPU.");
}

u32 GetQueueFamily() {
	u32 queueCount = 8;
	VkQueueFamilyProperties queues[8];
	vkGetPhysicalDeviceQueueFamilyProperties(bz::physicalDevice, &queueCount, queues);

	for (int i = 0; i < queueCount; i++) {
		VkBool32 presentSupport = false;
		VkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(bz::physicalDevice, i, bz::surface, &presentSupport));

		if (presentSupport && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			return i;
		}
	}

	Check(false, "No queue family supporting graphics + present found.");
}

void CreateLogicalDevice() {
	u32 queueFamilyIndex = GetQueueFamily();
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
		.pNext = &features11
	};

	VkDeviceCreateInfo deviceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueCreateInfo,
		.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]),
		.ppEnabledExtensionNames = extensions
	};

	VkCheck(vkCreateDevice(bz::physicalDevice, &deviceCreateInfo, nullptr, &bz::device), "Failed to create logical device.");
	volkLoadDevice(bz::device);
	vkGetDeviceQueue(bz::device, queueFamilyIndex, 0, &bz::queue);
}

struct SwapchainSupport {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

SwapchainSupport QuerySwapchainSupport() {
	u32 formatCount, presentModeCount; 
	VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(bz::physicalDevice, bz::surface, &formatCount, nullptr));
	VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(bz::physicalDevice, bz::surface, &presentModeCount, nullptr));

	SwapchainSupport support = {
		.formats = std::vector<VkSurfaceFormatKHR>(formatCount),
		.presentModes = std::vector<VkPresentModeKHR>(presentModeCount)
	};

	VkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(bz::physicalDevice, bz::surface, &support.capabilities));
	VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(bz::physicalDevice, bz::surface, &formatCount, support.formats.data()));
	VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(bz::physicalDevice, bz::surface, &presentModeCount, support.presentModes.data()));

	return support;
}

VkSurfaceFormatKHR GetSwapchainFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
	for (const auto& format : formats) {
		if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return format;
		}
	}

	return formats[0];
}

VkExtent2D GetSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != ~0u) {
		return capabilities.currentExtent;
	}
	
	int width, height;
	glfwGetFramebufferSize(window, &width, &height);

	VkExtent2D extent = { 
		.width  = CLAMP(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		.height = CLAMP(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
	};

	return extent;
}

void CreateSwapchain() {
	SwapchainSupport support = QuerySwapchainSupport();

	VkSurfaceFormatKHR surfaceFormat = GetSwapchainFormat(support.formats);
	VkExtent2D extent = GetSwapchainExtent(support.capabilities);
	u32 imageCount = CLAMP(2, support.capabilities.minImageCount, support.capabilities.maxImageCount);

	VkSwapchainCreateInfoKHR createInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = bz::surface,
		.minImageCount = imageCount,
		.imageFormat = surfaceFormat.format,
		.imageColorSpace = surfaceFormat.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = support.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE
	};

	VkCheck(vkCreateSwapchainKHR(bz::device, &createInfo, nullptr, &bz::swapchain), "Failed to create swapchain.");

	// We only specified the minimum number of images we want, so we have to check how many were actually created
	vkGetSwapchainImagesKHR(bz::device, bz::swapchain, &imageCount, nullptr); 
	bz::swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(bz::device, bz::swapchain, &imageCount, bz::swapchainImages.data());

	bz::swapchainImageFormat = surfaceFormat.format;
	bz::swapchainExtent = extent;
}

void CreateImageViews() {
	bz::swapchainImageViews.resize(bz::swapchainImages.size());

	for (int i = 0; i < bz::swapchainImages.size(); i++) {
		VkImageViewCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = bz::swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = bz::swapchainImageFormat,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.levelCount = 1,
				.layerCount = 1
			}
		};

		VkCheck(vkCreateImageView(bz::device, &createInfo, nullptr, &bz::swapchainImageViews[i]), "Failed to create image views.");
	}
}

void CreateGraphicsPipeline() {

}

void InitVulkan() {
	VkCheck(volkInitialize(), "Failed to initialzie volk.");
	CreateInstance();
	CreateDebugMessenger();
	CreateSurface();
	CreatePhysicalDevice();
	CreateLogicalDevice();
	CreateSwapchain();
	CreateImageViews();
	CreateGraphicsPipeline();
}

void CleanupVulkan() {
	for (auto imageView : bz::swapchainImageViews) {
		vkDestroyImageView(bz::device, imageView, nullptr);
	}

	vkDestroySwapchainKHR(bz::device, bz::swapchain, nullptr);
	vkDestroyDevice(bz::device, nullptr);
	vkDestroyDebugUtilsMessengerEXT(bz::instance, bz::debugMessenger, nullptr);
	vkDestroySurfaceKHR(bz::instance, bz::surface, nullptr);
	vkDestroyInstance(bz::instance, nullptr);
}

int main(int argc, char* argv[]) {
	InitWindow(800, 600);
	InitVulkan();

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	CleanupVulkan();
	CleanupWindow();

	return 0;
}