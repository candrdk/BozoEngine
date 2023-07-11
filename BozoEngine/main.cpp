#include "Common.h"

#include <backends/imgui_impl_glfw.h>

#include <glm/gtc/matrix_transform.hpp>		// glm::rotate

#include "Device.h"
#include "Swapchain.h"
#include "Texture.h"
#include "Camera.h"

#include "GLTF.h"
#include "UIOverlay.h"

constexpr u32 WIDTH = 800;
constexpr u32 HEIGHT = 600;

GLTFModel* flightHelmet;

struct UniformBufferObject {
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

u32 currentFrame = 0;

// Temporary namespace to contain globals
namespace bz {
	Camera camera(glm::vec3(0.0f, 0.0f, 0.5f), 1.0f, 90.0f, (float)WIDTH / HEIGHT, 0.01f, 0.0f, -90.0f);

	Device device;
	Swapchain swapchain;

	UIOverlay Overlay;

	// Wrap these
	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;

	VkImage colorImage;
	VkDeviceMemory colorImageMemory;
	VkImageView colorImageView;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout uboLayout, texturesLayout;
	VkDescriptorSet uboDescriptorSets[MAX_FRAMES_IN_FLIGHT];

	VkPipelineLayout pipelineLayout;
	VkPipeline graphicsPipeline;

	// Maybe wrap all of these in a RenderFrame struct?
	// Every renderframe should have its own comandbuffer pool, which is reset every frame.
	VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];

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
	case GLFW_KEY_LEFT_SHIFT:
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
	VkCheck(vkCreateImageView(bz::device.logicalDevice, &viewInfo, nullptr, &imageView), "Failed to create image view");

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
	VkCheck(vkCreateShaderModule(bz::device.logicalDevice, &createInfo, nullptr, &shaderModule), "Failed to create shader module");

	delete[] buffer;

	return shaderModule;
}

VkPipelineShaderStageCreateInfo LoadShader(const char* path, VkShaderStageFlagBits stage) {
	return {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = stage,
		.module = CreateShaderModule(path),
		.pName = "main",
		.pSpecializationInfo = 0
	};
}

void CreateGraphicsPipeline() {
	VkPipelineShaderStageCreateInfo shaderStages[] = {
		LoadShader("shaders/triangle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
		LoadShader("shaders/triangle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
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

	VkVertexInputBindingDescription bindingDescription = GLTFModel::Vertex::GetBindingDescription();
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions = GLTFModel::Vertex::GetAttributeDescriptions();
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
		.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL, // inverse z
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

	VkPipelineRenderingCreateInfo renderingCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &bz::swapchain.format,
		.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT
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

	VkCheck(vkCreateGraphicsPipelines(bz::device.logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bz::graphicsPipeline), "Failed to create graphics pipeline");

	// if we rebuild this pipeline often, we should retain these shader modules.
	vkDestroyShaderModule(bz::device.logicalDevice, shaderStages[0].module, nullptr);
	vkDestroyShaderModule(bz::device.logicalDevice, shaderStages[1].module, nullptr);
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

	VkCheck(vkCreateImage(bz::device.logicalDevice, &imageInfo, nullptr, &image), "Failed to create image");

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(bz::device.logicalDevice, image, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
	};

	VkCheck(vkAllocateMemory(bz::device.logicalDevice, &allocInfo, nullptr, &imageMemory), "Failed to allocate image memory");
	VkCheck(vkBindImageMemory(bz::device.logicalDevice, image, imageMemory, 0), "Failed to bind VkDeviceMemory to VkImage");
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

// TODO: should query supported formats and select from them.
void CreateDepthResources() {
	CreateImage(bz::swapchain.extent.width, bz::swapchain.extent.height, 1, bz::msaaSamples,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		bz::depthImage, bz::depthImageMemory);
	bz::depthImageView = CreateImageView(bz::depthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
	
	VkCommandBuffer cmdBuffer = bz::device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	SetImageLayout(cmdBuffer, bz::depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, 
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);	// TODO: check that these stages are appropriate (prop doesn't matter in this case, but still nice to have right)
	bz::device.FlushCommandBuffer(cmdBuffer, bz::device.graphicsQueue);
}

void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VkCheck(vkCreateBuffer(bz::device.logicalDevice, &bufferInfo, nullptr, &buffer), "Failed to create buffer");

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(bz::device.logicalDevice, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties)
	};

	VkCheck(vkAllocateMemory(bz::device.logicalDevice, &allocInfo, nullptr, &bufferMemory), "Failed to allocate vertex buffer memory");
	VkCheck(vkBindBufferMemory(bz::device.logicalDevice, buffer, bufferMemory, 0), "Failed to bind DeviceMemory to VkBuffer");
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
	VkCheck(vkAllocateCommandBuffers(bz::device.logicalDevice, &allocInfo, &commandBuffer), "Failed to allocate command buffer");

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

	vkFreeCommandBuffers(bz::device.logicalDevice, bz::device.commandPool, 1, &commandBuffer);
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

void CreateUniformBuffers() {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		CreateBuffer(bufferSize, 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
			bz::uniformBuffers[i], bz::uniformBuffersMemory[i]);

		VkCheck(vkMapMemory(bz::device.logicalDevice, bz::uniformBuffersMemory[i], 0, bufferSize, 0, &bz::uniformBuffersMapped[i]), "Failed to map memory");
	}
}

void CreateDescriptorPool() {
	VkDescriptorPoolSize poolSizes[2] = {
		{ // uniform buffer descriptor pool
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = MAX_FRAMES_IN_FLIGHT	// TODO: try setting this to 1
		},
		{ // combined image sampler descriptor pool per model image / texture
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = (u32)flightHelmet->images.size()
		}
	};

	// One set for matrices and one per model image/texture
	const u32 maxSetCount = (u32)flightHelmet->images.size() + MAX_FRAMES_IN_FLIGHT;
	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = maxSetCount,
		.poolSizeCount = arraysize(poolSizes),
		.pPoolSizes = poolSizes
	};

	VkCheck(vkCreateDescriptorPool(bz::device.logicalDevice, &poolInfo, nullptr, &bz::descriptorPool), "Failed to create descriptor pool");
}

void CreateDescriptorSets() {
	// Descriptor set layout for passing matrices
	VkDescriptorSetLayoutBinding setLayoutBinding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &setLayoutBinding
	};
	VkCheck(vkCreateDescriptorSetLayout(bz::device.logicalDevice, &descriptorSetLayoutInfo, nullptr, &bz::uboLayout), "Failed to create descriptor set layout for the matrix UBO");

	// Descriptor set layout for passing material textures
	setLayoutBinding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.pImmutableSamplers = nullptr
	};
	VkCheck(vkCreateDescriptorSetLayout(bz::device.logicalDevice, &descriptorSetLayoutInfo, nullptr, &bz::texturesLayout), "Failed to create descriptor set layout for textures");

	// Push constants are used to push local matrices of a primitive to the vertex shader
	VkPushConstantRange pushConstantRange = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(glm::mat4)
	};

	// Pipeline layout using both descriptor sets (set 0 = matrices, set 1 = material)
	VkDescriptorSetLayout setLayouts[2] = { bz::uboLayout, bz::texturesLayout };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = arraysize(setLayouts),
		.pSetLayouts = setLayouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
	};

	VkCheck(vkCreatePipelineLayout(bz::device.logicalDevice, &pipelineLayoutInfo, nullptr, &bz::pipelineLayout), "Failed to create pipeline layout");

	// Descriptor set for scene matrices
	setLayouts[1] = bz::uboLayout;	// reuse setLayouts, to allocate two descriptors at once (setLayouts = { uboLayout, uboLayot })
	VkDescriptorSetAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = bz::descriptorPool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT, // TODO also set to 1 here
		.pSetLayouts = setLayouts
	};
	VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, bz::uboDescriptorSets), "Failed to allocate UBO descriptor sets");

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkDescriptorBufferInfo bufferInfo = {
			.buffer = bz::uniformBuffers[i],
			.offset = 0,
			.range = sizeof(UniformBufferObject)
		};

		VkWriteDescriptorSet writeDescriptorSet = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bz::uboDescriptorSets[i],
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &bufferInfo
		};

		vkUpdateDescriptorSets(bz::device.logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}

	// Create descriptor sets for materials
	allocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = bz::descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &bz::texturesLayout
	};

	for (auto& image : flightHelmet->images) {
		VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, &image.descriptorSet), "Failed to allocate descriptor set for image");
		VkWriteDescriptorSet writeDescriptorSet = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = image.descriptorSet,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &image.texture.descriptor
		};
		vkUpdateDescriptorSets(bz::device.logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}
}

void CreateCommandBuffers() {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = bz::device.commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = arraysize(bz::commandBuffers)
	};

	VkCheck(vkAllocateCommandBuffers(bz::device.logicalDevice, &allocInfo, bz::commandBuffers), "Failed to allocate command buffers");
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
			.depthStencil = { 0.0f, 0 },
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

	InsertImageBarrier(commandBuffer, bz::swapchain.images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, 
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	
	vkCmdBeginRendering(commandBuffer, &renderingInfo);

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

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::pipelineLayout, 0, 1, &bz::uboDescriptorSets[currentFrame], 0, nullptr);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::graphicsPipeline);

	flightHelmet->Draw(commandBuffer, bz::pipelineLayout);

	bz::Overlay.Draw(commandBuffer);

	vkCmdEndRendering(commandBuffer);

	InsertImageBarrier(commandBuffer, bz::swapchain.images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
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
		VkCheck(vkCreateSemaphore(bz::device.logicalDevice, &semaphoreInfo, nullptr, &bz::imageAvailableSemaphores[i]), "Failed to create imageAvailable semaphore");
		VkCheck(vkCreateSemaphore(bz::device.logicalDevice, &semaphoreInfo, nullptr, &bz::renderFinishedSemaphores[i]), "Failed to create renderFinished semaphore");
		VkCheck(vkCreateFence(bz::device.logicalDevice, &fenceInfo, nullptr, &bz::inFlightFences[i]), "Failed to create inFlight fence");
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

	flightHelmet = new GLTFModel(bz::device);
	flightHelmet->LoadGLTFFile("assets/FlightHelmet/FlightHelmet.gltf");

	CreateUniformBuffers();

	CreateDescriptorPool();
	CreateDescriptorSets();

	CreateGraphicsPipeline();

	CreateColorResources();
	CreateDepthResources();

	CreateCommandBuffers();
	CreateSyncObjects();
}

void CleanupSwapchain() {
	vkDestroyImageView(bz::device.logicalDevice, bz::colorImageView, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bz::colorImage, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bz::colorImageMemory, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bz::depthImageView, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bz::depthImage, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bz::depthImageMemory, nullptr);

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

	vkDeviceWaitIdle(bz::device.logicalDevice);

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

	delete flightHelmet;

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroyBuffer(bz::device.logicalDevice, bz::uniformBuffers[i], nullptr);
		vkFreeMemory(bz::device.logicalDevice, bz::uniformBuffersMemory[i], nullptr);
	}

	vkDestroyDescriptorPool(bz::device.logicalDevice, bz::descriptorPool, nullptr);

	vkDestroyPipeline(bz::device.logicalDevice, bz::graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(bz::device.logicalDevice, bz::pipelineLayout, nullptr);

	vkDestroyDescriptorSetLayout(bz::device.logicalDevice, bz::uboLayout, nullptr);
	vkDestroyDescriptorSetLayout(bz::device.logicalDevice, bz::texturesLayout, nullptr);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(bz::device.logicalDevice, bz::imageAvailableSemaphores[i], nullptr);
		vkDestroySemaphore(bz::device.logicalDevice, bz::renderFinishedSemaphores[i], nullptr);
		vkDestroyFence(bz::device.logicalDevice, bz::inFlightFences[i], nullptr);
	}

	bz::device.DestroyDevice();
}

void UpdateUniformBuffer(u32 currentImage) {
	UniformBufferObject ubo = {
		.view = bz::camera.view,
		.proj = bz::camera.projection
	};

	memcpy(bz::uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void DrawFrame() {
	VkCheck(vkWaitForFences(bz::device.logicalDevice, 1, &bz::inFlightFences[currentFrame], VK_TRUE, UINT64_MAX), "Wait for inFlight fence failed");

	u32 imageIndex;
	VkResult result = vkAcquireNextImageKHR(bz::device.logicalDevice, bz::swapchain.swapchain, UINT64_MAX, bz::imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		RecreateSwapchain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		VkCheck(result, "Failed to acquire swapchain image!");
	}

	UpdateUniformBuffer(currentFrame);

	// Only reset the fence if we swapchain was valid and we are actually submitting work.
	VkCheck(vkResetFences(bz::device.logicalDevice, 1, &bz::inFlightFences[currentFrame]), "Failed to reset inFlight fence");

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

void UpdateImGuiFrame() {
	ImGui_ImplGlfw_NewFrame();

	ImGui::NewFrame();
	ImGui::ShowDemoWindow();
	ImGui::Render();

	bz::Overlay.Update(bz::device);
}

int main(int argc, char* argv[]) {
	InitWindow(WIDTH, HEIGHT);
	InitVulkan();

	ImGui_ImplGlfw_InitForVulkan(window, true);
	bz::Overlay.vertShader = LoadShader("shaders/uioverlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	bz::Overlay.fragShader = LoadShader("shaders/uioverlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	bz::Overlay.Initialize(bz::device, bz::msaaSamples, bz::swapchain.format, VK_FORMAT_D32_SFLOAT);

	double lastFrame = 0.0f;

	while (!glfwWindowShouldClose(window)) {
		double currentFrame = glfwGetTime();
		float deltaTime = float(currentFrame - lastFrame);
		lastFrame = currentFrame;

		bz::camera.Update(deltaTime);

		UpdateImGuiFrame();
		
		DrawFrame();

		glfwPollEvents();
	}

	// Wait until all commandbuffers are done so we can safely clean up semaphores they might potentially be using.
	vkDeviceWaitIdle(bz::device.logicalDevice);

	bz::Overlay.Free(bz::device);

	CleanupVulkan();
	CleanupWindow();

	return 0;
}