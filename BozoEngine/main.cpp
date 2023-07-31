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
	UIOverlay* overlay;

	RenderFrame renderFrames[MAX_FRAMES_IN_FLIGHT];
	Buffer uniformBuffers[MAX_FRAMES_IN_FLIGHT];
	RenderAttachment albedo, normal, occMetRough;
	DepthAttachment depth;
	VkSampler attachmentSampler;

	BindGroupLayout materialLayout, globalsLayout;
	BindGroup gbufferBindings;
	BindGroup globalsBindings[arraysize(bz::uniformBuffers)];
	Pipeline offscreenPipeline, deferredPipeline;

	u32 renderMode = 0;

	VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
	bool framebufferResized = false;
}

double lastXpos = WIDTH / 2.0f;
double lastYpos = WIDTH / 2.0f;
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

	case GLFW_KEY_0:
	case GLFW_KEY_1:
	case GLFW_KEY_2:
	case GLFW_KEY_3:
	case GLFW_KEY_4:
		bz::renderMode = key - '0';
		break;
	}
}

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

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::offscreenPipeline.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::offscreenPipeline.pipelineLayout, 0, 1, &bz::globalsBindings[currentFrame].descriptorSet, 0, nullptr);

	vkCmdBeginRendering(cmd, &renderingInfo);

	flightHelmet->Draw(cmd, bz::offscreenPipeline.pipelineLayout);

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
	
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::deferredPipeline.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::deferredPipeline.pipelineLayout, 0, 1, &bz::globalsBindings[currentFrame].descriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::deferredPipeline.pipelineLayout, 1, 1, &bz::gbufferBindings.descriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, bz::deferredPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(u32), &bz::renderMode);

	vkCmdBeginRendering(cmd, &renderingInfo);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	bz::overlay->Draw(cmd);

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

void CreateBindGroupLayouts() {
	bz::materialLayout = BindGroupLayout::Create(bz::device, {
		{.binding = 0, .type = Binding::TEXTURE, .stages = Binding::FRAGMENT },
		{.binding = 1, .type = Binding::TEXTURE, .stages = Binding::FRAGMENT },
		{.binding = 2, .type = Binding::TEXTURE, .stages = Binding::FRAGMENT },
		{.binding = 3, .type = Binding::TEXTURE, .stages = Binding::FRAGMENT }
	});

	bz::globalsLayout = BindGroupLayout::Create(bz::device, {
		{.binding = 0, .type = Binding::BUFFER }
	});
}

void CreateBindGroups() {
	bz::gbufferBindings = BindGroup::Create(bz::device, bz::materialLayout, {
		.textures = {
			{.binding = 0, .sampler = bz::attachmentSampler, .view = bz::albedo.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			{.binding = 1, .sampler = bz::attachmentSampler, .view = bz::normal.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			{.binding = 2, .sampler = bz::attachmentSampler, .view = bz::occMetRough.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			{.binding = 3, .sampler = bz::attachmentSampler, .view = bz::depth.depthView, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }
		}
	});

	for (u32 i = 0; i < arraysize(bz::globalsBindings); i++) {
		bz::globalsBindings[i] = BindGroup::Create(bz::device, bz::globalsLayout, {
			.buffers = { {.binding = 0, .buffer = bz::uniformBuffers[i].buffer, .offset = 0, .size = sizeof(CameraUBO)}}
		});
	}
}

void UpdateGBufferBindGroup() {
	bz::gbufferBindings.Update(bz::device, {
		.textures = {
			{.binding = 0, .sampler = bz::attachmentSampler, .view = bz::albedo.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			{.binding = 1, .sampler = bz::attachmentSampler, .view = bz::normal.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			{.binding = 2, .sampler = bz::attachmentSampler, .view = bz::occMetRough.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			{.binding = 3, .sampler = bz::attachmentSampler, .view = bz::depth.depthView, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }
		}
	});
}

void CreatePipelines() {
	Shader offscreenVert = Shader::Create(bz::device, "shaders/offscreen.vert.spv");
	Shader offscreenFrag = Shader::Create(bz::device, "shaders/offscreen.frag.spv");

	Shader deferredVert = Shader::Create(bz::device, "shaders/deferred.vert.spv");
	Shader deferredFrag = Shader::Create(bz::device, "shaders/deferred.frag.spv");

	bz::offscreenPipeline = Pipeline::Create(bz::device, VK_PIPELINE_BIND_POINT_GRAPHICS, {
		.debugName = "Offscreen pipeline",
		.shaders = { offscreenVert, offscreenFrag },
		.bindGroups = { bz::globalsLayout, bz::materialLayout },
		.graphicsState = {
			.attachments = {
				.formats = { bz::albedo.format, bz::normal.format, bz::occMetRough.format },
				.depthStencilFormat = bz::depth.format
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
		}
	});

	bz::deferredPipeline = Pipeline::Create(bz::device, VK_PIPELINE_BIND_POINT_GRAPHICS, {
		.debugName = "Deferred pipeline",
		.shaders = { deferredVert, deferredFrag },
		.bindGroups = { bz::globalsLayout, bz::materialLayout },
		.graphicsState = {
			.attachments = {
				.formats = { bz::swapchain.format },
				.depthStencilFormat = bz::depth.format
			},
			.rasterization = {
				.cullMode = VK_CULL_MODE_FRONT_BIT,
				.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
			},
			.sampleCount = VK_SAMPLE_COUNT_1_BIT,
			.specialization = {
				.mapEntries = { { .size = sizeof(u32)}},
				.dataSize = sizeof(u32),
				.pData = &bz::msaaSamples
			}
		}
	});

	offscreenVert.Destroy(bz::device);
	offscreenFrag.Destroy(bz::device);
	deferredVert.Destroy(bz::device);
	deferredFrag.Destroy(bz::device);
}

void InitVulkan() {
	bz::device.CreateDevice(window);
	bz::msaaSamples = bz::device.GetMaxUsableSampleCount();
	bz::swapchain.CreateSwapchain(window, bz::device, {
		.enableVSync = true, 
		.preferredImageCount = 2, 
		.oldSwapchain = VK_NULL_HANDLE 
	});

	CreateUniformBuffers();
	CreateRenderAttachments();

	CreateBindGroupLayouts();
	CreateBindGroups();
	CreatePipelines();

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
	UpdateGBufferBindGroup();
}

void CleanupVulkan() {
	CleanupSwapchain();

	for (int i = 0; i < arraysize(bz::uniformBuffers); i++) {
		bz::uniformBuffers[i].unmap(bz::device.logicalDevice);
		bz::uniformBuffers[i].destroy(bz::device.logicalDevice);
	}

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

	bz::overlay = new UIOverlay(window, bz::device, bz::swapchain.format, bz::depth.format);

	flightHelmet = new GLTFModel(bz::device, bz::materialLayout, "assets/FlightHelmet/FlightHelmet.gltf");

	double lastFrame = 0.0f;
	while (!glfwWindowShouldClose(window)) {
		double currentFrame = glfwGetTime();
		float deltaTime = float(currentFrame - lastFrame);
		lastFrame = currentFrame;

		bz::camera.Update(deltaTime);
		bz::overlay->Update();
		
		DrawFrame();

		glfwPollEvents();
	}

	// Wait until all commandbuffers are done so we can safely clean up semaphores they might potentially be using.
	vkDeviceWaitIdle(bz::device.logicalDevice);

	delete flightHelmet;
	
	delete bz::overlay;

	bz::offscreenPipeline.Destroy(bz::device);
	bz::deferredPipeline.Destroy(bz::device);
	bz::globalsLayout.Destroy(bz::device);
	bz::materialLayout.Destroy(bz::device);

	CleanupVulkan();
	CleanupWindow();

	return 0;
}