// Stuff TODO in the future: 
//	- At some point VMA should be integrated instead of making individual allocations for every buffer.
//	- Read up on driver developer recommendations (fx. suballocating vertex/index buffers inside the same VkBuffer)
//	- Add meshoptimizer
//	- Implement loading multiple mip levels from a file instead of creating them at runtime.

#include "Common.h"

#include <chrono> // ugh

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <glm/gtc/matrix_transform.hpp>		// glm::rotate

#pragma warning(disable : 26451 6262)
	#define FAST_OBJ_IMPLEMENTATION
	#include <fast_obj.h>
#pragma warning(default : 26451 6262)

#include "Logging.h"
#include "Device.h"
#include "Swapchain.h"
#include "Texture.h"
#include "Camera.h"

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
	Camera camera(glm::vec3(1.0f, 1.0f, 1.0f), 1.0f, 90.0f, (float)WIDTH / HEIGHT, 0.1f, 100.0f, -30.0f, -135.0f);

	Device device;
	Swapchain swapchain;

	// Wrap these
	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;

	VkImage colorImage;
	VkDeviceMemory colorImageMemory;
	VkImageView colorImageView;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSets[MAX_FRAMES_IN_FLIGHT];

	VkPipelineLayout pipelineLayout;
	VkPipeline graphicsPipeline;

	VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];

	VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];

	// Wrap these into Model/Mesh classes //
	Texture texture;

	std::vector<Vertex> vertices;
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;

	std::vector<u32> indices;
	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;
	////////////////////////////////////////

	VkBuffer uniformBuffers[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory uniformBuffersMemory[MAX_FRAMES_IN_FLIGHT];
	void* uniformBuffersMapped[MAX_FRAMES_IN_FLIGHT];

	VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

	bool framebufferResized = false;
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
	bz::framebufferResized = true;
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	switch (key) {
	case GLFW_KEY_ESCAPE:
		glfwSetWindowShouldClose(window, true);
		break;

	case GLFW_KEY_SPACE:
	case GLFW_KEY_LEFT_CONTROL:
	case GLFW_KEY_W:
	case GLFW_KEY_A:
	case GLFW_KEY_S:
	case GLFW_KEY_D:
		bz::camera.ProcessKeyboard(key, action);
		break;
	}
}

double lastXpos = WIDTH / 2.0f;
double lastYpos = WIDTH / 2.0f;
void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
	if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
		return;

	double xoffset = (xpos - lastXpos);
	double yoffset = (lastYpos - ypos);
	
	lastXpos = xpos;
	lastYpos = ypos;

	bz::camera.ProcessMouseMovement(xoffset, yoffset);
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if (button == GLFW_MOUSE_BUTTON_RIGHT) {
		glfwSetInputMode(window, GLFW_CURSOR, action == GLFW_PRESS ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		glfwGetCursorPos(window, &lastXpos, &lastYpos);
	}
}

// Initialize glfw and create a window of width/height
GLFWwindow* window;
void InitWindow(int width, int height) {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

	window = glfwCreateWindow(width, height, "BozoEngine", nullptr, nullptr);

	glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

	glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
	glfwSetKeyCallback(window, KeyCallback);
	glfwSetCursorPosCallback(window, CursorPosCallback);
	glfwSetMouseButtonCallback(window, MouseButtonCallback);
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
	VkCheck(vkCreateImageView(bz::device.device, &viewInfo, nullptr, &imageView), "Failed to create image view");

	return imageView;
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
	VkCheck(vkCreateShaderModule(bz::device.device, &createInfo, nullptr, &shaderModule), "Failed to create shader module");

	delete[] buffer;

	return shaderModule;
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
		.bindingCount = arraysize(bindings),
		.pBindings = bindings
	};

	VkCheck(vkCreateDescriptorSetLayout(bz::device.device, &layoutInfo, nullptr, &bz::descriptorSetLayout), "Failed to create a descriptor set layout");
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
		.dynamicStateCount = arraysize(dynamicState),
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
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
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

	VkCheck(vkCreatePipelineLayout(bz::device.device, &pipelineLayoutInfo, nullptr, &bz::pipelineLayout), "Failed to create pipeline layout");

	VkPipelineRenderingCreateInfo renderingCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &bz::swapchain.format,
		.depthAttachmentFormat = VK_FORMAT_D24_UNORM_S8_UINT
	};

	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingCreateInfo,
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
		.renderPass = VK_NULL_HANDLE,
		.subpass = 0
	};

	VkCheck(vkCreateGraphicsPipelines(bz::device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bz::graphicsPipeline), "Failed to create graphics pipeline");

	vkDestroyShaderModule(bz::device.device, vertShaderModule, nullptr);
	vkDestroyShaderModule(bz::device.device, fragShaderModule, nullptr);
}

u32 FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(bz::device.physicalDevice, &memProperties);

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

	VkCheck(vkCreateImage(bz::device.device, &imageInfo, nullptr, &image), "Failed to create image");

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(bz::device.device, image, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
	};

	VkCheck(vkAllocateMemory(bz::device.device, &allocInfo, nullptr, &imageMemory), "Failed to allocate image memory");
	VkCheck(vkBindImageMemory(bz::device.device, image, imageMemory, 0), "Failed to bind VkDeviceMemory to VkImage");
}

void CreateColorResources() {
	VkFormat colorFormat = bz::swapchain.format;

	CreateImage(bz::swapchain.extent.width, bz::swapchain.extent.height, 1, bz::msaaSamples, colorFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		bz::colorImage, bz::colorImageMemory);
	bz::colorImageView = CreateImageView(bz::colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void CreateDepthResources() {
	// TODO: should query supported formats and select from them.

	CreateImage(bz::swapchain.extent.width, bz::swapchain.extent.height, 1, bz::msaaSamples,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		bz::depthImage, bz::depthImageMemory);
	bz::depthImageView = CreateImageView(bz::depthImage, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
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
		.poolSizeCount = arraysize(poolSizes),
		.pPoolSizes = poolSizes
	};

	VkCheck(vkCreateDescriptorPool(bz::device.device, &poolInfo, nullptr, &bz::descriptorPool), "Failed to create descriptor pool");
}

void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VkCheck(vkCreateBuffer(bz::device.device, &bufferInfo, nullptr, &buffer), "Failed to create buffer");

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(bz::device.device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
	};

	VkCheck(vkAllocateMemory(bz::device.device, &allocInfo, nullptr, &bufferMemory), "Failed to allocate vertex buffer memory");
	VkCheck(vkBindBufferMemory(bz::device.device, buffer, bufferMemory, 0), "Failed to bind DeviceMemory to VkBuffer");
}

// TODO: should allocate a separate command pool for these kinds of short-lived buffers.
// When we do, use the VK_COMMAND_POOL_CREATE_TRANSIENT_BIT flag during command pool generation.
VkCommandBuffer BeginSingleTimeCommands() {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = bz::device.commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer commandBuffer;
	VkCheck(vkAllocateCommandBuffers(bz::device.device, &allocInfo, &commandBuffer), "Failed to allocate command buffer");

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

	VkCheck(vkQueueSubmit(bz::device.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit command buffer to queue");
	VkCheck(vkQueueWaitIdle(bz::device.graphicsQueue), "QueueWaitIdle failed");

	vkFreeCommandBuffers(bz::device.device, bz::device.commandPool, 1, &commandBuffer);
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

void LoadModel() {
	fastObjMesh* mesh = fast_obj_read(MODEL_PATH);
	Check(mesh != nullptr, "Failed to load obj file: `%s`", MODEL_PATH);

	bz::vertices.reserve(mesh->index_count);
	bz::indices.reserve(mesh->index_count);
	for (u32 i = 0; i < mesh->index_count; i++) {
		fastObjIndex index = mesh->indices[i];

		// Model has z pointing upwards, so we swap y and z
		// This means we also have to fix up indices:
		if ((i + 1) % 3 == 0) {
			bz::indices[i - 1] = i;
			bz::indices.push_back(i - 1);
		}
		else {
			bz::indices.push_back(i);
		}

		bz::vertices.push_back({
			.pos = {
				mesh->positions[3 * index.p + 0],
				mesh->positions[3 * index.p + 2], // model has z pointing upwards,
				mesh->positions[3 * index.p + 1]  // so we swap y and z here
			},
			.color = { 1.0f, 1.0f, 1.0f },
			.texCoord = {
				mesh->texcoords[2 * index.t + 0],
				mesh->texcoords[2 * index.t + 1]
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
	VkCheck(vkMapMemory(bz::device.device, stagingBufferMemory, 0, bufferSize, 0, &data), "Failed to map memory");
	memcpy(data, bz::vertices.data(), bufferSize);
	vkUnmapMemory(bz::device.device, stagingBufferMemory);

	CreateBuffer(bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
		bz::vertexBuffer, bz::vertexBufferMemory);

	CopyBuffer(stagingBuffer, bz::vertexBuffer, bufferSize);

	vkDestroyBuffer(bz::device.device, stagingBuffer, nullptr);
	vkFreeMemory(bz::device.device, stagingBufferMemory, nullptr);
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
	VkCheck(vkMapMemory(bz::device.device, stagingBufferMemory, 0, bufferSize, 0, &data), "Failed to map memory");
	memcpy(data, bz::indices.data(), bufferSize);
	vkUnmapMemory(bz::device.device, stagingBufferMemory);

	CreateBuffer(bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
		bz::indexBuffer, bz::indexBufferMemory);

	CopyBuffer(stagingBuffer, bz::indexBuffer, bufferSize);

	vkDestroyBuffer(bz::device.device, stagingBuffer, nullptr);
	vkFreeMemory(bz::device.device, stagingBufferMemory, nullptr);
}

void CreateUniformBuffers() {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		CreateBuffer(bufferSize, 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			bz::uniformBuffers[i], bz::uniformBuffersMemory[i]);

		VkCheck(vkMapMemory(bz::device.device, bz::uniformBuffersMemory[i], 0, bufferSize, 0, &bz::uniformBuffersMapped[i]), "Failed to map memory");
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

	VkCheck(vkAllocateDescriptorSets(bz::device.device, &allocInfo, bz::descriptorSets), "Failed to allocate descriptor sets");

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkDescriptorBufferInfo bufferInfo = {
			.buffer = bz::uniformBuffers[i],
			.offset = 0,
			.range = sizeof(UniformBufferObject)
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
				.pImageInfo = &bz::texture.descriptor
			}
		};

		vkUpdateDescriptorSets(bz::device.device, arraysize(descriptorWrites), descriptorWrites, 0, nullptr);
	}
}

void CreateCommandBuffers() {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = bz::device.commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = arraysize(bz::commandBuffers)
	};

	VkCheck(vkAllocateCommandBuffers(bz::device.device, &allocInfo, bz::commandBuffers), "Failed to allocate command buffers");
}

void InsertImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask) {
	VkImageSubresourceRange subresourceRange = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.layerCount = 1
	};

	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = srcAccessMask,
		.dstAccessMask = dstAccessMask,
		.oldLayout = oldLayout, //VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = subresourceRange
	};

	vkCmdPipelineBarrier(cmdBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void RecordCommandBuffer(VkCommandBuffer commandBuffer, u32 imageIndex) {
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	VkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin recording command buffer!");

	VkRenderingAttachmentInfo colorAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = bz::colorImageView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
		.resolveImageView = bz::swapchain.imageViews[imageIndex],
		.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = {
			.color = { 0.0f, 0.0f, 0.0f },
		}
	};

	VkRenderingAttachmentInfo depthAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = bz::depthImageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.resolveMode = VK_RESOLVE_MODE_NONE,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.clearValue = {
			.depthStencil = { 1.0f, 0 },
		}
	};

	VkRenderingInfo renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
			.offset = {0, 0},
			.extent = bz::swapchain.extent
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
		.pDepthAttachment = &depthAttachment,
		.pStencilAttachment = nullptr
	};

	InsertImageBarrier(commandBuffer, bz::swapchain.images[imageIndex], 
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
		VK_IMAGE_LAYOUT_UNDEFINED, 
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	
	vkCmdBeginRendering(commandBuffer, &renderingInfo);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::graphicsPipeline);

	VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)bz::swapchain.extent.width,
		.height = (float)bz::swapchain.extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = bz::swapchain.extent
	};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkBuffer vertexBuffers[] = { bz::vertexBuffer };
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, bz::indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::pipelineLayout, 0, 1, &bz::descriptorSets[currentFrame], 0, nullptr);

	vkCmdDrawIndexed(commandBuffer, (u32)bz::indices.size(), 1, 0, 0, 0);

#if 0	// TODO: do this ourselves
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
#endif

	vkCmdEndRendering(commandBuffer);

	InsertImageBarrier(commandBuffer, bz::swapchain.images[imageIndex],
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

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
		VkCheck(vkCreateSemaphore(bz::device.device, &semaphoreInfo, nullptr, &bz::imageAvailableSemaphores[i]), "Failed to create imageAvailable semaphore");
		VkCheck(vkCreateSemaphore(bz::device.device, &semaphoreInfo, nullptr, &bz::renderFinishedSemaphores[i]), "Failed to create renderFinished semaphore");
		VkCheck(vkCreateFence(bz::device.device, &fenceInfo, nullptr, &bz::inFlightFences[i]), "Failed to create inFlight fence");
	}
}

void InitVulkan() {
	bz::device.CreateDevice(window);
	bz::msaaSamples = bz::device.GetMaxUsableSampleCount();
	bz::swapchain.CreateSwapchain(window, bz::device, {
		.enableVSync = true, 
		.prefferedImageCount = 2, 
		.oldSwapchain = VK_NULL_HANDLE 
	});

	CreateDescriptorSetLayout();
	CreateGraphicsPipeline();

	CreateColorResources();
	CreateDepthResources();

	CreateDescriptorPool();

	bz::texture.LoadFromFile(TEXTURE_PATH, bz::device, bz::device.graphicsQueue, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	LoadModel();

	CreateVertexBuffer();
	CreateIndexBuffer();
	CreateUniformBuffers();

	CreateDescriptorSets();
	CreateCommandBuffers();
	CreateSyncObjects();
}

void CleanupSwapchain() {
	vkDestroyImageView(bz::device.device, bz::colorImageView, nullptr);
	vkDestroyImage(bz::device.device, bz::colorImage, nullptr);
	vkFreeMemory(bz::device.device, bz::colorImageMemory, nullptr);

	vkDestroyImageView(bz::device.device, bz::depthImageView, nullptr);
	vkDestroyImage(bz::device.device, bz::depthImage, nullptr);
	vkFreeMemory(bz::device.device, bz::depthImageMemory, nullptr);

	bz::swapchain.DestroySwapchain(bz::device, bz::swapchain);
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

	vkDeviceWaitIdle(bz::device.device);

	CleanupSwapchain();
	bz::swapchain.CreateSwapchain(window, bz::device, { 
		.enableVSync = true, 
		.prefferedImageCount = 2, 
		.oldSwapchain = VK_NULL_HANDLE 
	});

	CreateColorResources();
	CreateDepthResources();
}

void CleanupVulkan() {
	CleanupSwapchain();

	bz::texture.Destroy(bz::device);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroyBuffer(bz::device.device, bz::uniformBuffers[i], nullptr);
		vkFreeMemory(bz::device.device, bz::uniformBuffersMemory[i], nullptr);
	}

	vkDestroyDescriptorPool(bz::device.device, bz::descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(bz::device.device, bz::descriptorSetLayout, nullptr);

	vkDestroyBuffer(bz::device.device, bz::indexBuffer, nullptr);
	vkFreeMemory(bz::device.device, bz::indexBufferMemory, nullptr);

	vkDestroyBuffer(bz::device.device, bz::vertexBuffer, nullptr);
	vkFreeMemory(bz::device.device, bz::vertexBufferMemory, nullptr);

	vkDestroyPipeline(bz::device.device, bz::graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(bz::device.device, bz::pipelineLayout, nullptr);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(bz::device.device, bz::imageAvailableSemaphores[i], nullptr);
		vkDestroySemaphore(bz::device.device, bz::renderFinishedSemaphores[i], nullptr);
		vkDestroyFence(bz::device.device, bz::inFlightFences[i], nullptr);
	}

	bz::device.DestroyDevice();
}

void UpdateUniformBuffer(u32 currentImage) {
	static auto startTime = std::chrono::high_resolution_clock::now();
	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	UniformBufferObject ubo = {
		.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
		.view = bz::camera.view,
		.proj = bz::camera.projection
	};

	memcpy(bz::uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void DrawFrame() {
	VkCheck(vkWaitForFences(bz::device.device, 1, &bz::inFlightFences[currentFrame], VK_TRUE, UINT64_MAX), "Wait for inFlight fence failed");

	u32 imageIndex;
	VkResult result = vkAcquireNextImageKHR(bz::device.device, bz::swapchain.swapchain, UINT64_MAX, bz::imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		RecreateSwapchain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		VkCheck(result, "Failed to acquire swapchain image!");
	}

	UpdateUniformBuffer(currentFrame);

	// Only reset the fence if we swapchain was valid and we are actually submitting work.
	VkCheck(vkResetFences(bz::device.device, 1, &bz::inFlightFences[currentFrame]), "Failed to reset inFlight fence");

	// This reset happens implicitly on vkBeginCommandBuffer, as it was allocated from a commandPool with VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT set.
	VkCheck(vkResetCommandBuffer(bz::commandBuffers[currentFrame], 0), "Failed to reset command buffer"); 
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
		.pSwapchains = &bz::swapchain.swapchain,
		.pImageIndices = &imageIndex
	};

	VkCheck(vkQueueSubmit(bz::device.graphicsQueue, 1, &submitInfo, bz::inFlightFences[currentFrame]), "Failed to submit draw command buffer");
	result = vkQueuePresentKHR(bz::device.graphicsQueue, &presentInfo);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || bz::framebufferResized) {
		bz::framebufferResized = false;
		RecreateSwapchain();
	}
	else if (result != VK_SUCCESS) {
		VkCheck(result, "Failed to present swapchain image");
	}

	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

#if 0 // TODO: do this ourselves
VkDescriptorPool imguiPool;
void InitImGui() {
	// Super oversized, just copied from imgui demo
	VkDescriptorPoolSize poolSizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1000,
		.poolSizeCount = arraysize(poolSizes),
		.pPoolSizes = poolSizes,
	};

	VkCheck(vkCreateDescriptorPool(bz::device.device, &poolInfo, nullptr, &imguiPool), "Failed to create imgui descriptor pool");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForVulkan(window, true);

	ImGui_ImplVulkan_InitInfo initInfo = {
		.Instance = bz::device.instance,
		.PhysicalDevice = bz::device.physicalDevice,
		.Device = bz::device.device,
		.Queue = bz::device.graphicsQueue,
		.DescriptorPool = imguiPool,
		.MinImageCount = MAX_FRAMES_IN_FLIGHT,
		.ImageCount = MAX_FRAMES_IN_FLIGHT,
		.MSAASamples = bz::msaaSamples
	};

	ImGui_ImplVulkan_LoadFunctions([](const char* function_name, void* vulkan_instance) {
		return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkan_instance)), function_name);
		}, &bz::device.instance);
	ImGui_ImplVulkan_Init(&initInfo, bz::renderPass);
	
	VkCommandBuffer buffer = BeginSingleTimeCommands();
	ImGui_ImplVulkan_CreateFontsTexture(buffer);
	EndSingleTimeCommands(buffer);

	ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void CleanupImGui() {
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	vkDestroyDescriptorPool(bz::device.device, imguiPool, nullptr);
}

void UpdateImGuiFrame() {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGui::ShowDemoWindow();
	ImGui::Render();
}
#endif

int main(int argc, char* argv[]) {
	InitWindow(WIDTH, HEIGHT);
	InitVulkan();

#if 0
	InitImGui();
#endif

	double lastFrame = 0.0f;

	while (!glfwWindowShouldClose(window)) {
		double currentFrame = glfwGetTime();
		float deltaTime = float(currentFrame - lastFrame);
		lastFrame = currentFrame;

		bz::camera.Update(deltaTime);

#if 0
		UpdateImGuiFrame();
#endif
		
		DrawFrame();

		glfwPollEvents();
	}

	// Wait until all commandbuffers are done so we can safely clean up semaphores they might potentially be using.
	vkDeviceWaitIdle(bz::device.device);

#if 0
	CleanupImGui();
#endif

	CleanupVulkan();
	CleanupWindow();

	return 0;
}