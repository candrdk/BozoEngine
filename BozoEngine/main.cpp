#include "Device.h"
#include "Swapchain.h"
#include "Texture.h"
#include "Camera.h"
#include "Tools.h"

#include "GLTF.h"
#include "UIOverlay.h"
#include <imgui.h>

#include "Pipeline.h"

constexpr u32 WIDTH = 1600;
constexpr u32 HEIGHT = 900;

GLTFModel* model;
GLTFModel* plane;
GLTFModel* cube;
u32 currentFrame = 0;

struct CameraUBO {
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
	alignas(16) glm::vec3 camPos;
};

struct DirectionalLight {
	alignas(16) glm::vec3 direction;
	alignas(16) glm::vec3 ambient;
	alignas(16) glm::vec3 diffuse;
	alignas(16) glm::vec3 specular;
};

struct PointLight {
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 ambient;
	alignas(16) glm::vec3 diffuse;
	alignas(16) glm::vec3 specular;
};

#define MAX_POINT_LIGHTS 4
struct DeferredUBO {
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 invProj;
	alignas(16) glm::vec4 camPos;

	int pointLightCount;
	DirectionalLight dirLight;
	PointLight pointLights[MAX_POINT_LIGHTS];
};

struct RenderFrame {
	VkSemaphore imageAvailable;
	VkSemaphore renderFinished;
	VkFence inFlight;

	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
};

// Temporary namespace to contain globals
namespace bz {
	// Scene
	Camera camera(glm::vec3(0.0f, 1.25f, 1.5f), 1.0f, 60.0f, (float)WIDTH / HEIGHT, 0.01f, -30.0f, -90.0f);

	Texture   skybox;
	BindGroup skyboxBindGroup;

	bool bAnimateLight = true;
	DirectionalLight dirLight = {
		.direction = glm::vec3(0.0f, -1.0f,  0.0f),
		.ambient   = glm::vec3(0.05f, 0.05f, 0.05f),
		.diffuse   = glm::vec3(1.0f,  0.8f,  0.7f),
		.specular  = glm::vec3(0.1f,  0.1f,  0.1f)
	};
	PointLight pointLightR = {
		.position = glm::vec3(0.0f,  0.25f, 0.25f),
		.ambient  = glm::vec3(0.1f,  0.1f,  0.1f),
		.diffuse  = glm::vec3(1.0f,  0.0f,  0.0f),
		.specular = glm::vec3(0.05f, 0.05f, 0.05f)
	};
	PointLight pointLightG = {
		.position = glm::vec3(0.0f,  0.25f, 0.25f),
		.ambient  = glm::vec3(0.1f,  0.1f,  0.1f),
		.diffuse  = glm::vec3(0.0f,  1.0f,  0.0f),
		.specular = glm::vec3(0.05f, 0.05f, 0.05f)
	};
	PointLight pointLightB = {
		.position = glm::vec3(0.0f,  0.25f, 0.25f),
		.ambient  = glm::vec3(0.1f,  0.1f,  0.1f),
		.diffuse  = glm::vec3(0.0f,  0.0f,  1.0f),
		.specular = glm::vec3(0.05f, 0.05f, 0.05f)
	};

	// Deferred pass settings
	u32 renderMode      = 0;
	u32 parallaxMode    = 4;
	u32 parallaxSteps   = 8;
	float parallaxScale = 0.05f;

	// Vulkan stuff
	Device device;
	Swapchain swapchain;
	UIOverlay* overlay;

	RenderFrame renderFrames[MAX_FRAMES_IN_FLIGHT];

	// Uniform buffers
	Buffer uniformBuffers[MAX_FRAMES_IN_FLIGHT];
	Buffer deferredBuffers[MAX_FRAMES_IN_FLIGHT];
	BindGroup globalsBindings[arraysize(uniformBuffers)];
	BindGroup deferredBindings[arraysize(deferredBuffers)];

	// GBuffer
	Texture depth, albedo, normal, metallicRoughness;

	BindGroupLayout materialLayout, globalsLayout, skyboxLayout;
	BindGroup gbufferBindings;

	// Pipelines
	Pipeline offscreenPipeline, skyboxPipeline, deferredPipeline;

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

void CreateUniformBuffers() {
	for (u32 i = 0; i < arraysize(bz::uniformBuffers); i++) {
		bz::uniformBuffers[i] = Buffer::Create(bz::device, {
			.debugName = "Uniform buffer",
			.byteSize = sizeof(CameraUBO),
			.usage = Usage::UNIFORM_BUFFER,
			.memory = Memory::UPLOAD
		});
		bz::uniformBuffers[i].Map(bz::device);
	}

	for (u32 i = 0; i < arraysize(bz::uniformBuffers); i++) {
		bz::deferredBuffers[i] = Buffer::Create(bz::device, {
			.debugName = "Deferred uniform buffer",
			.byteSize = sizeof(DeferredUBO),
			.usage = Usage::UNIFORM_BUFFER,
			.memory = Memory::UPLOAD
		});
		bz::deferredBuffers[i].Map(bz::device);
	}
}

void CreateRenderAttachments() {
	bz::depth = Texture::Create(bz::device, {
		.width = bz::swapchain.extent.width,
		.height = bz::swapchain.extent.height,
		.format = Format::D24_UNORM_S8_UINT,
		.usage = Usage::DEPTH_STENCIL | Usage::SHADER_RESOURCE
	});

	bz::albedo = Texture::Create(bz::device, {
		.width = bz::swapchain.extent.width,
		.height = bz::swapchain.extent.height,
		.format = Format::RGBA8_UNORM,
		.usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
	});

	bz::normal = Texture::Create(bz::device, {
		.width = bz::swapchain.extent.width,
		.height = bz::swapchain.extent.height,
		.format = Format::RGBA8_UNORM,
		.usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
	});

	bz::metallicRoughness = Texture::Create(bz::device, {
		.width = bz::swapchain.extent.width,
		.height = bz::swapchain.extent.height,
		.format = Format::RGBA8_UNORM,
		.usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
	});
}

void CleanupRenderAttachments() {
	bz::depth.Destroy(bz::device);
	bz::normal.Destroy(bz::device);
	bz::albedo.Destroy(bz::device);
	bz::metallicRoughness.Destroy(bz::device);
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

	VkRenderingAttachmentInfo colorAttachments[] = { bz::albedo.GetAttachmentInfo(), bz::normal.GetAttachmentInfo(), bz::metallicRoughness.GetAttachmentInfo() };
	VkRenderingAttachmentInfo depthAttachment[] = { bz::depth.GetAttachmentInfo() };

	// OFFSCREEN PASS
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

	ImageBarrier(cmd, bz::metallicRoughness.image,			VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,						VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::offscreenPipeline.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::offscreenPipeline.pipelineLayout, 0, 1, &bz::globalsBindings[currentFrame].descriptorSet, 0, nullptr);

	vkCmdBeginRendering(cmd, &renderingInfo);

	model->Draw(cmd, bz::offscreenPipeline.pipelineLayout);
	plane->Draw(cmd, bz::offscreenPipeline.pipelineLayout);

	vkCmdEndRendering(cmd);

	// DEFERRED PASS
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

	ImageBarrier(cmd, bz::metallicRoughness.image, VK_IMAGE_ASPECT_COLOR_BIT,
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
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::deferredPipeline.pipelineLayout, 0, 1, &bz::deferredBindings[currentFrame].descriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::deferredPipeline.pipelineLayout, 1, 1, &bz::gbufferBindings.descriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, bz::deferredPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(u32), &bz::renderMode);

	vkCmdBeginRendering(cmd, &renderingInfo);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRendering(cmd);

	// SKYBOX PASS
	// Transition depth back to attachment optimal
	depthAttachment[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	ImageBarrier(cmd, bz::depth.image,				VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,		VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
		VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	// Then render the skybox at infinite distance (depth = 0)
	renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
			.offset = {0, 0},
			.extent = bz::swapchain.extent
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &bz::swapchain.attachmentInfos[imageIndex],
		.pDepthAttachment = depthAttachment,
		.pStencilAttachment = depthAttachment
	};

	vkCmdBeginRendering(cmd, &renderingInfo);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::skyboxPipeline.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::skyboxPipeline.pipelineLayout, 0, 1, &bz::globalsBindings[currentFrame].descriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::skyboxPipeline.pipelineLayout, 1, 1, &bz::skyboxBindGroup.descriptorSet, 0, nullptr);

	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(cmd, 0, 1, &cube->vertices.buffer, offsets);
	vkCmdBindIndexBuffer(cmd, cube->indices.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, cube->nodes[0]->children[0]->mesh.primitives[0].indexCount, 1, cube->nodes[0]->children[0]->mesh.primitives[0].firstIndex, 0, 0);

	vkCmdEndRendering(cmd);

	// UI OVERLAY PASS
	bz::overlay->Draw(cmd, bz::swapchain.extent, bz::swapchain.attachmentInfos[imageIndex]);

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

	bz::skyboxLayout = BindGroupLayout::Create(bz::device, {
		{.binding = 0, .type = Binding::TEXTURE, .stages = Binding::FRAGMENT }
	});
}

void CreateBindGroups() {
	bz::gbufferBindings = BindGroup::Create(bz::device, bz::materialLayout, {
		.textures = {
			bz::albedo.GetBinding(0), bz::normal.GetBinding(1), bz::metallicRoughness.GetBinding(2), bz::depth.GetBinding(3)
			//{.binding = 0, .sampler = bz::attachmentSampler, .view = bz::albedo.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			//{.binding = 1, .sampler = bz::attachmentSampler, .view = bz::normal.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			//{.binding = 2, .sampler = bz::attachmentSampler, .view = bz::metallicRoughness.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			//{.binding = 3, .sampler = bz::attachmentSampler, .view = bz::depth.depthView, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL }
		}
	});

	for (u32 i = 0; i < arraysize(bz::globalsBindings); i++) {
		bz::globalsBindings[i] = BindGroup::Create(bz::device, bz::globalsLayout, {
			.buffers = { {.binding = 0, .buffer = bz::uniformBuffers[i].buffer, .offset = 0, .size = sizeof(CameraUBO)}}
		});
	}

	for (u32 i = 0; i < arraysize(bz::deferredBindings); i++) {
		bz::deferredBindings[i] = BindGroup::Create(bz::device, bz::globalsLayout, {
			.buffers = { {.binding = 0, .buffer = bz::deferredBuffers[i].buffer, .offset = 0, .size = sizeof(DeferredUBO)}}
		});
	}
}

void UpdateGBufferBindGroup() {
	bz::gbufferBindings.Update(bz::device, {
		.textures = {
			bz::albedo.GetBinding(0), bz::normal.GetBinding(1), bz::metallicRoughness.GetBinding(2), bz::depth.GetBinding(3)
			//{.binding = 0, .sampler = bz::attachmentSampler, .view = bz::albedo.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			//{.binding = 1, .sampler = bz::attachmentSampler, .view = bz::normal.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			//{.binding = 2, .sampler = bz::attachmentSampler, .view = bz::metallicRoughness.view, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
			//{.binding = 3, .sampler = bz::attachmentSampler, .view = bz::depth.depthView, .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL }
		}
	});
}

void CreatePipelines() {
	Shader offscreenVert = Shader::Create(bz::device, "shaders/offscreen.vert.spv");
	Shader offscreenFrag = Shader::Create(bz::device, "shaders/offscreen.frag.spv");

	bz::offscreenPipeline = Pipeline::Create(bz::device, VK_PIPELINE_BIND_POINT_GRAPHICS, {
		.debugName = "Offscreen pipeline",
		.shaders = { offscreenVert, offscreenFrag },
		.bindGroups = { bz::globalsLayout, bz::materialLayout },
		.graphicsState = {
			.attachments = {
				.formats = { bz::albedo.format, bz::normal.format, bz::metallicRoughness.format },
				.depthStencilFormat = bz::depth.format
			},
			.rasterization = {
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
			},
			.sampleCount = VK_SAMPLE_COUNT_1_BIT,
			.vertexInput = {
				.bindingDesc = GLTFModel::Vertex::BindingDescripton,
				.attributeDesc = GLTFModel::Vertex::AttributeDescription
			}
		}
	});

	offscreenVert.Destroy(bz::device);
	offscreenFrag.Destroy(bz::device);

	Shader deferredVert = Shader::Create(bz::device, "shaders/deferred.vert.spv");
	Shader deferredFrag = Shader::Create(bz::device, "shaders/deferred.frag.spv");

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
			.sampleCount = VK_SAMPLE_COUNT_1_BIT
		}
	});

	deferredVert.Destroy(bz::device);
	deferredFrag.Destroy(bz::device);

	Shader skyboxVert = Shader::Create(bz::device, "shaders/skybox.vert.spv");
	Shader skyboxFrag = Shader::Create(bz::device, "shaders/skybox.frag.spv");

	bz::skyboxPipeline = Pipeline::Create(bz::device, VK_PIPELINE_BIND_POINT_GRAPHICS, {
		.debugName = "Skybox pipeline",
		.shaders = { skyboxVert, skyboxFrag },
		.bindGroups = { bz::globalsLayout, bz::skyboxLayout },
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
			.vertexInput = {
				.bindingDesc = GLTFModel::Vertex::BindingDescripton,
				.attributeDesc = GLTFModel::Vertex::AttributeDescription
			}
		}
	});

	skyboxVert.Destroy(bz::device);
	skyboxFrag.Destroy(bz::device);
}

void InitVulkan() {
	bz::device.CreateDevice(window);
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
		bz::uniformBuffers[i].Destroy(bz::device);
	}

	for (int i = 0; i < arraysize(bz::deferredBuffers); i++) {
		bz::deferredBuffers[i].Destroy(bz::device);
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
	CameraUBO cameraUBO = {
		.view = bz::camera.view,
		.proj = bz::camera.projection,
		.camPos = bz::camera.position
	};

	memcpy(bz::uniformBuffers[currentImage].mapped, &cameraUBO, sizeof(cameraUBO));

	DeferredUBO deferredUBO = {
		.view = bz::camera.view,
		.invProj = glm::inverse(bz::camera.projection),
		.camPos = glm::vec4(bz::camera.position, 1.0f),
		.pointLightCount = 3,
		.dirLight = bz::dirLight,
		.pointLights = {
			bz::pointLightR,
			bz::pointLightG,
			bz::pointLightB
		}
	};

	memcpy(bz::deferredBuffers[currentImage].mapped, &deferredUBO, sizeof(deferredUBO));
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

void OverlayRender() {
	ImGui::Begin("Bozo Engine", 0, 0);
	bz::overlay->ShowFrameTimeGraph();

	ImGui::SeparatorText("Directional Light settings");
	ImGui::Checkbox("Animate light", &bz::bAnimateLight);
	ImGui::ColorEdit3("Ambient", &bz::dirLight.ambient.x, ImGuiColorEditFlags_Float);
	ImGui::ColorEdit3("Diffuse", &bz::dirLight.diffuse.x, ImGuiColorEditFlags_Float);
	ImGui::ColorEdit3("Specular", &bz::dirLight.specular.x, ImGuiColorEditFlags_Float);

	if (ImGui::CollapsingHeader("Render Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::BeginTable("split", 2);

		ImGui::TableNextColumn(); if (ImGui::RadioButton("Deferred", bz::renderMode == 0))			{ bz::renderMode = 0; }
		ImGui::TableNextColumn();
		ImGui::TableNextColumn(); if (ImGui::RadioButton("Albedo", bz::renderMode == 1))			{ bz::renderMode = 1; }
		ImGui::TableNextColumn(); if (ImGui::RadioButton("Normal", bz::renderMode == 2))			{ bz::renderMode = 2; }
		ImGui::TableNextColumn(); if (ImGui::RadioButton("Metallic/Roughness", bz::renderMode == 3)){ bz::renderMode = 3; }
		ImGui::TableNextColumn(); if (ImGui::RadioButton("Depth", bz::renderMode == 4))				{ bz::renderMode = 4; }

		ImGui::EndTable();
	}

	if (ImGui::CollapsingHeader("Parallax Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::RadioButton("Disable", bz::parallaxMode == 0)) { bz::parallaxMode = 0; }
		if (ImGui::RadioButton("Simple Parallax Mapping", bz::parallaxMode == 1)) { bz::parallaxMode = 1; }
		if (ImGui::RadioButton("FGED Parallax Mapping", bz::parallaxMode == 2)) { bz::parallaxMode = 2; }
		if (ImGui::RadioButton("Steep Parallax Mapping", bz::parallaxMode == 3)) { bz::parallaxMode = 3; }
		if (ImGui::RadioButton("Parallax Occlusion Mapping", bz::parallaxMode == 4)) { bz::parallaxMode = 4; }

		if (bz::parallaxMode) {
			ImGui::SliderFloat("Scale", &bz::parallaxScale, 0.001f, 0.1f);
			if (bz::parallaxMode > 1) {
				const u32 minSteps = 8;
				const u32 maxSteps = 64;
				ImGui::SliderScalar("Steps", ImGuiDataType_U32, &bz::parallaxSteps, &minSteps, &maxSteps);
			}
		}
	}

	ImGui::SeparatorText("Shader hot-reload");
	if (ImGui::Button("Reload shaders")) {
		vkDeviceWaitIdle(bz::device.logicalDevice);
		bz::offscreenPipeline.Destroy(bz::device);
		bz::deferredPipeline.Destroy(bz::device);
		bz::skyboxPipeline.Destroy(bz::device);
		CreatePipelines();
	}

	ImGui::End();
}

int main(int argc, char* argv[]) {
	InitWindow(WIDTH, HEIGHT);
	InitVulkan();

	bz::overlay = new UIOverlay(window, bz::device, bz::swapchain.format, bz::depth.format, OverlayRender);

	bz::skybox = Texture::CreateCubemap(bz::device, Format::RGBA8_UNORM, Memory::DEFAULT, Usage::SHADER_RESOURCE, {
		"assets/Skybox/right.jpg",
		"assets/Skybox/left.jpg",
		"assets/Skybox/top.jpg",
		"assets/Skybox/bottom.jpg",
		"assets/Skybox/front.jpg",
		"assets/Skybox/back.jpg"
	});

	bz::skyboxBindGroup = BindGroup::Create(bz::device, bz::skyboxLayout, {
		.textures = { bz::skybox.GetBinding(0) }
	});

	cube = new GLTFModel(bz::device, bz::materialLayout, "assets/Box.glb");

	//model = new GLTFModel(bz::device, bz::materialLayout, "assets/FlightHelmet/FlightHelmet.gltf");
	//model->nodes[0]->transform = glm::scale(glm::translate(model->nodes[0]->transform, glm::vec3(0.0, 1.0, 0.0)), glm::vec3(2.0));
	model = new GLTFModel(bz::device, bz::materialLayout, "assets/Sponza/Sponza.gltf");

	plane = new GLTFModel(bz::device, bz::materialLayout, "assets/ParallaxTest/plane.gltf");
	{
		plane->images.resize(3);
		plane->images[1] = Texture::Create(bz::device, "assets/ParallaxTest/rocks_color_rgba.png", { 
			.generateMipLevels = true,
			.format = Format::RGBA8_SRGB,
			.usage = Usage::SHADER_RESOURCE
		});

		plane->images[2] = Texture::Create(bz::device, "assets/ParallaxTest/rocks_normal_height_rgba.png", { 
			.generateMipLevels = true,
			.format = Format::RGBA8_UNORM,
			.usage = Usage::SHADER_RESOURCE
		});

		plane->materials.push_back({
			.albedo = &plane->images[1],
			.normal = &plane->images[2],
			.metallicRoughness = &plane->images[0],
			.bindGroup = BindGroup::Create(bz::device, bz::materialLayout, {
				.textures = { plane->images[1].GetBinding(0), plane->images[2].GetBinding(1), plane->images[0].GetBinding(2) }
			})
		});

		plane->nodes[0]->mesh.primitives[0].materialIndex = 0;
		plane->nodes[0]->transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.2f));
	}

	double lastFrame = 0.0f;
	while (!glfwWindowShouldClose(window)) {
		double currentFrame = glfwGetTime();
		float deltaTime = float(currentFrame - lastFrame);
		lastFrame = currentFrame;

		bz::overlay->frameTimeHistory.Post(deltaTime);

		if (bz::bAnimateLight) {
			float t = float(currentFrame * 0.5);
			bz::dirLight.direction = glm::vec3(glm::cos(t), -1.0f, glm::sin(t));

			bz::pointLightR.position = glm::vec3(-2.0f, glm::cos(2.0f * t) + 1.0f, 2.0f);
			bz::pointLightG.position = glm::vec3(2.0f, 0.25f, 0.0f);
			bz::pointLightB.position = glm::vec3(glm::cos(4.0f * t), 0.25f, -2.0f);
		}

		plane->materials[0].parallaxMode = bz::parallaxMode;
		plane->materials[0].parallaxSteps = bz::parallaxSteps;
		plane->materials[0].parallaxScale = bz::parallaxScale;

		bz::camera.Update(deltaTime);
		bz::overlay->Update();

		DrawFrame();

		glfwPollEvents();
	}

	// Wait until all commandbuffers are done so we can safely clean up semaphores they might potentially be using.
	vkDeviceWaitIdle(bz::device.logicalDevice);

	bz::skybox.Destroy(bz::device);
	bz::skyboxPipeline.Destroy(bz::device);
	bz::skyboxLayout.Destroy(bz::device);

	delete plane;
	delete model;
	delete cube;
	
	delete bz::overlay;

	bz::offscreenPipeline.Destroy(bz::device);
	bz::deferredPipeline.Destroy(bz::device);
	bz::globalsLayout.Destroy(bz::device);
	bz::materialLayout.Destroy(bz::device);

	CleanupVulkan();
	CleanupWindow();

	return 0;
}