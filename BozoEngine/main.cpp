// Stuff TODO in the future: 
//	- At some point VMA should be integrated instead of making individual allocations for every buffer.
//	- Read up on driver developer recommendations (fx. suballocating vertex/index buffers inside the same VkBuffer)
//	- Add meshoptimizer
//	- Implement loading multiple mip levels from a file instead of creating them at runtime.

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>

#include <chrono> // ugh

typedef uint8_t	 u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t	i8;
typedef int16_t	i16;
typedef int32_t	i32;
typedef int64_t	i64;

#include <Windows.h>
#include <volk.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define FAST_OBJ_IMPLEMENTATION
#include <fast_obj.h>

#include "Logging.h"

constexpr u32 WIDTH = 800;
constexpr u32 HEIGHT = 600;
constexpr const char* MODEL_PATH = "models/viking_room.obj";
constexpr const char* TEXTURE_PATH = "textures/viking_room.png";

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;
	
	static VkVertexInputBindingDescription GetBindingDescription() {
		VkVertexInputBindingDescription bindingDescription = {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};

		return bindingDescription;
	}

	static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions() {
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
			{	// Position attribute
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, pos)
			},
			{	// Color attribute
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, color)
			},
			{	// Texture coordinate attribute
				.location = 2,
				.binding = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(Vertex, texCoord)
			}
		};

		return attributeDescriptions;
	}
};

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

constexpr int MAX_FRAMES_IN_FLIGHT = 2;
u32 currentFrame = 0;

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
	std::vector<VkFramebuffer> swapchainFramebuffers;
	VkFormat swapchainImageFormat;
	VkExtent2D swapchainExtent;

	VkRenderPass renderPass;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSets[MAX_FRAMES_IN_FLIGHT];

	VkPipelineLayout pipelineLayout;
	VkPipeline graphicsPipeline;

	VkCommandPool commandPool;
	VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];

	VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];

	u32 textureMipLevels;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;

	std::vector<Vertex> vertices;
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;

	std::vector<u32> indices;
	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;

	VkBuffer uniformBuffers[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory uniformBuffersMemory[MAX_FRAMES_IN_FLIGHT];
	void* uniformBuffersMapped[MAX_FRAMES_IN_FLIGHT];

	VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
	VkImage colorImage;
	VkDeviceMemory colorImageMemory;
	VkImageView colorImageView;

	bool framebufferResized = false;
}

static void FramebufferResizeCallback(GLFWwindow* window, int width, int height) {
	bz::framebufferResized = true;
}

// Initialize glfw and create a window of width/height
GLFWwindow* window;
void InitWindow(int width, int height) {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	window = glfwCreateWindow(width, height, "BozoEngine", nullptr, nullptr);
	glfwSetFramebufferSizeCallback(window, FramebufferResizeCallback);
}

void CleanupWindow() {
	glfwDestroyWindow(window);
	glfwTerminate();
}

void PrintAvailableVulkanExtensions() {
	u32 extensionCount;
	VkCheck(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr));

	std::vector<VkExtensionProperties> extensions(extensionCount);
	VkCheck(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()));

	printf("Available extensions:\n");
	for (u32 i = 0; i < extensionCount; i++) {
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

	VkCheck(vkCreateInstance(&createInfo, nullptr, &bz::instance), "Failed to create instance");
	volkLoadInstance(bz::instance);
}

void CreateSurface() {
	VkCheck(glfwCreateWindowSurface(bz::instance, window, nullptr, &bz::surface), "Failed to create window surface");
}

void CreateDebugMessenger() {
	VkDebugUtilsMessengerCreateInfoEXT createInfo = GetDebugMessengerCreateInfo();
	VkCheck(vkCreateDebugUtilsMessengerEXT(bz::instance, &createInfo, nullptr, &bz::debugMessenger), "Failed to create debug messenger");
}

VkSampleCountFlagBits GetMaxUsableSampleCount(VkSampleCountFlags colorSampleCounts, VkSampleCountFlags depthSampleCounts) {
	VkSampleCountFlags counts = colorSampleCounts & depthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT)  { return VK_SAMPLE_COUNT_8_BIT;  }
	if (counts & VK_SAMPLE_COUNT_4_BIT)  { return VK_SAMPLE_COUNT_4_BIT;  }
	if (counts & VK_SAMPLE_COUNT_2_BIT)  { return VK_SAMPLE_COUNT_2_BIT;  }

	return VK_SAMPLE_COUNT_1_BIT;
}

// Verifying that the device is suitable is not rigorous atm.
void CreatePhysicalDevice() {
	u32 deviceCount = 8;
	VkPhysicalDevice devices[8];
	VkCheck(vkEnumeratePhysicalDevices(bz::instance, &deviceCount, devices));
	Check(deviceCount > 0, "Failed to find GPUs with Vulkan support");

	for (u32 i = 0; i < deviceCount; i++) {
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(devices[i], &properties);
		VkPhysicalDeviceFeatures features;
		vkGetPhysicalDeviceFeatures(devices[i], &features);

		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && features.samplerAnisotropy) {
			bz::physicalDevice = devices[i];
			bz::msaaSamples = GetMaxUsableSampleCount(properties.limits.framebufferColorSampleCounts, properties.limits.framebufferDepthSampleCounts);
			printf(SGR_SET_BG_GRAY "[INFO]" SGR_SET_DEFAULT "    Found suitable GPU: `%s`. Max MSAA samples: %u\n", properties.deviceName, 1 << (31 - __lzcnt(bz::msaaSamples)));
			break;
		}
	}

	Check(bz::physicalDevice != VK_NULL_HANDLE, "Failed to find a suitable GPU");
}

u32 GetQueueFamily() {
	u32 queueCount = 8;
	VkQueueFamilyProperties queues[8];
	vkGetPhysicalDeviceQueueFamilyProperties(bz::physicalDevice, &queueCount, queues);

	for (u32 i = 0; i < queueCount; i++) {
		VkBool32 presentSupport = false;
		VkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(bz::physicalDevice, i, bz::surface, &presentSupport));

		if (presentSupport && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			return i;
		}
	}

	Check(false, "No queue family supporting graphics + present found");
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
		.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]),
		.ppEnabledExtensionNames = extensions
	};

	VkCheck(vkCreateDevice(bz::physicalDevice, &deviceCreateInfo, nullptr, &bz::device), "Failed to create logical device");
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
		.width = std::clamp((u32)width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		.height = std::clamp((u32)height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
	};

	return extent;
}

void CreateSwapchain() {
	SwapchainSupport support = QuerySwapchainSupport();

	VkSurfaceFormatKHR surfaceFormat = GetSwapchainFormat(support.formats);
	VkExtent2D extent = GetSwapchainExtent(support.capabilities);
	u32 imageCount = std::clamp(2u, support.capabilities.minImageCount, support.capabilities.maxImageCount);

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

	VkCheck(vkCreateSwapchainKHR(bz::device, &createInfo, nullptr, &bz::swapchain), "Failed to create swapchain");

	// We only specified the minimum number of images we want, so we have to check how many were actually created
	vkGetSwapchainImagesKHR(bz::device, bz::swapchain, &imageCount, nullptr); 
	bz::swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(bz::device, bz::swapchain, &imageCount, bz::swapchainImages.data());

	bz::swapchainImageFormat = surfaceFormat.format;
	bz::swapchainExtent = extent;
}

VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, u32 mipLevels) {
	VkImageViewCreateInfo viewInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = aspectFlags,
				.baseMipLevel = 0,
				.levelCount = mipLevels,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
	};

	VkImageView imageView;
	VkCheck(vkCreateImageView(bz::device, &viewInfo, nullptr, &imageView), "Failed to create image view");

	return imageView;
}

void CreateImageViews() {
	bz::swapchainImageViews.resize(bz::swapchainImages.size());

	for (int i = 0; i < bz::swapchainImages.size(); i++) {
		bz::swapchainImageViews[i] = CreateImageView(bz::swapchainImages[i], bz::swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}
}

VkShaderModule CreateShaderModule(const char* path) {
	FILE* fp = fopen(path, "rb");
	Check(fp != nullptr, "File: `%s` failed to open", path);

	fseek(fp, 0, SEEK_END);
	long length = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	Check(length > 0, "File: `%s` was empty", path);

	char* buffer = new char[length];
	Check(buffer, "Failed to allocate buffer");

	size_t read = fread(buffer, 1, length, fp);
	Check(read == length, "Failed to read all contents of `%s`", path);
	fclose(fp);

	VkShaderModuleCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = read,
		.pCode = (u32*)buffer
	};

	VkShaderModule shaderModule;
	VkCheck(vkCreateShaderModule(bz::device, &createInfo, nullptr, &shaderModule), "Failed to create shader module");

	delete[] buffer;

	return shaderModule;
}

void CreateRenderPass() {
	VkAttachmentDescription colorAttachment = {
		.format = bz::swapchainImageFormat,
		.samples = bz::msaaSamples,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,					// dont care. Note that contents wont be preserved. Ok, since loadOp = clear
		.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription depthAttachment = {
		.format = VK_FORMAT_D32_SFLOAT,
		.samples = bz::msaaSamples,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription colorAttachmentResolve = {
		.format = bz::swapchainImageFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	};

	VkAttachmentReference colorAttachmentRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkAttachmentReference depthAttachmentRef = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	VkAttachmentReference colorAttachmentResolveRef = {
		.attachment = 2,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentRef,
		.pResolveAttachments = &colorAttachmentResolveRef,
		.pDepthStencilAttachment = &depthAttachmentRef,
	};

	VkSubpassDependency dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,		// implicit subpass before the renderpass
		.dstSubpass = 0,						// our subpass
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
	};

	VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment, colorAttachmentResolve };
	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = sizeof(attachments) / sizeof(attachments[0]),
		.pAttachments = attachments,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &dependency
	};

	VkCheck(vkCreateRenderPass(bz::device, &renderPassInfo, nullptr, &bz::renderPass), "Failed to create render pass");
}

void CreateDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};

	VkDescriptorSetLayoutBinding samplerLayoutBinding = {
		.binding = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.pImmutableSamplers = nullptr
	};

	VkDescriptorSetLayoutBinding bindings[] = { uboLayoutBinding, samplerLayoutBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(bindings) / sizeof(bindings[0]),
		.pBindings = bindings
	};

	VkCheck(vkCreateDescriptorSetLayout(bz::device, &layoutInfo, nullptr, &bz::descriptorSetLayout), "Failed to create a descriptor set layout");
}

void CreateGraphicsPipeline() {
	VkShaderModule vertShaderModule = CreateShaderModule("shaders/triangle.vert.spv");
	VkShaderModule fragShaderModule = CreateShaderModule("shaders/triangle.frag.spv");

	VkPipelineShaderStageCreateInfo shaderStages[] = {
		{ // Vertex shader module
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertShaderModule,
			.pName = "main"
		},
		{ // Fragment shader module
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = fragShaderModule,
		.pName = "main"
		}
	};

	VkDynamicState dynamicState[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamicState) / sizeof(dynamicState[0]),
		.pDynamicStates = dynamicState
	};

	VkPipelineViewportStateCreateInfo viewportStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1
	};

	VkVertexInputBindingDescription bindingDescription = Vertex::GetBindingDescription();
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions = Vertex::GetAttributeDescriptions();
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &bindingDescription,
		.vertexAttributeDescriptionCount = (u32)attributeDescriptions.size(),
		.pVertexAttributeDescriptions = attributeDescriptions.data()
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE
	};

	VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,		// depth clamp discards fragments outside the near/far planes. Usefull for shadow maps, requires enabling a GPU feature.
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, // counter clockwise due to the y-flip of the projection matrix
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisampeInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = bz::msaaSamples,
		.sampleShadingEnable = VK_FALSE
	};

	VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
		.minDepthBounds = 0.0f,
		.maxDepthBounds = 1.0f
	};

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = &colorBlendAttachment
	};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &bz::descriptorSetLayout
	};

	VkCheck(vkCreatePipelineLayout(bz::device, &pipelineLayoutInfo, nullptr, &bz::pipelineLayout), "Failed to create pipeline layout");

	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = shaderStages,
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssemblyInfo,
		.pViewportState = &viewportStateInfo,
		.pRasterizationState = &rasterizationInfo,
		.pMultisampleState = &multisampeInfo,
		.pDepthStencilState = &depthStencilInfo,
		.pColorBlendState = &colorBlendStateInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = bz::pipelineLayout,
		.renderPass = bz::renderPass,
		.subpass = 0
	};

	VkCheck(vkCreateGraphicsPipelines(bz::device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bz::graphicsPipeline), "Failed to create graphics pipeline");

	vkDestroyShaderModule(bz::device, vertShaderModule, nullptr);
	vkDestroyShaderModule(bz::device, fragShaderModule, nullptr);
}

u32 FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(bz::physicalDevice, &memProperties);

	for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	Check(false, "Failed to find suitable memory type");
}

void CreateImage(u32 width, u32 height, u32 mipLevels, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
	VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {
			.width = width,
			.height = height,
			.depth = 1
		},
		.mipLevels = mipLevels,
		.arrayLayers = 1,
		.samples = samples,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VkCheck(vkCreateImage(bz::device, &imageInfo, nullptr, &image), "Failed to create image");

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(bz::device, image, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
	};

	VkCheck(vkAllocateMemory(bz::device, &allocInfo, nullptr, &imageMemory), "Failed to allocate image memory");
	VkCheck(vkBindImageMemory(bz::device, image, imageMemory, 0), "Failed to bind VkDeviceMemory to VkImage");
}

void CreateColorResources() {
	VkFormat colorFormat = bz::swapchainImageFormat;

	CreateImage(bz::swapchainExtent.width, bz::swapchainExtent.height, 1, bz::msaaSamples, colorFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		bz::colorImage, bz::colorImageMemory);
	bz::colorImageView = CreateImageView(bz::colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void CreateDepthResources() {
	// TODO: should query supported formats and select from them.

	CreateImage(bz::swapchainExtent.width, bz::swapchainExtent.height, 1, bz::msaaSamples,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		bz::depthImage, bz::depthImageMemory);
	bz::depthImageView = CreateImageView(bz::depthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

void CreateFramebuffers() {
	bz::swapchainFramebuffers.resize(bz::swapchainImageViews.size());

	for (int i = 0; i < bz::swapchainImageViews.size(); i++) {
		VkImageView attachments[] = {
			bz::colorImageView,
			bz::depthImageView,
			bz::swapchainImageViews[i]
		};

		VkFramebufferCreateInfo framebufferInfo = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = bz::renderPass,
			.attachmentCount = sizeof(attachments) / sizeof(attachments[0]),
			.pAttachments = attachments,
			.width = bz::swapchainExtent.width,
			.height = bz::swapchainExtent.height,
			.layers = 1
		};

		VkCheck(vkCreateFramebuffer(bz::device, &framebufferInfo, nullptr, &bz::swapchainFramebuffers[i]), "Failed to create framebuffer");
	}
}

void CreateCommandPool() {
	VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = GetQueueFamily()
	};

	VkCheck(vkCreateCommandPool(bz::device, &poolInfo, nullptr, &bz::commandPool), "Failed to create command pool");
}

void CreateDescriptorPool() {
	VkDescriptorPoolSize poolSizes[] = {
		{ // uniform buffer descriptor pool
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = MAX_FRAMES_IN_FLIGHT
		},
		{ // combined image sampler descriptor pool
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_FRAMES_IN_FLIGHT
		}
	};

	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = MAX_FRAMES_IN_FLIGHT,
		.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]),
		.pPoolSizes = poolSizes
	};

	VkCheck(vkCreateDescriptorPool(bz::device, &poolInfo, nullptr, &bz::descriptorPool), "Failed to create descriptor pool");
}

void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VkCheck(vkCreateBuffer(bz::device, &bufferInfo, nullptr, &buffer), "Failed to create buffer");

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(bz::device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
	};

	VkCheck(vkAllocateMemory(bz::device, &allocInfo, nullptr, &bufferMemory), "Failed to allocate vertex buffer memory");
	VkCheck(vkBindBufferMemory(bz::device, buffer, bufferMemory, 0), "Failed to bind DeviceMemory to VkBuffer");
}

// TODO: should allocate a separate command pool for these kinds of short-lived buffers.
// When we do, use the VK_COMMAND_POOL_CREATE_TRANSIENT_BIT flag during command pool generation.
VkCommandBuffer BeginSingleTimeCommands() {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = bz::commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer commandBuffer;
	VkCheck(vkAllocateCommandBuffers(bz::device, &allocInfo, &commandBuffer), "Failed to allocate command buffer");

	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer");

	return commandBuffer;
}

void EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
	VkCheck(vkEndCommandBuffer(commandBuffer), "Failed to end command buffer");

	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &commandBuffer
	};

	VkCheck(vkQueueSubmit(bz::queue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit command buffer to queue");
	VkCheck(vkQueueWaitIdle(bz::queue), "QueueWaitIdle failed");

	vkFreeCommandBuffers(bz::device, bz::commandPool, 1, &commandBuffer);
}

void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
	VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

	VkBufferCopy copyRegion = {
		.srcOffset = 0,
		.dstOffset = 0,
		.size = size
	};
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	EndSingleTimeCommands(commandBuffer);
}

void TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, u32 mipLevels) {
	//VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else {
		Check(false, "Unsupported layout transition");
	}

	vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	//EndSingleTimeCommands(commandBuffer);
}

void CopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, u32 width, u32 height) {
	//VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

	VkBufferImageCopy region = {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.imageOffset = { 0, 0, 0},
		.imageExtent = {
			.width = width,
			.height = height,
			.depth = 1
		}
	};

	vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	//EndSingleTimeCommands(commandBuffer);
}

void GenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image, VkFormat imageFormat, i32 width, i32 height, u32 mipLevels) {
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(bz::physicalDevice, imageFormat, &formatProperties);
	Check(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "Texture image does not support linear blitting");

	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};

	i32 mipWidth = width;
	i32 mipHeight = height;
	for (u32 i = 1; i < mipLevels; i++) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer, 
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 
			0, nullptr, 
			0, nullptr, 
			1, &barrier);

		VkImageBlit blit = {
			.srcSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i - 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
			.srcOffsets = {
				{ 0, 0, 0 },
				{ mipWidth, mipHeight, 1 }
			},
			.dstSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
			.dstOffsets = { 
				{ 0, 0, 0 },
				{ mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 }
			}
		};

		vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		if (mipWidth  > 1)	mipWidth /= 2;
		if (mipHeight > 1)	mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, nullptr,
		0, nullptr,
		1, &barrier);
}

void CreateTextureImage() {
	i32 width, height, channels;
	stbi_uc* pixels = stbi_load(TEXTURE_PATH, &width, &height, &channels, STBI_rgb_alpha);
	Check(pixels != nullptr, "Failed to load: `%s`", TEXTURE_PATH);

	bz::textureMipLevels = (u32)(std::floor(std::log2(std::max(width, height))) + 1);
	VkDeviceSize size = width * height * STBI_rgb_alpha;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	CreateBuffer(size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(bz::device, stagingBufferMemory, 0, size, 0, &data);
	memcpy(data, pixels, size);
	vkUnmapMemory(bz::device, stagingBufferMemory);

	stbi_image_free(pixels);

	CreateImage(width, height, bz::textureMipLevels, VK_SAMPLE_COUNT_1_BIT,
		VK_FORMAT_R8G8B8A8_SRGB, 
		VK_IMAGE_TILING_OPTIMAL, 
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
		bz::textureImage, bz::textureImageMemory);

	// Combine transition/copies into a single command buffer
	// If stuff breaks, revert to calling BeginSingleTimeCommands inside every helper function.
	VkCommandBuffer setupCommandBuffer = BeginSingleTimeCommands();

	TransitionImageLayout(setupCommandBuffer, bz::textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bz::textureMipLevels);
	CopyBufferToImage(setupCommandBuffer, stagingBuffer, bz::textureImage, width, height);
	//TransitionImageLayout(setupCommandBuffer, bz::textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, bz::textureMipLevels);
	GenerateMipmaps(setupCommandBuffer, bz::textureImage, VK_FORMAT_R8G8B8A8_SRGB, width, height, bz::textureMipLevels);

	EndSingleTimeCommands(setupCommandBuffer);

	vkDestroyBuffer(bz::device, stagingBuffer, nullptr);
	vkFreeMemory(bz::device, stagingBufferMemory, nullptr);
}

void CreateTextureImageView() {
	bz::textureImageView = CreateImageView(bz::textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, bz::textureMipLevels);
}

void CreateTextureSampler() {
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(bz::physicalDevice, &properties);

	VkSamplerCreateInfo samplerInfo = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias = 0.0f,
		.anisotropyEnable = VK_TRUE,
		.maxAnisotropy = properties.limits.maxSamplerAnisotropy,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = (float)bz::textureMipLevels,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE
	};

	VkCheck(vkCreateSampler(bz::device, &samplerInfo, nullptr, &bz::textureSampler), "Failed to create texture sampler");
}

void LoadModel() {
	fastObjMesh* mesh = fast_obj_read(MODEL_PATH);
	Check(mesh != nullptr, "Failed to load obj file: `%s`", MODEL_PATH);

	bz::vertices.reserve(mesh->index_count);
	bz::indices.reserve(mesh->index_count);
	for (u32 i = 0; i < mesh->index_count; i++) {
		fastObjIndex index = mesh->indices[i];
		bz::indices.push_back(i);
		bz::vertices.push_back({
			.pos = {
				mesh->positions[3 * index.p + 0],
				mesh->positions[3 * index.p + 1],
				mesh->positions[3 * index.p + 2]
			},
			.color = { 1.0f, 1.0f, 1.0f },
			.texCoord = {
				mesh->texcoords[2 * index.t + 0],
				1.0f - mesh->texcoords[2 * index.t + 1]
			}
		});
	}

	fast_obj_destroy(mesh);
}

void CreateVertexBuffer() {
	VkDeviceSize bufferSize = sizeof(bz::vertices[0]) * bz::vertices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	CreateBuffer(bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
		stagingBuffer, stagingBufferMemory);

	void* data;
	VkCheck(vkMapMemory(bz::device, stagingBufferMemory, 0, bufferSize, 0, &data), "Failed to map memory");
	memcpy(data, bz::vertices.data(), bufferSize);
	vkUnmapMemory(bz::device, stagingBufferMemory);

	CreateBuffer(bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
		bz::vertexBuffer, bz::vertexBufferMemory);

	CopyBuffer(stagingBuffer, bz::vertexBuffer, bufferSize);

	vkDestroyBuffer(bz::device, stagingBuffer, nullptr);
	vkFreeMemory(bz::device, stagingBufferMemory, nullptr);
}

void CreateIndexBuffer() {
	VkDeviceSize bufferSize = sizeof(bz::indices[0]) * bz::indices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	CreateBuffer(bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
		stagingBuffer, stagingBufferMemory);
	
	void* data;
	VkCheck(vkMapMemory(bz::device, stagingBufferMemory, 0, bufferSize, 0, &data), "Failed to map memory");
	memcpy(data, bz::indices.data(), bufferSize);
	vkUnmapMemory(bz::device, stagingBufferMemory);

	CreateBuffer(bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
		bz::indexBuffer, bz::indexBufferMemory);

	CopyBuffer(stagingBuffer, bz::indexBuffer, bufferSize);

	vkDestroyBuffer(bz::device, stagingBuffer, nullptr);
	vkFreeMemory(bz::device, stagingBufferMemory, nullptr);
}

void CreateUniformBuffers() {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		CreateBuffer(bufferSize, 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			bz::uniformBuffers[i], bz::uniformBuffersMemory[i]);

		VkCheck(vkMapMemory(bz::device, bz::uniformBuffersMemory[i], 0, bufferSize, 0, &bz::uniformBuffersMapped[i]), "Failed to map memory");
	}
}

void CreateDescriptorSets() {
	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) { layouts[i] = bz::descriptorSetLayout; }

	VkDescriptorSetAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = bz::descriptorPool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
		.pSetLayouts = layouts
	};

	VkCheck(vkAllocateDescriptorSets(bz::device, &allocInfo, bz::descriptorSets), "Failed to allocate descriptor sets");

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkDescriptorBufferInfo bufferInfo = {
			.buffer = bz::uniformBuffers[i],
			.offset = 0,
			.range = sizeof(UniformBufferObject)
		};

		VkDescriptorImageInfo imageInfo = {
			.sampler = bz::textureSampler,
			.imageView = bz::textureImageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		VkWriteDescriptorSet descriptorWrites[] = {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = bz::descriptorSets[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &bufferInfo
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = bz::descriptorSets[i],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &imageInfo
			}
		};

		vkUpdateDescriptorSets(bz::device, sizeof(descriptorWrites) / sizeof(descriptorWrites[0]), descriptorWrites, 0, nullptr);
	}
}

void CreateCommandBuffers() {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = bz::commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = sizeof(bz::commandBuffers) / sizeof(bz::commandBuffers[0])
	};

	VkCheck(vkAllocateCommandBuffers(bz::device, &allocInfo, bz::commandBuffers), "Failed to allocate command buffers");
}

void RecordCommandBuffer(VkCommandBuffer commandBuffer, u32 imageIndex) {
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	VkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin recording command buffer!");

	// Order of clearvalues should be identical to the order of attachments!
	VkClearValue clearColors[] = {
		{{0.0f, 0.0f, 0.0f}},	// clear color to black
		{1.0f, 0.0f}			// clear depth to far (1.0f)
	};
	VkRenderPassBeginInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = bz::renderPass,
		.framebuffer = bz::swapchainFramebuffers[imageIndex],
		.renderArea = {
			.offset = {0, 0},
			.extent = bz::swapchainExtent
		},
		.clearValueCount = sizeof(clearColors) / sizeof(clearColors[0]),
		.pClearValues = clearColors
	};

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::graphicsPipeline);

	VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)bz::swapchainExtent.width,
		.height = (float)bz::swapchainExtent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = bz::swapchainExtent
	};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkBuffer vertexBuffers[] = { bz::vertexBuffer };
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, bz::indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::pipelineLayout, 0, 1, &bz::descriptorSets[currentFrame], 0, nullptr);

	vkCmdDrawIndexed(commandBuffer, (u32)bz::indices.size(), 1, 0, 0, 0);

	vkCmdEndRenderPass(commandBuffer);

	VkCheck(vkEndCommandBuffer(commandBuffer), "Failed to record command buffer");
}

void CreateSyncObjects() {
	VkSemaphoreCreateInfo semaphoreInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};

	VkFenceCreateInfo fenceInfo = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkCheck(vkCreateSemaphore(bz::device, &semaphoreInfo, nullptr, &bz::imageAvailableSemaphores[i]), "Failed to create imageAvailable semaphore");
		VkCheck(vkCreateSemaphore(bz::device, &semaphoreInfo, nullptr, &bz::renderFinishedSemaphores[i]), "Failed to create renderFinished semaphore");
		VkCheck(vkCreateFence(bz::device, &fenceInfo, nullptr, &bz::inFlightFences[i]), "Failed to create inFlight fence");
	}
}

void InitVulkan() {
	VkCheck(volkInitialize(), "Failed to initialzie volk");

	CreateInstance();

#if _DEBUG
	CreateDebugMessenger();
#endif

	CreateSurface();

	CreatePhysicalDevice();
	CreateLogicalDevice();

	CreateSwapchain();
	CreateImageViews();

	CreateRenderPass();
	CreateDescriptorSetLayout();
	CreateGraphicsPipeline();

	CreateColorResources();
	CreateDepthResources();
	CreateFramebuffers();

	CreateCommandPool();
	CreateDescriptorPool();

	CreateTextureImage();
	CreateTextureImageView();
	CreateTextureSampler();

	LoadModel();
	CreateVertexBuffer();
	CreateIndexBuffer();
	CreateUniformBuffers();
	CreateDescriptorSets();

	CreateCommandBuffers();

	CreateSyncObjects();
}

void CleanupSwapchain() {
	vkDestroyImageView(bz::device, bz::colorImageView, nullptr);
	vkDestroyImage(bz::device, bz::colorImage, nullptr);
	vkFreeMemory(bz::device, bz::colorImageMemory, nullptr);

	vkDestroyImageView(bz::device, bz::depthImageView, nullptr);
	vkDestroyImage(bz::device, bz::depthImage, nullptr);
	vkFreeMemory(bz::device, bz::depthImageMemory, nullptr);

	for (auto framebuffer : bz::swapchainFramebuffers) {
		vkDestroyFramebuffer(bz::device, framebuffer, nullptr);
	}

	for (auto imageView : bz::swapchainImageViews) {
		vkDestroyImageView(bz::device, imageView, nullptr);
	}

	vkDestroySwapchainKHR(bz::device, bz::swapchain, nullptr);
}

// In theory the swap chain image could change during the applications lifetime,
// for example if the window was moved between an sdr and hdr display.
// We dont handle those changes.
void RecreateSwapchain() {
	int width = 0, height = 0;
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(bz::device);

	CleanupSwapchain();
	CreateSwapchain();

	CreateImageViews();
	CreateColorResources();
	CreateDepthResources();

	CreateFramebuffers();
}

void CleanupVulkan() {
	CleanupSwapchain();

	vkDestroySampler(bz::device, bz::textureSampler, nullptr);
	vkDestroyImageView(bz::device, bz::textureImageView, nullptr);
	vkDestroyImage(bz::device, bz::textureImage, nullptr);
	vkFreeMemory(bz::device, bz::textureImageMemory, nullptr);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroyBuffer(bz::device, bz::uniformBuffers[i], nullptr);
		vkFreeMemory(bz::device, bz::uniformBuffersMemory[i], nullptr);
	}

	vkDestroyDescriptorPool(bz::device, bz::descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(bz::device, bz::descriptorSetLayout, nullptr);

	vkDestroyBuffer(bz::device, bz::indexBuffer, nullptr);
	vkFreeMemory(bz::device, bz::indexBufferMemory, nullptr);

	vkDestroyBuffer(bz::device, bz::vertexBuffer, nullptr);
	vkFreeMemory(bz::device, bz::vertexBufferMemory, nullptr);

	vkDestroyPipeline(bz::device, bz::graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(bz::device, bz::pipelineLayout, nullptr);

	vkDestroyRenderPass(bz::device, bz::renderPass, nullptr);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(bz::device, bz::imageAvailableSemaphores[i], nullptr);
		vkDestroySemaphore(bz::device, bz::renderFinishedSemaphores[i], nullptr);
		vkDestroyFence(bz::device, bz::inFlightFences[i], nullptr);
	}

	vkDestroyCommandPool(bz::device, bz::commandPool, nullptr);
	vkDestroyDevice(bz::device, nullptr);

#if _DEBUG
	vkDestroyDebugUtilsMessengerEXT(bz::instance, bz::debugMessenger, nullptr);
#endif

	vkDestroySurfaceKHR(bz::instance, bz::surface, nullptr);
	vkDestroyInstance(bz::instance, nullptr);
}

void UpdateUniformBuffer(u32 currentImage) {
	static auto startTime = std::chrono::high_resolution_clock::now();
	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	UniformBufferObject ubo = {
		.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		.proj = glm::perspective(glm::radians(45.0f), bz::swapchainExtent.width / (float)bz::swapchainExtent.height, 0.1f, 10.0f)
	};

	ubo.proj[1][1] *= -1; // flip scaling factor of Y axis to adhere to vulkan.

	memcpy(bz::uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void DrawFrame() {
	VkCheck(vkWaitForFences(bz::device, 1, &bz::inFlightFences[currentFrame], VK_TRUE, UINT64_MAX), "Wait for inFlight fence failed");

	u32 imageIndex;
	VkResult result = vkAcquireNextImageKHR(bz::device, bz::swapchain, UINT64_MAX, bz::imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		RecreateSwapchain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		VkCheck(result, "Failed to acquire swapchain image!");
	}

	UpdateUniformBuffer(currentFrame);

	// Only reset the fence if we swapchain was valid and we are actually submitting work.
	VkCheck(vkResetFences(bz::device, 1, &bz::inFlightFences[currentFrame]), "Failed to reset inFlight fence");

	// This reset happens implicitly on vkBeginCommandBuffer, as it was allocated from a commandPool with VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT set.
	// VkCheck(vkResetCommandBuffer(bz::commandBuffers[currentFrame], 0), "Failed to reset command buffer"); 
	RecordCommandBuffer(bz::commandBuffers[currentFrame], imageIndex);

	VkSemaphore waitSemaphores[] = { bz::imageAvailableSemaphores[currentFrame] };
	VkSemaphore signalSemaphores[] = { bz::renderFinishedSemaphores[currentFrame] };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = waitSemaphores,
		.pWaitDstStageMask = waitStages,
		.commandBufferCount = 1,
		.pCommandBuffers = &bz::commandBuffers[currentFrame],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = signalSemaphores
	};

	VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signalSemaphores,
		.swapchainCount = 1,
		.pSwapchains = &bz::swapchain,
		.pImageIndices = &imageIndex
	};

	VkCheck(vkQueueSubmit(bz::queue, 1, &submitInfo, bz::inFlightFences[currentFrame]), "Failed to submit draw command buffer");
	result = vkQueuePresentKHR(bz::queue, &presentInfo);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || bz::framebufferResized) {
		bz::framebufferResized = false;
		RecreateSwapchain();
	}
	else if (result != VK_SUCCESS) {
		VkCheck(result, "Failed to present swapchain image");
	}

	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

int main(int argc, char* argv[]) {
	InitWindow(WIDTH, HEIGHT);
	InitVulkan();

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		DrawFrame();
	}

	// Wait untill all commandbuffers are done so we can safely clean up semaphores they might potentially be using.
	vkDeviceWaitIdle(bz::device);

	CleanupVulkan();
	CleanupWindow();

	return 0;
}