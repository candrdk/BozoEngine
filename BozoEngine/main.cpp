#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>

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
#include <glm/glm.hpp>

#include "Logging.h"

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

struct Vertex {
	glm::vec2 pos;
	glm::vec3 color;
	
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
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(Vertex, pos)
			},
			{	// Color attribute
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, color)
			}
		};

		return attributeDescriptions;
	}
};

const std::vector<Vertex> vertices = {
	{{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
	{{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
	{{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
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
	VkPipelineLayout pipelineLayout;
	VkPipeline graphicsPipeline;

	VkCommandPool commandPool;
	VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];

	VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];

	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;

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

	for (u32 i = 0; i < queueCount; i++) {
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
		.width  = CLAMP((u32)width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		.height = CLAMP((u32)height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
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

VkShaderModule CreateShaderModule(const char* path) {
	FILE* fp = fopen(path, "rb");
	Check(fp != nullptr, "Failed open shader file.");

	fseek(fp, 0, SEEK_END);
	long length = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	Check(length > 0, "Shader file was empty.");

	char* buffer = new char[length];
	Check(buffer, "Failed to allocate buffer.");

	size_t read = fread(buffer, 1, length, fp);
	Check(read == length, "Failed to read all contents of shader file.");
	fclose(fp);

	VkShaderModuleCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = read,
		.pCode = (u32*)buffer
	};

	VkShaderModule shaderModule;
	VkCheck(vkCreateShaderModule(bz::device, &createInfo, nullptr, &shaderModule), "Failed to create shader module.");

	delete[] buffer;

	return shaderModule;
}

void CreateRenderPass() {
	VkAttachmentDescription colorAttachment = {
		.format = bz::swapchainImageFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,			// dont care. Note that contents wont be preserved. Ok, since loadOp = clear
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	};

	VkAttachmentReference colorAttachmentRef = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentRef
	};

	// Subpass dependency to synchronize our renderpass w/ the acquisition of swapchain images
	// This could alternatively have been accomplished by waitStages if of imageAvailableSemaphore to VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT.
	VkSubpassDependency dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,		// implicit subpass before the renderpass
		.dstSubpass = 0,						// our subpass
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	// wait until the color ouput stage (swapchain has finished reading)
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	// operations that should wait on this are in the color output stage
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT			// operations that should wait on this are writing to the framebuffer
	};

	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &colorAttachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &dependency
	};

	VkCheck(vkCreateRenderPass(bz::device, &renderPassInfo, nullptr, &bz::renderPass), "Failed to create render pass.");
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
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisampeInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE
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
	};

	VkCheck(vkCreatePipelineLayout(bz::device, &pipelineLayoutInfo, nullptr, &bz::pipelineLayout), "Failed to create pipeline layout.");

	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = shaderStages,
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssemblyInfo,
		.pViewportState = &viewportStateInfo,
		.pRasterizationState = &rasterizationInfo,
		.pMultisampleState = &multisampeInfo,
		.pDepthStencilState = nullptr,
		.pColorBlendState = &colorBlendStateInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = bz::pipelineLayout,
		.renderPass = bz::renderPass,
		.subpass = 0
	};

	VkCheck(vkCreateGraphicsPipelines(bz::device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bz::graphicsPipeline), "Failed to create graphics pipeline.");

	vkDestroyShaderModule(bz::device, vertShaderModule, nullptr);
	vkDestroyShaderModule(bz::device, fragShaderModule, nullptr);
}

void CreateFramebuffers() {
	bz::swapchainFramebuffers.resize(bz::swapchainImageViews.size());

	for (int i = 0; i < bz::swapchainImageViews.size(); i++) {
		VkImageView attachments[] = {
			bz::swapchainImageViews[i]
		};

		VkFramebufferCreateInfo framebufferInfo = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = bz::renderPass,
			.attachmentCount = 1,
			.pAttachments = attachments,
			.width = bz::swapchainExtent.width,
			.height = bz::swapchainExtent.height,
			.layers = 1
		};

		VkCheck(vkCreateFramebuffer(bz::device, &framebufferInfo, nullptr, &bz::swapchainFramebuffers[i]), "Failed to create framebuffer.");
	}
}

void CreateCommandPool() {
	VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = GetQueueFamily()
	};

	VkCheck(vkCreateCommandPool(bz::device, &poolInfo, nullptr, &bz::commandPool), "Failed to create command pool.");
}

u32 FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(bz::physicalDevice, &memProperties);

	for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	Check(false, "Failed to find suitable memory type.");
}

void CreateVertexBuffer() {
	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(vertices[0]) * vertices.size(),
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VkCheck(vkCreateBuffer(bz::device, &bufferInfo, nullptr, &bz::vertexBuffer), "Failed to create vertex buffer");

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(bz::device, bz::vertexBuffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	};

	VkCheck(vkAllocateMemory(bz::device, &allocInfo, nullptr, &bz::vertexBufferMemory), "Failed to allocate vertex buffer memory.");
	VkCheck(vkBindBufferMemory(bz::device, bz::vertexBuffer, bz::vertexBufferMemory, 0), "Failed to bind vertex buffer memory to VkBuffer.");

	void* data;
	VkCheck(vkMapMemory(bz::device, bz::vertexBufferMemory, 0, bufferInfo.size, 0, &data), "Failed to map memory.");
	memcpy(data, vertices.data(), bufferInfo.size);
	vkUnmapMemory(bz::device, bz::vertexBufferMemory);
}

void CreateCommandBuffers() {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = bz::commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = sizeof(bz::commandBuffers) / sizeof(bz::commandBuffers[0])
	};

	VkCheck(vkAllocateCommandBuffers(bz::device, &allocInfo, bz::commandBuffers), "Failed to allocate command buffers.");
}

void RecordCommandBuffer(VkCommandBuffer commandBuffer, u32 imageIndex) {
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	VkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin recording command buffer!");

	VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f}} };
	VkRenderPassBeginInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = bz::renderPass,
		.framebuffer = bz::swapchainFramebuffers[imageIndex],
		.renderArea = {
			.offset = {0, 0},
			.extent = bz::swapchainExtent
		},
		.clearValueCount = 1,
		.pClearValues = &clearColor
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

	vkCmdDraw(commandBuffer, (u32)vertices.size(), 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);

	VkCheck(vkEndCommandBuffer(commandBuffer), "Failed to record command buffer.");
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
		VkCheck(vkCreateSemaphore(bz::device, &semaphoreInfo, nullptr, &bz::imageAvailableSemaphores[i]), "Failed to create imageAvailable semaphore.");
		VkCheck(vkCreateSemaphore(bz::device, &semaphoreInfo, nullptr, &bz::renderFinishedSemaphores[i]), "Failed to create renderFinished semaphore.");
		VkCheck(vkCreateFence(bz::device, &fenceInfo, nullptr, &bz::inFlightFences[i]), "Failed to create inFlight fence.");
	}
}

void InitVulkan() {
	VkCheck(volkInitialize(), "Failed to initialzie volk.");

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
	CreateGraphicsPipeline();
	CreateFramebuffers();

	CreateCommandPool();
	CreateVertexBuffer();
	CreateCommandBuffers();

	CreateSyncObjects();
}

void CleanupSwapchain() {
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
	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(bz::device);

	CleanupSwapchain();

	CreateSwapchain();
	CreateImageViews();
	CreateFramebuffers();
}

void CleanupVulkan() {
	CleanupSwapchain();

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

void DrawFrame() {
	VkCheck(vkWaitForFences(bz::device, 1, &bz::inFlightFences[currentFrame], VK_TRUE, UINT64_MAX), "Wait for inFlight fence failed.");

	u32 imageIndex;
	VkResult result = vkAcquireNextImageKHR(bz::device, bz::swapchain, UINT64_MAX, bz::imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		RecreateSwapchain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		VkCheck(result, "Failed to acquire swapchain image!");
	}

	// Only reset the fence if we swapchain was valid and we are actually submitting work.
	VkCheck(vkResetFences(bz::device, 1, &bz::inFlightFences[currentFrame]), "Failed to reset inFlight fence.");

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

	VkCheck(vkQueueSubmit(bz::queue, 1, &submitInfo, bz::inFlightFences[currentFrame]), "Failed to submit draw command buffer.");
	result = vkQueuePresentKHR(bz::queue, &presentInfo);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || bz::framebufferResized) {
		bz::framebufferResized = false;
		RecreateSwapchain();
	}
	else if (result != VK_SUCCESS) {
		VkCheck(result, "Failed to present swapchain image.");
	}

	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

int main(int argc, char* argv[]) {
	InitWindow(800, 600);
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