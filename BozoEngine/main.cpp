#include "Common.h"

#include <backends/imgui_impl_glfw.h>

#include <glm/gtc/matrix_transform.hpp>		// glm::rotate

#include "Device.h"
#include "Swapchain.h"
#include "Texture.h"
#include "Camera.h"

#include "GLTF.h"
#include "UIOverlay.h"

constexpr u32 WIDTH = 1600;
constexpr u32 HEIGHT = 900;
#define DEFERRED

GLTFModel* flightHelmet;

struct UniformBufferObject {
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

u32 currentFrame = 0;

// When moving on recording multiple command buffers, every RenderFrame should
// have its own VkCommandPool. Commandbuffers for a given frame are then allocated
// from this pool, and the entire pool is reset every frame.
struct RenderFrame {
	VkSemaphore imageAvailable;
	VkSemaphore renderFinished;
	VkFence inFlight;

	VkCommandBuffer commandBuffer;
};

struct RenderAttachmentDesc {
	VkExtent2D extent;
	VkFormat format;
	VkImageUsageFlags usage;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
};

struct RenderAttachment {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkFormat format;
	VkRenderingAttachmentInfo attachmentInfo;
};

// Temporary namespace to contain globals
namespace bz {
	Camera camera(glm::vec3(0.0f, 0.0f, 0.5f), 1.0f, 90.0f, (float)WIDTH / HEIGHT, 0.01f, 0.0f, -90.0f);

	Device device;
	Swapchain swapchain;

	UIOverlay Overlay;
	RenderFrame renderFrames[MAX_FRAMES_IN_FLIGHT];
	Buffer uniformBuffers[MAX_FRAMES_IN_FLIGHT];
	RenderAttachment depth, color;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout uboLayout, texturesLayout;			// bozo
	VkDescriptorSet uboDescriptorSets[MAX_FRAMES_IN_FLIGHT];	// bozo

	VkPipelineLayout pipelineLayout;							// bozo
	VkPipeline graphicsPipeline;

	VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

	bool framebufferResized = false;
}

namespace bozo {
	RenderAttachment albedo, normal;
	VkSampler attachmentSampler;

	VkDescriptorSetLayout uboDescriptorSetLayout, descriptorSetLayout;
	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSet uboDescriptorSets[arraysize(bz::uniformBuffers)];

	VkPipeline offscreenPipeline;
	VkPipeline deferredPipeline;
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
		.memoryTypeIndex = bz::device.GetMemoryType(memRequirements.memoryTypeBits, properties)
	};

	VkCheck(vkAllocateMemory(bz::device.logicalDevice, &allocInfo, nullptr, &imageMemory), "Failed to allocate image memory");
	VkCheck(vkBindImageMemory(bz::device.logicalDevice, image, imageMemory, 0), "Failed to bind VkDeviceMemory to VkImage");
}

void CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, u32 mipLevels, VkImageView& imageView) {
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

	VkCheck(vkCreateImageView(bz::device.logicalDevice, &viewInfo, nullptr, &imageView), "Failed to create image view");
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
		.pColorAttachmentFormats = &bz::color.format,
		.depthAttachmentFormat = bz::depth.format
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

void CreateUniformBuffers() {
	for (int i = 0; i < arraysize(bz::uniformBuffers); i++) {
		bz::device.CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(UniformBufferObject), &bz::uniformBuffers[i]);
		bz::uniformBuffers[i].map(bz::device.logicalDevice);
	}
}

void CreateRenderAttachment(RenderAttachment& attachment, RenderAttachmentDesc desc) {
	attachment.format = desc.format;

	VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_NONE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (desc.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
		aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	else if (desc.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
		// TODO: check if format has depth / stencil and set ASPECT_DEPTH / ASPECT_STENCIL conditionally.
		aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	else {
		Check(desc.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT), "Unsupported image usage");
	}

	CreateImage(desc.extent.width, desc.extent.height, 1, desc.samples, desc.format, VK_IMAGE_TILING_OPTIMAL, desc.usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, attachment.image, attachment.memory);
	CreateImageView(attachment.image, attachment.format, aspectMask, 1, attachment.view);

	attachment.attachmentInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = attachment.view,
		.imageLayout = layout,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = (desc.usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.clearValue = {} // We use reversed depth, so clear color is 0 regardless of whether this is a depth or color attachment
	};
}

void CreateRenderAttachments() {
	CreateRenderAttachment(bz::depth, {
		.extent = bz::swapchain.extent,
		.format = VK_FORMAT_D32_SFLOAT,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.samples = bz::msaaSamples
	});

	CreateRenderAttachment(bz::color, {
		.extent = bz::swapchain.extent,
		.format = bz::swapchain.format,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,	// TODO: clarify if transient is needed
		.samples = bz::msaaSamples
	});

	CreateRenderAttachment(bozo::normal, {
		.extent = bz::swapchain.extent,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.samples = bz::msaaSamples
	});

	CreateRenderAttachment(bozo::albedo, {
		.extent = bz::swapchain.extent,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.samples = bz::msaaSamples
	});

	VkSamplerCreateInfo samplerInfo = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias = 0.0f,
		.anisotropyEnable = bz::device.enabledFeatures.samplerAnisotropy,
		.maxAnisotropy = bz::device.enabledFeatures.samplerAnisotropy ? bz::device.properties.limits.maxSamplerAnisotropy : 1.0f,
		.minLod = 0.0f,
		.maxLod = 1.0f,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK
	};

	VkCheck(vkCreateSampler(bz::device.logicalDevice, &samplerInfo, nullptr, &bozo::attachmentSampler), "Failed to create sampler");
}

void CleanupRenderAttachments() {
	vkDestroySampler(bz::device.logicalDevice, bozo::attachmentSampler, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bozo::albedo.view, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bozo::albedo.image, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bozo::albedo.memory, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bozo::normal.view, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bozo::normal.image, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bozo::normal.memory, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bz::color.view, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bz::color.image, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bz::color.memory, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bz::depth.view, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bz::depth.image, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bz::depth.memory, nullptr);
}

void UpdateRenderAttachmentDescriptorSets() {
	// Image descriptors for the offscreen gbuffer attachments
	VkDescriptorImageInfo texDescriptorNormal = {
		.sampler = bozo::attachmentSampler,
		.imageView = bozo::normal.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	VkDescriptorImageInfo texDescriptorAlbedo = {
		.sampler = bozo::attachmentSampler,
		.imageView = bozo::albedo.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};

	VkWriteDescriptorSet writeDescriptorSets[] = {
		{	// Binding 1: World space normals texture
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bozo::descriptorSet,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texDescriptorNormal
		},
		{	// Binding 2: Albedo texture
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bozo::descriptorSet,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texDescriptorAlbedo
		}
	};
	vkUpdateDescriptorSets(bz::device.logicalDevice, arraysize(writeDescriptorSets), writeDescriptorSets, 0, nullptr);
}

void CreateDescriptorPool() {
	VkDescriptorPoolSize poolSizes[2] = {
		{ // uniform buffer descriptor pool
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = arraysize(bz::uniformBuffers)	// 1 descriptor per uniform buffer
		},
		{ // combined image sampler descriptor pool per model image / texture + 1 sampler for gbuffer
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = (u32)flightHelmet->images.size() + 2	// why +2 instead of +1?
		}
	};

	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = poolSizes[0].descriptorCount + poolSizes[1].descriptorCount,
		.poolSizeCount = arraysize(poolSizes),
		.pPoolSizes = poolSizes
	};

	VkCheck(vkCreateDescriptorPool(bz::device.logicalDevice, &poolInfo, nullptr, &bz::descriptorPool), "Failed to create descriptor pool");
}

void SetupPipelines() {
	VkPipelineShaderStageCreateInfo deferredShaders[] = {
		LoadShader("shaders/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
		LoadShader("shaders/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
	};

	VkDynamicState dynamicState[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = arraysize(dynamicState), .pDynamicStates = dynamicState };
	VkPipelineViewportStateCreateInfo viewportStateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

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
		.cullMode = VK_CULL_MODE_FRONT_BIT,
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

	VkPipelineColorBlendAttachmentState colorBlendAttachment = { .blendEnable = VK_FALSE, .colorWriteMask = 0xF };

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
		.depthAttachmentFormat = bz::depth.format
	};

	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingCreateInfo,
		.stageCount = arraysize(deferredShaders),
		.pStages = deferredShaders,
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssemblyInfo,
		.pViewportState = &viewportStateInfo,
		.pRasterizationState = &rasterizationInfo,
		.pMultisampleState = &multisampeInfo,
		.pDepthStencilState = &depthStencilInfo,
		.pColorBlendState = &colorBlendStateInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = bozo::pipelineLayout
	};

	VkCheck(vkCreateGraphicsPipelines(bz::device.logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bozo::deferredPipeline), "Failed to create graphics pipeline");

	VkPipelineShaderStageCreateInfo offscreenShaders[] = {
		LoadShader("shaders/offscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
		LoadShader("shaders/offscreen.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
	};

	VkVertexInputBindingDescription bindingDescription = GLTFModel::Vertex::GetBindingDescription();
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions = GLTFModel::Vertex::GetAttributeDescriptions();
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount = (u32)attributeDescriptions.size();
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;

	VkPipelineColorBlendAttachmentState blendAttachmentStates[] = {
		{.blendEnable = VK_FALSE, .colorWriteMask = 0xF },
		{.blendEnable = VK_FALSE, .colorWriteMask = 0xF },
	};
	colorBlendStateInfo.attachmentCount = arraysize(blendAttachmentStates);
	colorBlendStateInfo.pAttachments = blendAttachmentStates;

	pipelineInfo.stageCount = arraysize(offscreenShaders);
	pipelineInfo.pStages = offscreenShaders;

	VkFormat colorAttachmentFormats[] = {bozo::normal.format, bozo::albedo.format};
	renderingCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = arraysize(colorAttachmentFormats),
		.pColorAttachmentFormats = colorAttachmentFormats,
		.depthAttachmentFormat = bz::depth.format
	};

	VkCheck(vkCreateGraphicsPipelines(bz::device.logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bozo::offscreenPipeline), "Failed to create graphics pipeline");

	vkDestroyShaderModule(bz::device.logicalDevice, deferredShaders[0].module, nullptr);
	vkDestroyShaderModule(bz::device.logicalDevice, deferredShaders[1].module, nullptr);

	vkDestroyShaderModule(bz::device.logicalDevice, offscreenShaders[0].module, nullptr);
	vkDestroyShaderModule(bz::device.logicalDevice, offscreenShaders[1].module, nullptr);
}

void SetupDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding[] = {
		{	// Binding 0 : Vertex shader uniform buffer
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
		}
	};
	
	VkDescriptorSetLayoutCreateInfo uboDescriptorLayout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = arraysize(uboLayoutBinding),
		.pBindings = uboLayoutBinding
	};

	VkCheck(vkCreateDescriptorSetLayout(bz::device.logicalDevice, &uboDescriptorLayout, nullptr, &bozo::uboDescriptorSetLayout), "Failed to create descriptor set layout");

	VkDescriptorSetLayoutBinding setLayoutBindings[] = {
		{	// Binding 1 : Normals texture target
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{	// Binding 2 : Albedo texture target
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		}
	};

	VkDescriptorSetLayoutCreateInfo descriptorLayout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = arraysize(setLayoutBindings),
		.pBindings = setLayoutBindings
	};

	VkCheck(vkCreateDescriptorSetLayout(bz::device.logicalDevice, &descriptorLayout, nullptr, &bozo::descriptorSetLayout), "Failed to create descriptor set layout");

	VkPushConstantRange pushConstantRange = { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(glm::mat4) };
	VkDescriptorSetLayout layouts[] = { bozo::uboDescriptorSetLayout, bozo::descriptorSetLayout };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = arraysize(layouts),
		.pSetLayouts = layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
	};

	VkCheck(vkCreatePipelineLayout(bz::device.logicalDevice, &pipelineLayoutInfo, nullptr, &bozo::pipelineLayout), "Failed to create pipeline layout");
}

void AllocateDescriptorSets() {

		// Deferred composition descriptor set
	{
		std::vector<VkDescriptorSetLayout> layouts(2, bozo::descriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = bz::descriptorPool,
			.descriptorSetCount = u32(layouts.size()),
			.pSetLayouts = layouts.data()
		};

		VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, &bozo::descriptorSet), "Failed to allocate descriptor sets");
	}

		// UBO buffer descriptor sets
	{
		std::vector<VkDescriptorSetLayout> layouts(arraysize(bozo::uboDescriptorSets), bozo::uboDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = bz::descriptorPool,
			.descriptorSetCount = u32(layouts.size()),
			.pSetLayouts = layouts.data(),
		};

		VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, bozo::uboDescriptorSets), "Failed to allocate descriptor sets");
	}

		// Material descriptor sets
	{
		std::vector<VkDescriptorSetLayout> layouts(1, bozo::descriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = bz::descriptorPool,
			.descriptorSetCount = u32(layouts.size()),
			.pSetLayouts = layouts.data(),
		};
		for (auto& image : flightHelmet->images) {
			VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, &image.descriptorSet), "Failed to allocate descriptor set for image");
		}
	}
}

void UpdateDescriptorSets() {
	// Offscreen: Model ubo
	for (u32 i = 0; i < arraysize(bozo::uboDescriptorSets); i++) {
		// Buffer descriptor for the offscreen uniform buffer
		VkDescriptorBufferInfo uniformBufferDescriptor = {
			.buffer = bz::uniformBuffers[i].buffer,
			.offset = 0,
			.range = sizeof(UniformBufferObject)
		};

		VkWriteDescriptorSet writeDescriptorSet = {	// Binding 0: Vertex shader uniform buffer
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bozo::uboDescriptorSets[i],
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &uniformBufferDescriptor
		};

		vkUpdateDescriptorSets(bz::device.logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}

	// Offscreen: Model materials
	// TODO: this should probably be the done by the GLTFModel itself
	for (auto& image : flightHelmet->images) {
		VkWriteDescriptorSet writeDescriptorSet = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = image.descriptorSet,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &image.texture.descriptor
		};

		vkUpdateDescriptorSets(bz::device.logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}
}

void RecordDeferredCommandBuffer(VkCommandBuffer cmd, u32 imageIndex) {
	VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VkViewport viewport = {
		.x = 0.0f, .y = 0.0f,
		.width = (float)bz::swapchain.extent.width,
		.height = (float)bz::swapchain.extent.height,
		.minDepth = 0.0f, .maxDepth = 1.0f
	};
	VkRect2D scissor = { .offset = { 0, 0 }, .extent = bz::swapchain.extent };

	VkCheck(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin recording command buffer!");

	VkRenderingAttachmentInfo colorAttachments[] = { bozo::normal.attachmentInfo, bozo::albedo.attachmentInfo };
	VkRenderingAttachmentInfo depthAttachment[] = { bz::depth.attachmentInfo };

	VkRenderingInfo renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
			.offset = {0, 0},
			.extent = bz::swapchain.extent
		},
		.layerCount = 1,
		.colorAttachmentCount = arraysize(colorAttachments),
		.pColorAttachments = colorAttachments,
		.pDepthAttachment = depthAttachment,
		.pStencilAttachment = nullptr
	};

	SetImageLayout(cmd, bz::depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

	SetImageLayout(cmd, bozo::normal.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	SetImageLayout(cmd, bozo::albedo.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	vkCmdBeginRendering(cmd, &renderingInfo);

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bozo::offscreenPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bozo::pipelineLayout, 0, 1, &bozo::uboDescriptorSets[currentFrame], 0, nullptr);

	flightHelmet->Draw(cmd, bozo::pipelineLayout);

	vkCmdEndRendering(cmd);

	SetImageLayout(cmd, bz::swapchain.images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	SetImageLayout(cmd, bz::depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	SetImageLayout(cmd, bozo::normal.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	SetImageLayout(cmd, bozo::albedo.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	bz::color.attachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
	bz::color.attachmentInfo.resolveImageView = bz::swapchain.imageViews[imageIndex];
	bz::color.attachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
			.offset = {0, 0},
			.extent = bz::swapchain.extent
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &bz::color.attachmentInfo,
		.pDepthAttachment = nullptr,
		.pStencilAttachment = nullptr
	};

	vkCmdBeginRendering(cmd, &renderingInfo);

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bozo::pipelineLayout, 1, 1, &bozo::descriptorSet, 0, nullptr);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bozo::deferredPipeline);
	vkCmdDraw(cmd, 3, 1, 0, 0);

	bz::Overlay.Draw(cmd);

	vkCmdEndRendering(cmd);

	SetImageLayout(cmd, bz::swapchain.images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

	VkCheck(vkEndCommandBuffer(cmd), "Failed to record command buffer");
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
		.descriptorSetCount = arraysize(bz::uniformBuffers), // 1 descriptor set per uniform buffer
		.pSetLayouts = setLayouts
	};
	VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, bz::uboDescriptorSets), "Failed to allocate UBO descriptor sets");

	for (int i = 0; i < arraysize(bz::uniformBuffers); i++) {
		VkDescriptorBufferInfo bufferInfo = {
			.buffer = bz::uniformBuffers[i].buffer,
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

	// Create descriptor sets for materials.
	// TODO: this should probably be the done by the GLTFModel itself
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

void RecordCommandBuffer(VkCommandBuffer commandBuffer, u32 imageIndex) {
	VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

	VkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin recording command buffer!");

	bz::color.attachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
	bz::color.attachmentInfo.resolveImageView = bz::swapchain.imageViews[imageIndex];
	bz::color.attachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkRenderingInfo renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
			.offset = {0, 0},
			.extent = bz::swapchain.extent
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &bz::color.attachmentInfo,
		.pDepthAttachment = &bz::depth.attachmentInfo,
		.pStencilAttachment = nullptr
	};

	SetImageLayout(commandBuffer, bz::swapchain.images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	SetImageLayout(commandBuffer, bz::depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
	
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

	SetImageLayout(commandBuffer, bz::swapchain.images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

	VkCheck(vkEndCommandBuffer(commandBuffer), "Failed to record command buffer");
}

void CreateRenderFrames() {
	VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };

	for (u32 i = 0; i < arraysize(bz::renderFrames); i++) {
		VkCheck(vkCreateSemaphore(bz::device.logicalDevice, &semaphoreInfo, nullptr, &bz::renderFrames[i].imageAvailable), "Failed to create imageAvailable semaphore");
		VkCheck(vkCreateSemaphore(bz::device.logicalDevice, &semaphoreInfo, nullptr, &bz::renderFrames[i].renderFinished), "Failed to create renderFinished semaphore");
		VkCheck(vkCreateFence(bz::device.logicalDevice, &fenceInfo, nullptr, &bz::renderFrames[i].inFlight), "Failed to create inFlight fence");
		bz::renderFrames[i].commandBuffer = bz::device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	}
}

void InitVulkan() {
	bz::device.CreateDevice(window);
	bz::msaaSamples = bz::device.GetMaxUsableSampleCount();
	bz::swapchain.CreateSwapchain(window, bz::device, {
		.enableVSync = true, 
		.preferredImageCount = 2, 
		.oldSwapchain = VK_NULL_HANDLE 
	});

	flightHelmet = new GLTFModel(bz::device);
	flightHelmet->LoadGLTFFile("assets/FlightHelmet/FlightHelmet.gltf");

	CreateUniformBuffers();
	CreateRenderAttachments();

	CreateDescriptorPool();

#ifdef DEFERRED
	SetupDescriptorSetLayout();
	AllocateDescriptorSets();
	UpdateRenderAttachmentDescriptorSets();
	UpdateDescriptorSets();
	SetupPipelines();
#else
	CreateDescriptorSets();
	CreateGraphicsPipeline();
#endif

	CreateRenderFrames();
}

void CleanupSwapchain() {
	CleanupRenderAttachments();
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
		.preferredImageCount = 2, 
		.oldSwapchain = VK_NULL_HANDLE 
	});

	CreateRenderAttachments();
	UpdateRenderAttachmentDescriptorSets();
}

void CleanupVulkan() {
	CleanupSwapchain();

	delete flightHelmet;

	for (int i = 0; i < arraysize(bz::uniformBuffers); i++) {
		bz::uniformBuffers[i].unmap(bz::device.logicalDevice);
		bz::uniformBuffers[i].destroy(bz::device.logicalDevice);
	}

	vkDestroyDescriptorPool(bz::device.logicalDevice, bz::descriptorPool, nullptr);

#ifdef DEFERRED
	vkDestroyPipeline(bz::device.logicalDevice, bozo::deferredPipeline, nullptr);
	vkDestroyPipeline(bz::device.logicalDevice, bozo::offscreenPipeline, nullptr);
	vkDestroyPipelineLayout(bz::device.logicalDevice, bozo::pipelineLayout, nullptr);

	vkDestroyDescriptorSetLayout(bz::device.logicalDevice, bozo::descriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(bz::device.logicalDevice, bozo::uboDescriptorSetLayout, nullptr);
#else
	vkDestroyPipeline(bz::device.logicalDevice, bz::graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(bz::device.logicalDevice, bz::pipelineLayout, nullptr);

	vkDestroyDescriptorSetLayout(bz::device.logicalDevice, bz::uboLayout, nullptr);
	vkDestroyDescriptorSetLayout(bz::device.logicalDevice, bz::texturesLayout, nullptr);
#endif

	for (int i = 0; i < arraysize(bz::renderFrames); i++) {
		vkDestroySemaphore(bz::device.logicalDevice, bz::renderFrames[i].imageAvailable, nullptr);
		vkDestroySemaphore(bz::device.logicalDevice, bz::renderFrames[i].renderFinished, nullptr);
		vkDestroyFence(bz::device.logicalDevice, bz::renderFrames[i].inFlight, nullptr);
	}

	bz::device.DestroyDevice();
}

void UpdateUniformBuffer(u32 currentImage) {
	UniformBufferObject ubo = {
		.view = bz::camera.view,
		.proj = bz::camera.projection
	};

	memcpy(bz::uniformBuffers[currentImage].mapped, &ubo, sizeof(ubo));
}

void DrawFrame() {
	VkCheck(vkWaitForFences(bz::device.logicalDevice, 1, &bz::renderFrames[currentFrame].inFlight, VK_TRUE, UINT64_MAX), "Wait for inFlight fence failed");

	u32 imageIndex;
	VkResult result = vkAcquireNextImageKHR(bz::device.logicalDevice, bz::swapchain.swapchain, UINT64_MAX, bz::renderFrames[currentFrame].imageAvailable, VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		RecreateSwapchain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		VkCheck(result, "Failed to acquire swapchain image!");
	}

	UpdateUniformBuffer(currentFrame);

	// Only reset the fence if we swapchain was valid and we are actually submitting work.
	VkCheck(vkResetFences(bz::device.logicalDevice, 1, &bz::renderFrames[currentFrame].inFlight), "Failed to reset inFlight fence");

	VkCheck(vkResetCommandBuffer(bz::renderFrames[currentFrame].commandBuffer, 0), "Failed to reset command buffer");
#ifdef DEFERRED
	RecordDeferredCommandBuffer(bz::renderFrames[currentFrame].commandBuffer, imageIndex);
#else
	RecordCommandBuffer(bz::renderFrames[currentFrame].commandBuffer, imageIndex);
#endif

	VkSemaphore waitSemaphores[] = { bz::renderFrames[currentFrame].imageAvailable };
	VkSemaphore signalSemaphores[] = { bz::renderFrames[currentFrame].renderFinished };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = waitSemaphores,
		.pWaitDstStageMask = waitStages,
		.commandBufferCount = 1,
		.pCommandBuffers = &bz::renderFrames[currentFrame].commandBuffer,
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

	VkCheck(vkQueueSubmit(bz::device.graphicsQueue, 1, &submitInfo, bz::renderFrames[currentFrame].inFlight), "Failed to submit draw command buffer");
	result = vkQueuePresentKHR(bz::device.graphicsQueue, &presentInfo);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || bz::framebufferResized) {
		bz::framebufferResized = false;
		RecreateSwapchain();
	}
	else if (result != VK_SUCCESS) {
		VkCheck(result, "Failed to present swapchain image");
	}

	currentFrame = (currentFrame + 1) % arraysize(bz::renderFrames);
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