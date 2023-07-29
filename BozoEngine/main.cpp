#include "Device.h"
#include "Swapchain.h"
#include "Texture.h"
#include "Camera.h"
#include "Tools.h"

#include "GLTF.h"
#include "UIOverlay.h"

#include "Pipeline.h"

constexpr u32 WIDTH = 1600;
constexpr u32 HEIGHT = 900;

GLTFModel* flightHelmet;
u32 currentFrame = 0;

struct CameraUBO {
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

struct RenderFrame {
	VkSemaphore imageAvailable;
	VkSemaphore renderFinished;
	VkFence inFlight;

	VkCommandPool commandPool;
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

struct DepthAttachment {
	VkImage image;
	VkDeviceMemory memory;

	VkImageView depthStencilView;
	VkImageView depthView;
	VkImageView stencilView;

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
	RenderAttachment albedo, normal, occMetRough;
	DepthAttachment depth;
	VkSampler attachmentSampler;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout uboDescriptorSetLayout, descriptorSetLayout;
	VkPipelineLayout pipelineLayout;

	VkDescriptorSet descriptorSet;
	VkDescriptorSet uboDescriptorSets[arraysize(bz::uniformBuffers)];

	VkPipeline offscreenPipeline;
	VkPipeline deferredPipeline;
	VkPipeline albedoPipeline, normalPipeline, occMetRoughPipeline, depthPipeline;

	VkPipeline currentPipeline;

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

	case GLFW_KEY_0: bz::currentPipeline = bz::deferredPipeline; break;
	case GLFW_KEY_1: bz::currentPipeline = bz::albedoPipeline; break;
	case GLFW_KEY_2: bz::currentPipeline = bz::normalPipeline; break;
	case GLFW_KEY_3: bz::currentPipeline = bz::occMetRoughPipeline; break;
	case GLFW_KEY_4: bz::currentPipeline = bz::depthPipeline; break;
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

void CreateUniformBuffers() {
	for (int i = 0; i < arraysize(bz::uniformBuffers); i++) {
		bz::device.CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(CameraUBO), &bz::uniformBuffers[i]);
		bz::uniformBuffers[i].map(bz::device.logicalDevice);
	}
}

void CreateRenderAttachment(RenderAttachment& attachment, RenderAttachmentDesc desc) {
	Check(desc.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, "CreateRenderAttachment can only be used to create color attachments");
	attachment.format = desc.format;

	VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	CreateImage(desc.extent.width, desc.extent.height, 1, desc.samples, desc.format, VK_IMAGE_TILING_OPTIMAL, desc.usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, attachment.image, attachment.memory);
	CreateImageView(attachment.image, attachment.format, aspectMask, 1, attachment.view);

	attachment.attachmentInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = attachment.view,
		.imageLayout = layout,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = (desc.usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE
	};
}

void CreateDepthAttachment(DepthAttachment& attachment, RenderAttachmentDesc desc) {
	Check(desc.usage & (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT), "CreateDepthAttachment can only be used to create depth attachments");
	attachment.format = desc.format;

	VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	VkImageLayout layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	if (attachment.format >= VK_FORMAT_D16_UNORM_S8_UINT) {	// if format has stencil
		aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	CreateImage(desc.extent.width, desc.extent.height, 1, desc.samples, desc.format, VK_IMAGE_TILING_OPTIMAL, desc.usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, attachment.image, attachment.memory);
	CreateImageView(bz::depth.image, bz::depth.format, aspectMask, 1, bz::depth.depthStencilView);
	CreateImageView(bz::depth.image, bz::depth.format, VK_IMAGE_ASPECT_DEPTH_BIT, 1, bz::depth.depthView);

	if (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
		CreateImageView(bz::depth.image, bz::depth.format, VK_IMAGE_ASPECT_STENCIL_BIT, 1, bz::depth.stencilView);
	}

	attachment.attachmentInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = attachment.depthStencilView,
		.imageLayout = layout,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = (desc.usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE
	};
}

void CreateRenderAttachments() {
	CreateDepthAttachment(bz::depth, {
		.extent = bz::swapchain.extent,
		.format = VK_FORMAT_D24_UNORM_S8_UINT,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.samples = bz::msaaSamples
	});

	CreateRenderAttachment(bz::normal, {
		.extent = bz::swapchain.extent,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.samples = bz::msaaSamples
	});

	CreateRenderAttachment(bz::albedo, {
		.extent = bz::swapchain.extent,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.samples = bz::msaaSamples
	});

	CreateRenderAttachment(bz::occMetRough, {
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

	VkCheck(vkCreateSampler(bz::device.logicalDevice, &samplerInfo, nullptr, &bz::attachmentSampler), "Failed to create sampler");
}

void CleanupRenderAttachments() {
	vkDestroySampler(bz::device.logicalDevice, bz::attachmentSampler, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bz::albedo.view, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bz::albedo.image, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bz::albedo.memory, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bz::normal.view, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bz::normal.image, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bz::normal.memory, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bz::occMetRough.view, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bz::occMetRough.image, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bz::occMetRough.memory, nullptr);

	vkDestroyImageView(bz::device.logicalDevice, bz::depth.depthStencilView, nullptr);
	vkDestroyImageView(bz::device.logicalDevice, bz::depth.depthView, nullptr);
	vkDestroyImageView(bz::device.logicalDevice, bz::depth.stencilView, nullptr);
	vkDestroyImage(bz::device.logicalDevice, bz::depth.image, nullptr);
	vkFreeMemory(bz::device.logicalDevice, bz::depth.memory, nullptr);
}

void UpdateRenderAttachmentDescriptorSets() {
	// Image descriptors for the offscreen gbuffer attachments
	VkDescriptorImageInfo texDescriptorNormal = {
		.sampler = bz::attachmentSampler,
		.imageView = bz::normal.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	VkDescriptorImageInfo texDescriptorAlbedo = {
		.sampler = bz::attachmentSampler,
		.imageView = bz::albedo.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	VkDescriptorImageInfo texDescriptorOccMetRough = {
		.sampler = bz::attachmentSampler,
		.imageView = bz::occMetRough.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};

	VkDescriptorImageInfo texDescriptorDepth = {
		.sampler = bz::attachmentSampler,
		.imageView = bz::depth.depthView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	};

	VkWriteDescriptorSet writeDescriptorSets[] = {
		{	// Binding 0: Albedo texture
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bz::descriptorSet,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texDescriptorAlbedo
		},
		{	// Binding 1: World space normals texture
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bz::descriptorSet,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texDescriptorNormal
		},
		{	// Binding 2: Occlusion / metal / rough texture
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bz::descriptorSet,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texDescriptorOccMetRough
		},
		{	// Binding 3: Depth texture
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bz::descriptorSet,
			.dstBinding = 3,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texDescriptorDepth
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
		{ // combined image sampler descriptor pool per material + 1 sampling the gbuffer
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = (u32)flightHelmet->materials.size() + 1
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

// TODO: Temporary
struct Program {
	u32 stageCount;
	VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfos[2];

	~Program() {
		vkDestroyShaderModule(bz::device.logicalDevice, pipelineShaderStageCreateInfos[0].module, nullptr);
		vkDestroyShaderModule(bz::device.logicalDevice, pipelineShaderStageCreateInfos[1].module, nullptr);
	}
};

void CreatePipeline(VkPipeline* pipeline, VkPipelineLayout pipelineLayout, const Program& shaders,
	std::vector<VkVertexInputBindingDescription> vertexBindingDescription, std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions,
	VkPrimitiveTopology primitiveTopology,
	VkCullModeFlags triangleCullMode, VkFrontFace triangleFrontFace,
	VkSampleCountFlagBits msaaSampleCount,
	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments,
	std::vector<VkFormat> colorAttachmentFormats, VkFormat depthAttachmentFormat, VkFormat stencilAttachmentFormat)
{
	VkDynamicState dynamicState[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = arraysize(dynamicState), .pDynamicStates = dynamicState };
	VkPipelineViewportStateCreateInfo viewportStateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = (u32)vertexBindingDescription.size(),
		.pVertexBindingDescriptions = vertexBindingDescription.data(),
		.vertexAttributeDescriptionCount = (u32)vertexAttributeDescriptions.size(),
		.pVertexAttributeDescriptions = vertexAttributeDescriptions.data()
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = primitiveTopology,
		.primitiveRestartEnable = VK_FALSE
	};

	VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,		// depth clamp discards fragments outside the near/far planes. Usefull for shadow maps, requires enabling a GPU feature.
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = triangleCullMode,
		.frontFace = triangleFrontFace,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisampeInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = msaaSampleCount,
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

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = (u32)colorBlendAttachments.size(),
		.pAttachments = colorBlendAttachments.data()
	};

	VkPipelineRenderingCreateInfo renderingCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = (u32)colorAttachmentFormats.size(),
		.pColorAttachmentFormats = colorAttachmentFormats.data(),
		.depthAttachmentFormat = depthAttachmentFormat,
		.stencilAttachmentFormat = stencilAttachmentFormat
	};

	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingCreateInfo,
		.stageCount = shaders.stageCount,
		.pStages = shaders.pipelineShaderStageCreateInfos,
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssemblyInfo,
		.pViewportState = &viewportStateInfo,
		.pRasterizationState = &rasterizationInfo,
		.pMultisampleState = &multisampeInfo,
		.pDepthStencilState = &depthStencilInfo,
		.pColorBlendState = &colorBlendStateInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = pipelineLayout
	};

	VkCheck(vkCreateGraphicsPipelines(bz::device.logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, pipeline), "Failed to create graphics pipeline");
}

void SetupPipelines() {
	Program offscreen = {
		.stageCount = 2,
		.pipelineShaderStageCreateInfos = {
			LoadShader("shaders/offscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			LoadShader("shaders/offscreen.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		}
	};

	CreatePipeline(&bz::offscreenPipeline, bz::pipelineLayout, offscreen,
		{ GLTFModel::Vertex::GetBindingDescription() }, GLTFModel::Vertex::GetAttributeDescriptions(),
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
		bz::msaaSamples, {
			{.blendEnable = VK_FALSE, .colorWriteMask = 0xF },
			{.blendEnable = VK_FALSE, .colorWriteMask = 0xF },
			{.blendEnable = VK_FALSE, .colorWriteMask = 0xF },
		}, { bz::albedo.format, bz::normal.format, bz::occMetRough.format }, bz::depth.format, bz::depth.format);

	Program deferred = {
		.stageCount = 2,
		.pipelineShaderStageCreateInfos = {
			LoadShader("shaders/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			LoadShader("shaders/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		}
	};

	// TODO: shader reflection etc.
	// optional `std::vector<const void*> specializations` parameter should be passed to CreateShader
	// CreateShader then asserts that `specializations.size()` matches the expected size
	// if no specializations were found during reflection, `specializations` is untouched and can be omitted when creating a shader
	VkSpecializationMapEntry mapEntries[] = { {.size = sizeof(u32)}, {.constantID = 1, .offset = sizeof(u32), .size = sizeof(u32)} };
	u32 specializations[] = { 0, (u32)bz::msaaSamples };
	VkSpecializationInfo specializationInfo = {
		.mapEntryCount = arraysize(mapEntries),
		.pMapEntries = mapEntries,
		.dataSize = sizeof(specializations),
		.pData = specializations
	};
	deferred.pipelineShaderStageCreateInfos[1].pSpecializationInfo = &specializationInfo;

	CreatePipeline(&bz::deferredPipeline, bz::pipelineLayout, deferred, {}, {},
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
		VK_SAMPLE_COUNT_1_BIT,
		{ {.blendEnable = VK_FALSE, .colorWriteMask = 0xF } },
		{ bz::swapchain.format }, bz::depth.format, bz::depth.format);

	// TODO: this should just be done w/ an integer in the ubo. separate pipelines is overkill.
	specializations[0] = 1;
	CreatePipeline(&bz::albedoPipeline, bz::pipelineLayout, deferred, {}, {},
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
		VK_SAMPLE_COUNT_1_BIT,
		{ {.blendEnable = VK_FALSE, .colorWriteMask = 0xF } },
		{ bz::swapchain.format }, bz::depth.format, bz::depth.format);

	specializations[0] = 2;
	CreatePipeline(&bz::normalPipeline, bz::pipelineLayout, deferred, {}, {},
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
		VK_SAMPLE_COUNT_1_BIT,
		{ {.blendEnable = VK_FALSE, .colorWriteMask = 0xF } },
		{ bz::swapchain.format }, bz::depth.format, bz::depth.format);

	specializations[0] = 3;
	CreatePipeline(&bz::occMetRoughPipeline, bz::pipelineLayout, deferred, {}, {},
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
		VK_SAMPLE_COUNT_1_BIT,
		{ {.blendEnable = VK_FALSE, .colorWriteMask = 0xF } },
		{ bz::swapchain.format }, bz::depth.format, bz::depth.format);

	specializations[0] = 4;
	CreatePipeline(&bz::depthPipeline, bz::pipelineLayout, deferred, {}, {},
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
		VK_SAMPLE_COUNT_1_BIT,
		{ {.blendEnable = VK_FALSE, .colorWriteMask = 0xF } },
		{ bz::swapchain.format }, bz::depth.format, bz::depth.format);

	bz::currentPipeline = bz::deferredPipeline;
}

void SetupDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding[] = {
		{	// Binding 0 : Vertex shader uniform buffer
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			// TODO: should only be used in vertex stage. Atm, we just reuse the camera ubo in the deferred pass, but it should have its own ubo.
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
		}
	};
	
	VkDescriptorSetLayoutCreateInfo uboDescriptorLayout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = arraysize(uboLayoutBinding),
		.pBindings = uboLayoutBinding
	};

	VkCheck(vkCreateDescriptorSetLayout(bz::device.logicalDevice, &uboDescriptorLayout, nullptr, &bz::uboDescriptorSetLayout), "Failed to create descriptor set layout");

	VkDescriptorSetLayoutBinding setLayoutBindings[] = {
		{	// Binding 0 : Albedo texture
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{	// Binding 1 : Normals texture
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{	// Binding 2 : Occlusion/metal/roughness texture
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{	// Binding 3 : Depth texture
			.binding = 3,
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

	VkCheck(vkCreateDescriptorSetLayout(bz::device.logicalDevice, &descriptorLayout, nullptr, &bz::descriptorSetLayout), "Failed to create descriptor set layout");

	VkPushConstantRange pushConstantRange = { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(glm::mat4) };
	VkDescriptorSetLayout layouts[] = { bz::uboDescriptorSetLayout, bz::descriptorSetLayout };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = arraysize(layouts),
		.pSetLayouts = layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
	};

	VkCheck(vkCreatePipelineLayout(bz::device.logicalDevice, &pipelineLayoutInfo, nullptr, &bz::pipelineLayout), "Failed to create pipeline layout");
}

void AllocateDescriptorSets() {
	{	// Deferred composition descriptor set
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = bz::descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &bz::descriptorSetLayout
		};

		VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, &bz::descriptorSet), "Failed to allocate descriptor sets");
	}	
	{	// UBO buffer descriptor sets
		std::vector<VkDescriptorSetLayout> layouts(arraysize(bz::uboDescriptorSets), bz::uboDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = bz::descriptorPool,
			.descriptorSetCount = u32(layouts.size()),
			.pSetLayouts = layouts.data(),
		};

		VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, bz::uboDescriptorSets), "Failed to allocate descriptor sets");
	}
	{	// Material descriptor sets
		VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = bz::descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &bz::descriptorSetLayout,
		};

		for (auto& material : flightHelmet->materials)
			VkCheck(vkAllocateDescriptorSets(bz::device.logicalDevice, &allocInfo, &material.descriptorSet), "Failed to allocate descriptor set for image");
	}
}

void UpdateDescriptorSets() {
	for (u32 i = 0; i < arraysize(bz::uboDescriptorSets); i++) {
		// Buffer descriptor for the offscreen uniform buffer
		VkDescriptorBufferInfo uniformBufferDescriptor = {
			.buffer = bz::uniformBuffers[i].buffer,
			.offset = 0,
			.range = sizeof(CameraUBO)
		};

		VkWriteDescriptorSet writeDescriptorSet = {	// Binding 0: Vertex shader uniform buffer
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = bz::uboDescriptorSets[i],
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
	for (const auto& material : flightHelmet->materials) {
		VkWriteDescriptorSet writeDescriptorSets[] = {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material.descriptorSet,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &flightHelmet->images[material.albedo.imageIndex].texture.descriptor
			}, {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material.descriptorSet,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &flightHelmet->images[material.normal.imageIndex].texture.descriptor
			}, {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material.descriptorSet,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &flightHelmet->images[material.OccMetRough.imageIndex].texture.descriptor
			}
		};

		vkUpdateDescriptorSets(bz::device.logicalDevice, arraysize(writeDescriptorSets), writeDescriptorSets, 0, nullptr);
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

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	VkRenderingAttachmentInfo colorAttachments[] = { bz::albedo.attachmentInfo, bz::normal.attachmentInfo, bz::occMetRough.attachmentInfo };
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
		.pStencilAttachment = depthAttachment
	};

	ImageBarrier(cmd, bz::depth.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,	VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,						VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	ImageBarrier(cmd, bz::albedo.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,						VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	ImageBarrier(cmd, bz::normal.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,						VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	ImageBarrier(cmd, bz::occMetRough.image,			VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,						VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::offscreenPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::pipelineLayout, 0, 1, &bz::uboDescriptorSets[currentFrame], 0, nullptr);

	vkCmdBeginRendering(cmd, &renderingInfo);

	flightHelmet->Draw(cmd, bz::pipelineLayout);

	vkCmdEndRendering(cmd);

	ImageBarrier(cmd, bz::swapchain.images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_NONE,		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_NONE,				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,	VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	ImageBarrier(cmd, bz::depth.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,	VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

	ImageBarrier(cmd, bz::albedo.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

	ImageBarrier(cmd, bz::normal.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

	ImageBarrier(cmd, bz::occMetRough.image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

	renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
			.offset = {0, 0},
			.extent = bz::swapchain.extent
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &bz::swapchain.attachmentInfos[imageIndex],
		.pDepthAttachment = nullptr,
		.pStencilAttachment = nullptr
	};
	
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::currentPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::pipelineLayout, 1, 1, &bz::descriptorSet, 0, nullptr);

	vkCmdBeginRendering(cmd, &renderingInfo);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	bz::Overlay.Draw(cmd);

	vkCmdEndRendering(cmd);

	ImageBarrier(cmd, bz::swapchain.images[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_NONE,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_NONE,
		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VkCheck(vkEndCommandBuffer(cmd), "Failed to record command buffer");
}

void CreateRenderFrames() {
	VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
	VkCommandPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = bz::device.queueIndex.graphics };

	for (u32 i = 0; i < arraysize(bz::renderFrames); i++) {
		VkCheck(vkCreateSemaphore(bz::device.logicalDevice, &semaphoreInfo, nullptr, &bz::renderFrames[i].imageAvailable), "Failed to create imageAvailable semaphore");
		VkCheck(vkCreateSemaphore(bz::device.logicalDevice, &semaphoreInfo, nullptr, &bz::renderFrames[i].renderFinished), "Failed to create renderFinished semaphore");
		VkCheck(vkCreateFence(bz::device.logicalDevice, &fenceInfo, nullptr, &bz::renderFrames[i].inFlight), "Failed to create inFlight fence");
		VkCheck(vkCreateCommandPool(bz::device.logicalDevice, &poolInfo, nullptr, &bz::renderFrames[i].commandPool), "Failed to allocate renderframe command pool");
		bz::renderFrames[i].commandBuffer = bz::device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, bz::renderFrames[i].commandPool);
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

	SetupDescriptorSetLayout();
	AllocateDescriptorSets();
	UpdateRenderAttachmentDescriptorSets();
	UpdateDescriptorSets();
	SetupPipelines();

	Shader offscreenVert = Shader::Create(bz::device, "shaders/offscreen.vert.spv");
	Shader offscreenFrag = Shader::Create(bz::device, "shaders/offscreen.frag.spv");

	Shader deferredVert = Shader::Create(bz::device, "shaders/deferred.vert.spv");
	Shader deferredFrag = Shader::Create(bz::device, "shaders/deferred.frag.spv");

	Pipeline offscreenPipeline = Pipeline::Create(bz::device, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineDesc{
		.shaders = { offscreenVert, offscreenFrag },
		.attachments = {
			.formats = { bz::albedo.format, bz::normal.format, bz::occMetRough.format },
			.depthStencilFormat = bz::depth.format,
			.blendEnable = VK_FALSE,
			.blendStates = {
				{.blendEnable = VK_FALSE, .colorWriteMask = 0xF },
				{.blendEnable = VK_FALSE, .colorWriteMask = 0xF },
				{.blendEnable = VK_FALSE, .colorWriteMask = 0xF },
			},
		},
		.rasterization = {
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
		},
		.sampleCount = bz::msaaSamples,
		.vertexInput = { 
			.bindingDesc = { GLTFModel::Vertex::GetBindingDescription() },
			.attributeDesc = GLTFModel::Vertex::GetAttributeDescriptions()
		}
	});

	Pipeline deferredPipeline = Pipeline::Create(bz::device, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineDesc{
		.shaders = { deferredVert, deferredFrag },
		.attachments = {
			.formats = { bz::swapchain.format },
			.depthStencilFormat = bz::depth.format,
			.blendEnable = VK_FALSE,
			.blendStates = { {.blendEnable = VK_FALSE, .colorWriteMask = 0xF } },
		},
		.rasterization = {
			.cullMode = VK_CULL_MODE_FRONT_BIT,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
		},
		.sampleCount = VK_SAMPLE_COUNT_1_BIT
	});

	// TODO: Sample api.
	// make BINDGROUP strongly typed (enum class), and take it as a param for createBindGroup
	// Union sampler, view, layout into a descriptor / allow us to just give a Texture2D directly
	// instead of having to manually extract samler,view,layout information from it...
	// TODO: figure out if user should be responsible for freeing bindgroups / how to handle their reuse
	enum BINDGROUP {
		GLOBALS = 0,
		MATERIAL = 1
	};
#if 0
	BindGroup cameraUBO_0 = offscreenPipeline.CreateBindGroup(bz::device, bz::descriptorPool, BINDGROUP::GLOBALS, {
		.buffers = { { .binding = 0, .buffer = bz::uniformBuffers[0], .offset = 0, .size = 0 }}
	});

	BindGroup material_0 = offscreenPipeline.CreateBindGroup(bz::device, bz::descriptorPool, BINDGROUP::MATERIAL, {
		.textures = {
			{ .binding = 0, .sampler = flightHelmet->images[0].texture.sampler, .view = flightHelmet->images[0].texture.view, .layout = flightHelmet->images[0].texture.layout },
			{ .binding = 1, .sampler = flightHelmet->images[1].texture.sampler, .view = flightHelmet->images[1].texture.view, .layout = flightHelmet->images[1].texture.layout },
			{ .binding = 2, .sampler = flightHelmet->images[2].texture.sampler, .view = flightHelmet->images[2].texture.view, .layout = flightHelmet->images[2].texture.layout }
		}
	});
#endif

	offscreenVert.Destroy(bz::device);
	offscreenFrag.Destroy(bz::device);
	deferredVert.Destroy(bz::device);
	deferredFrag.Destroy(bz::device);

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

	vkDestroyPipeline(bz::device.logicalDevice, bz::deferredPipeline, nullptr);
	vkDestroyPipeline(bz::device.logicalDevice, bz::offscreenPipeline, nullptr);

	vkDestroyPipeline(bz::device.logicalDevice, bz::albedoPipeline, nullptr);
	vkDestroyPipeline(bz::device.logicalDevice, bz::normalPipeline, nullptr);
	vkDestroyPipeline(bz::device.logicalDevice, bz::occMetRoughPipeline, nullptr);
	vkDestroyPipeline(bz::device.logicalDevice, bz::depthPipeline, nullptr);

	vkDestroyPipelineLayout(bz::device.logicalDevice, bz::pipelineLayout, nullptr);

	vkDestroyDescriptorSetLayout(bz::device.logicalDevice, bz::descriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(bz::device.logicalDevice, bz::uboDescriptorSetLayout, nullptr);

	for (int i = 0; i < arraysize(bz::renderFrames); i++) {
		vkDestroySemaphore(bz::device.logicalDevice, bz::renderFrames[i].imageAvailable, nullptr);
		vkDestroySemaphore(bz::device.logicalDevice, bz::renderFrames[i].renderFinished, nullptr);
		vkDestroyFence(bz::device.logicalDevice, bz::renderFrames[i].inFlight, nullptr);
		vkDestroyCommandPool(bz::device.logicalDevice, bz::renderFrames[i].commandPool, nullptr);
	}

	bz::device.DestroyDevice();
}

void UpdateUniformBuffer(u32 currentImage) {
	CameraUBO ubo = {
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

	//VkCheck(vkResetCommandBuffer(bz::renderFrames[currentFrame].commandBuffer, 0), "Failed to reset command buffer");
	VkCheck(vkResetCommandPool(bz::device.logicalDevice, bz::renderFrames[currentFrame].commandPool, 0), "Failed to reset frame command pool");
	RecordDeferredCommandBuffer(bz::renderFrames[currentFrame].commandBuffer, imageIndex);

	VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = bz::renderFrames[currentFrame].imageAvailable,
		.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	VkSemaphoreSubmitInfo signalSemaphoreSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = bz::renderFrames[currentFrame].renderFinished,
		.stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT		// signal once the entire commandbuffer has been executed.
	};
	VkCommandBufferSubmitInfo commandBufferSubmitInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, 
		.commandBuffer = bz::renderFrames[currentFrame].commandBuffer 
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
	VkCheck(vkQueueSubmit2(bz::device.graphicsQueue, 1, &submitInfo, bz::renderFrames[currentFrame].inFlight), "Failed to submit draw command buffer");

	VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &bz::renderFrames[currentFrame].renderFinished,
		.swapchainCount = 1,
		.pSwapchains = &bz::swapchain.swapchain,
		.pImageIndices = &imageIndex
	};
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

int main(int argc, char* argv[]) {
	InitWindow(WIDTH, HEIGHT);
	InitVulkan();

	bz::Overlay.vertShader = LoadShader("shaders/uioverlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	bz::Overlay.fragShader = LoadShader("shaders/uioverlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	bz::Overlay.Initialize(window, &bz::device, bz::swapchain.format, bz::depth.format);

	double lastFrame = 0.0f;

	while (!glfwWindowShouldClose(window)) {
		double currentFrame = glfwGetTime();
		float deltaTime = float(currentFrame - lastFrame);
		lastFrame = currentFrame;

		bz::camera.Update(deltaTime);
		bz::Overlay.Update();
		
		DrawFrame();

		glfwPollEvents();
	}

	// Wait until all commandbuffers are done so we can safely clean up semaphores they might potentially be using.
	vkDeviceWaitIdle(bz::device.logicalDevice);

	bz::Overlay.Free();

	CleanupVulkan();
	CleanupWindow();

	return 0;
}