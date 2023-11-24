#include "Device.h"
#include "Swapchain.h"
#include "Texture.h"
#include "Camera.h"
#include "Tools.h"

#include "GLTF.h"
#include "UIOverlay.h"
#include <imgui.h>

#include "Pipeline.h"

// TODO:
// Simplify updatecascades
// Move ubo updates to separate function
// Improve shadow filtering, blurring. See tardif
// 
// Check shadows beyond the last cascade - how should this be handled?
// Record videos of artifacts before improving shadows further

constexpr u32 WIDTH = 1600;
constexpr u32 HEIGHT = 900;

GLTFModel* model;
GLTFModel* lightpoles;
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

struct CascadedShadowMap {
	static const int max_cascades = 4;
	const Device& device;
	const Camera& camera;

	const u32 n = 1 << 12;		// Shadow map resolution

	// TODO: should probably move away from the "view" matrix naming
	// Just use	worldToCascade/cascadeToWorld naming. Same goes for the camera.
	struct Cascade {
		glm::vec3 v0, v1, v2, v3;	// near plane of the cascade portion of the camera frustum
		glm::vec3 v4, v5, v6, v7;	// far  plane of the cascade portion of the camera frustum

		// Diameter of the cascade portion of the camera frustum
		float d;

		// Light-space bounding box of cascade
		float xmin, ymin, zmin;
		float xmax, ymax, zmax;

		glm::mat4 cascadeInvView;	// world to cascade camera space
		glm::mat4 cascadeProj;		// cascade projection matrix
		glm::mat4 viewProj;			// cascadeProj * cascadeInvView
	} cascades[max_cascades];

	struct ShadowData {
		alignas(16) glm::vec4 a;
		alignas(16) glm::vec4 b;
		alignas(16) glm::mat4 shadowMat;
		alignas(16) glm::vec4 cascadeScales[max_cascades - 1];
		alignas(16) glm::vec4 cascadeOffsets[max_cascades - 1];
		alignas(16) glm::vec4 shadowOffsets[2];
	} shadowData;

	Buffer cascadeUBO[MAX_FRAMES_IN_FLIGHT];
	BindGroupLayout cascadeBindGroupLayout;
	BindGroup cascadeBindGroup[MAX_FRAMES_IN_FLIGHT];

	BindGroupLayout shadowBindGroupLayout;
	BindGroup shadowBindGroup;

	Texture shadowMap;
	Pipeline pipeline;

	CascadedShadowMap(const Device& device, const Camera& camera, span<const glm::vec2> distances) : device{ device }, camera{ camera } {
		InitCascades(distances);
		InitVulkanResources();
	}

	~CascadedShadowMap() {
		pipeline.Destroy(device);
		
		for (auto& ubo : cascadeUBO)
			ubo.Destroy(device);

		shadowBindGroupLayout.Destroy(device);
		cascadeBindGroupLayout.Destroy(device);
		shadowMap.Destroy(device);
	}

	void InitVulkanResources() {
		shadowMap = Texture::Create(device, {
			.type = TextureDesc::Type::TEXTURE2DARRAY,
			.width = n,
			.height = n,
			.arrayLayers = max_cascades,
			.format = Format::D32_SFLOAT,
			.usage = Usage::DEPTH_STENCIL | Usage::SHADER_RESOURCE,
			.sampler = { true, VK_COMPARE_OP_GREATER } // Enable depth comparisons on the sampler
		});

		shadowBindGroupLayout = BindGroupLayout::Create(device, {
			{.binding = 0, .type = Binding::TEXTURE, .stages = ShaderStage::FRAGMENT}
		});

		shadowBindGroup = BindGroup::Create(device, shadowBindGroupLayout, {
			.textures = { shadowMap.GetBinding(0) }
		});

		cascadeBindGroupLayout = BindGroupLayout::Create(device, {
			{.binding = 0, .type = Binding::BUFFER_DYNAMIC }
		});

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			cascadeUBO[i] = Buffer::Create(device, {
				.debugName = "CSM cascade viewProj matrix",
				.byteSize = glm::max(sizeof(glm::mat4), 256ull) * max_cascades,
				.usage = Usage::UNIFORM_BUFFER,
				.memory = Memory::UPLOAD
			});
			cascadeUBO[i].Map(device);

			cascadeBindGroup[i] = BindGroup::Create(device, cascadeBindGroupLayout, {
				.buffers = { cascadeUBO[i].GetBinding(0, sizeof(glm::mat4)) }
			});
		}

		Shader shadowMapVert = Shader::Create(device, "shaders/shadowMap.vert.spv");

		pipeline = Pipeline::Create(device, VK_PIPELINE_BIND_POINT_GRAPHICS, {
			.debugName = "Shadow map pipeline",
			.shaders = { shadowMapVert },
			.bindGroups = { cascadeBindGroupLayout },
			.graphicsState = {
				.attachments = { .depthStencilFormat = shadowMap.format },
				.rasterization = {
					.depthClampEnable = VK_TRUE,
					.cullMode = VK_CULL_MODE_BACK_BIT,
					.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
					.depthBiasEnable = VK_TRUE,
					.depthBiasConstantFactor = -2.0f,	// TODO: Play around with these
					.depthBiasClamp = -1.0f / 128.0f,	// a bit to see if they can be
					.depthBiasSlopeFactor = -3.0f		// improved + get a better feel
				},
				.vertexInput = {
					.bindingDesc = GLTFModel::Vertex::BindingDescripton,
					.attributeDesc = { { .format = VK_FORMAT_R32G32B32_SFLOAT } }
				}
			}
		});

		shadowMapVert.Destroy(device);
	}

	void InitCascades(span<const glm::vec2> distances) {
		Check(distances.size() == max_cascades, "All %i cascade distances must be specified", max_cascades);

		// Offsets for shadow samples.
		float d = 3.0f / (16.0f * n);
		shadowData.shadowOffsets[0] = glm::vec4(glm::vec2(-d,-3*d), glm::vec2( 3*d,-d));
		shadowData.shadowOffsets[1] = glm::vec4(glm::vec2( d, 3*d), glm::vec2(-3*d, d));

		const float s = camera.aspect;
		const float g = 1.0f / glm::tan(glm::radians(camera.fov) * 0.5f);

		for (int k = 0; k < max_cascades; k++) {
			const float a = distances[k].x;
			const float b = distances[k].y;

			// Save cascade distances; they are used in the shader for cascade transitions.
			shadowData.a[k] = a;
			shadowData.b[k] = b;

			// Calculate the eight view-space frustum coordinates of the cascade
			cascades[k] = {
				.v0 = glm::vec3( a * s / g, -a / g, a),
				.v1 = glm::vec3( a * s / g,  a / g, a),
				.v2 = glm::vec3(-a * s / g,  a / g, a),
				.v3 = glm::vec3(-a * s / g, -a / g, a),

				.v4 = glm::vec3( b * s / g, -b / g, b),
				.v5 = glm::vec3( b * s / g,  b / g, b),
				.v6 = glm::vec3(-b * s / g,  b / g, b),
				.v7 = glm::vec3(-b * s / g, -b / g, b),
			};

			// Calculate shadow map size (diameter of the cascade)
			cascades[k].d = glm::ceil(glm::max(
				glm::length(cascades[k].v0 - cascades[k].v6),
				glm::length(cascades[k].v4 - cascades[k].v6)));
		}
	}

	void RenderShadowMap(VkCommandBuffer cmd) {
		// Transition shadow map to attachment optimal
		VkImageSubresourceRange subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = VK_REMAINING_ARRAY_LAYERS
		};

		ImageBarrier(cmd, shadowMap.image, subresourceRange,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,	VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,						VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

		VkViewport viewport = {
			.x = 0.0f,			.y = 0.0f,
			.width = (float)n,	.height = (float)n,
			.minDepth = 0.0f,	.maxDepth = 1.0f
		};

		VkRect2D scissor = { .offset = { 0, 0 }, .extent = {n, n} };

		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);

		// Render one cascade (layer) at a time
		VkRenderingInfo renderingInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = scissor,
			.layerCount = 1
		};

		for (u32 k = 0; k < max_cascades; k++) {
			// TODO: dont like relying on the global currentFrame int in here
			// TODO: 256 is the max possible minUniformBufferOffsetAlignment. Use device.properties.minUniformBufferOffsetAlignment instead.
			u32 dynamicOffset = k * 256;
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipelineLayout, 0, 1, &cascadeBindGroup[currentFrame].descriptorSet, 1, &dynamicOffset);

			VkRenderingAttachmentInfo cascadeAttachment = shadowMap.GetAttachmentInfo(k);
			renderingInfo.pDepthAttachment = &cascadeAttachment;

			vkCmdBeginRendering(cmd, &renderingInfo);

			model->Draw(cmd, pipeline, true);
			plane->Draw(cmd, pipeline, true);
			lightpoles->Draw(cmd, pipeline, true);

			vkCmdEndRendering(cmd);
		}

		ImageBarrier(cmd, shadowMap.image, subresourceRange,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
			| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,	VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
	}

	void UpdateCascades(glm::vec3 lightDir) {
		// Calculate light matrix from light direction. (This breaks when x,z are zero)
		const glm::vec3 z = -glm::normalize(lightDir);
		const glm::vec3 x = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), z));
		const glm::vec3 y = glm::cross(x, z);
		const glm::mat4 light = glm::mat3(x, y, z);

		// Camera space to light space matrix
		const glm::mat4 L = glm::inverse(light) * glm::inverse(camera.view);

		for (int k = 0; k < max_cascades; k++) {
			// Transform cascade frustum points from camera view space to light space
			const glm::vec4 Lv[8] = {
				L * glm::vec4(cascades[k].v0, 1.0f),
				L * glm::vec4(cascades[k].v1, 1.0f),
				L * glm::vec4(cascades[k].v2, 1.0f),
				L * glm::vec4(cascades[k].v3, 1.0f),

				L * glm::vec4(cascades[k].v4, 1.0f),
				L * glm::vec4(cascades[k].v5, 1.0f),
				L * glm::vec4(cascades[k].v6, 1.0f),
				L * glm::vec4(cascades[k].v7, 1.0f)
			};

			// Find the light-space bounding box of cascade frustum
			cascades[k].xmin = Lv[0].x;
			cascades[k].ymin = Lv[0].y;
			cascades[k].zmin = Lv[0].z;
			cascades[k].xmax = Lv[0].x;
			cascades[k].ymax = Lv[0].y;
			cascades[k].zmax = Lv[0].z;
			for (const glm::vec4 Lvi : Lv) {
				cascades[k].xmin = glm::min(cascades[k].xmin, Lvi.x);
				cascades[k].ymin = glm::min(cascades[k].ymin, Lvi.y);
				cascades[k].zmin = glm::min(cascades[k].zmin, Lvi.z);

				cascades[k].xmax = glm::max(cascades[k].xmax, Lvi.x);
				cascades[k].ymax = glm::max(cascades[k].ymax, Lvi.y);
				cascades[k].zmax = glm::max(cascades[k].zmax, Lvi.z);
			}

			// Calculate the physical size of shadow map texels
			const float T = cascades[k].d / n;

			// Shadow edges are stable if the viewport coordinates of each vertex belonging to an object rendered into the shadow map
			// have *constant fractional parts*. (Triangle are rasterized identically if moved by an integral number of texels).
			// Because the distance between adjacent texels in viewport space corresponds to the physical distance T, changing the
			// camera's x/y position by a multiple of T preserves the fractional positions of the vertices. To achieve shadow stability,
			// we thus require that the light-space x and y coordinates of the camera position always are integral multiples of T.
			//	
			//	Note: For this calculation to be completely effective, T must be exactly representable as a floating point number.
			//        We thus have to make sure that n is always a power of 2. This is also why take the ceiling of d.
			
			// Calculate light-space coordinates of the camera position we will be rendering the shadow map cascade from.
			const glm::vec3 s = glm::vec3(glm::floor((cascades[k].xmax + cascades[k].xmin) / (T * 2.0f)) * T, 
										  glm::floor((cascades[k].ymax + cascades[k].ymin) / (T * 2.0f)) * T, 
										  cascades[k].zmin);

			// The cascade camera space to world space matrix can be found by:
			// M    = [ light[0] | light[1] | light[2] | light * s ]
			// As we only need the inverse (world to cascade camera space), we instead calculate:
			// M^-1 = [ light^T[0] | light^T[1] | light^T[2] | -s ]
			// This assumes the upper 3x3 matrix of light is orthogonal and that the translation component is zero.
			const glm::mat4 lightT = glm::transpose(light);
			cascades[k].cascadeInvView = glm::mat4(lightT[0], lightT[1], lightT[2], glm::vec4(-s, 1.0f));

			// Calculate the cascade projection matrix
			const float d = cascades[k].d;
			const float zd = cascades[k].zmax - cascades[k].zmin;
			cascades[k].cascadeProj = glm::mat4(
				2.0f / d,	0.0f,		0.0f,		0.0f,
				0.0f,		2.0f / d,	0.0f,		0.0f,
				0.0f,		0.0f,		1.0f / zd,	0.0f,
				0.0f,		0.0f,		0.0f,		1.0f);

			// Calculate the view projection matrix of the cascade: P_cascade * (M_cascade)^-1 
			// Full MVP matrix is then cascade.viewProj * M_object
			cascades[k].viewProj = cascades[k].cascadeProj * cascades[k].cascadeInvView;

			// Update cascade ubo...
			// TODO: 256 is the max possible minUniformBufferOffsetAlignment. Use device.properties.minUniformBufferOffsetAlignment instead.
			memcpy(cascadeUBO[currentFrame].mapped + k * 256, &cascades[k].viewProj, sizeof(glm::mat4));
		}

		// Calculate world to shadow map texture coordinates texture.
		const float d0 = cascades[0].d;
		const float zd0 = cascades[0].zmax - cascades[0].zmin;
		const glm::mat4 shadowProj = glm::mat4(
			1.0f / d0,	0.0f,		0.0f,		0.0f,
			0.0f,		1.0f / d0,	0.0f,		0.0f,
			0.0f,		0.0f,		1.0f / zd0,	0.0f, 
			0.5f,		0.5f,		0.0f,		1.0f
		);

		shadowData.shadowMat = shadowProj * cascades[0].cascadeInvView;

		// We only calculate the shadow matrix for cascade 0.
		// To convert the texture coordinates between cascades, we just use some scales and offsets.
		for (int k = 1; k < max_cascades; k++) {
			const float dk = cascades[k].d;
			const float zdk = cascades[k].zmax - cascades[k].zmin;

			const glm::vec3 s0 = -cascades[0].cascadeInvView[3];
			const glm::vec3 sk = -cascades[k].cascadeInvView[3];

			shadowData.cascadeScales[k - 1] = glm::vec4(d0 / dk, d0 / dk, zd0 / zdk, 0.0f);

			shadowData.cascadeOffsets[k - 1].x = (s0.x - sk.x) / dk - d0 / (2.0f * dk) + 0.5f;
			shadowData.cascadeOffsets[k - 1].y = (s0.y - sk.y) / dk - d0 / (2.0f * dk) + 0.5f;
			shadowData.cascadeOffsets[k - 1].z = (s0.z - sk.z) / zdk;
		}
	}
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

	// TODO: is this the appropriate place to pass the shadow data?
	alignas(16) CascadedShadowMap::ShadowData shadowData;

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
	Camera camera(glm::vec3(0.0f, 1.5f, 1.0f), 1.0f, 60.0f, (float)WIDTH / HEIGHT, 0.01f, 0.0f, -30.0f);

	Texture   skybox;
	BindGroup skyboxBindGroup;

	CascadedShadowMap* shadowMap;

	bool bAnimateLight = false;
	DirectionalLight dirLight = {
		.direction = glm::vec3(1.0f, -1.0f, -0.2f),
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
	Pipeline offscreenPipeline, skyboxPipeline, deferredPipeline, shadowMapPipeline;

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
	VkViewport viewport = {
		.x = 0.0f, .y = 0.0f,
		.width = (float)bz::swapchain.extent.width,
		.height = (float)bz::swapchain.extent.height,
		.minDepth = 0.0f, .maxDepth = 1.0f
	};
	VkRect2D scissor = { .offset = { 0, 0 }, .extent = bz::swapchain.extent };

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

	model->Draw(cmd, bz::offscreenPipeline);
	plane->Draw(cmd, bz::offscreenPipeline);
	lightpoles->Draw(cmd, bz::offscreenPipeline);

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
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bz::deferredPipeline.pipelineLayout, 2, 1, &bz::shadowMap->shadowBindGroup.descriptorSet, 0, nullptr);
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
		{.binding = 0, .type = Binding::TEXTURE, .stages = ShaderStage::FRAGMENT },
		{.binding = 1, .type = Binding::TEXTURE, .stages = ShaderStage::FRAGMENT },
		{.binding = 2, .type = Binding::TEXTURE, .stages = ShaderStage::FRAGMENT },
		{.binding = 3, .type = Binding::TEXTURE, .stages = ShaderStage::FRAGMENT }
	});

	bz::globalsLayout = BindGroupLayout::Create(bz::device, {
		{.binding = 0, .type = Binding::BUFFER }
	});

	bz::skyboxLayout = BindGroupLayout::Create(bz::device, {
		{.binding = 0, .type = Binding::TEXTURE, .stages = ShaderStage::FRAGMENT }
	});
}

void CreateBindGroups() {
	bz::gbufferBindings = BindGroup::Create(bz::device, bz::materialLayout, {
		.textures = {
			bz::albedo.GetBinding(0), bz::normal.GetBinding(1), bz::metallicRoughness.GetBinding(2), bz::depth.GetBinding(3)
		}
	});

	for (u32 i = 0; i < arraysize(bz::globalsBindings); i++) {
		bz::globalsBindings[i] = BindGroup::Create(bz::device, bz::globalsLayout, {
			.buffers = { bz::uniformBuffers[i].GetBinding(0, sizeof(CameraUBO)) }
		});
	}

	for (u32 i = 0; i < arraysize(bz::deferredBindings); i++) {
		bz::deferredBindings[i] = BindGroup::Create(bz::device, bz::globalsLayout, {
			.buffers = { bz::deferredBuffers[i].GetBinding(0, sizeof(DeferredUBO)) }
		});
	}
}

void UpdateGBufferBindGroup() {
	bz::gbufferBindings.Update(bz::device, {
		.textures = {
			bz::albedo.GetBinding(0), bz::normal.GetBinding(1), bz::metallicRoughness.GetBinding(2), bz::depth.GetBinding(3)
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
		.bindGroups = { bz::globalsLayout, bz::materialLayout, bz::shadowMap->shadowBindGroupLayout },
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

	bz::shadowMap = new CascadedShadowMap(bz::device, bz::camera, { {0.0f, 3.0f}, {2.5f, 12.0f}, {11.0f, 32.0f}, {30.0f, 128.0f} });

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
		.shadowData = bz::shadowMap->shadowData,
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

	// UpdateCascades overwrites ubo memory so it has to be called on this side of the fence.
	// Might be a good idea to separate this function into
	//	- UpdateCascades		Updates cascade matrices in ShadowMap class, etc.
	//	- UpdateGPUResources	Copies the updated matrices to the UBO.
	bz::shadowMap->UpdateCascades(bz::dirLight.direction);

	UpdateUniformBuffer(currentFrame);

	// Only reset the fence if we swapchain was valid and we are actually submitting work.
	VkCheck(vkResetFences(bz::device.logicalDevice, 1, &bz::renderFrames[currentFrame].inFlight), "Failed to reset inFlight fence");

	VkCheck(vkResetCommandPool(bz::device.logicalDevice, bz::renderFrames[currentFrame].commandPool, 0), "Failed to reset frame command pool");

	VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VkCheck(vkBeginCommandBuffer(bz::renderFrames[currentFrame].commandBuffer, &beginInfo), "Failed to begin recording command buffer!");

	bz::shadowMap->RenderShadowMap(bz::renderFrames[currentFrame].commandBuffer);

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

	ImGui::SliderFloat3("dir", &bz::dirLight.direction[0], -1.0f, 1.0f);
	
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

	lightpoles = new GLTFModel(bz::device, bz::materialLayout, "assets/Lightpoles.glb");
	lightpoles->nodes[0]->transform = glm::scale(lightpoles->nodes[0]->transform, glm::vec3(0.4f));

	plane = new GLTFModel(bz::device, bz::materialLayout, "assets/ParallaxTest/plane.gltf");
	{
		const char* albedo = 1 ? "assets/Sponza/5823059166183034438.jpg"  : "assets/ParallaxTest/rocks_color_rgba.png";
		const char* normal = 1 ? "assets/Sponza/14267839433702832875.jpg" : "assets/ParallaxTest/rocks_normal_height_rgba.png";

		plane->images.resize(3);

		plane->images[1] = Texture::Create(bz::device, albedo, {
			.format = Format::RGBA8_SRGB,
			.usage = Usage::SHADER_RESOURCE,
			.generateMipLevels = true,
			});

		plane->images[2] = Texture::Create(bz::device, normal, {
			.format = Format::RGBA8_UNORM,
			.usage = Usage::SHADER_RESOURCE,
			.generateMipLevels = true,
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
		//plane->nodes[0]->transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.2f));
	}

	HANDLE shaderDir = FindFirstChangeNotificationA("shaders", false, FILE_NOTIFY_CHANGE_LAST_WRITE);
	Check(shaderDir != INVALID_HANDLE_VALUE, "FindFirstChangeNotification failed to create a notification handle for the shaders folder.");

	double lastFrame = 0.0f;
	while (!glfwWindowShouldClose(window)) {
		double currentFrame = glfwGetTime();
		float deltaTime = float(currentFrame - lastFrame);
		lastFrame = currentFrame;

		// If any of the files in the shader directory changed, reload all shaders.
		// Should call ReadDirectoryChangesW and only reload the specific shader.
		// For now, manually rebuilding everything every time is okay...
		if (WaitForSingleObject(shaderDir, 0) == WAIT_OBJECT_0) {
			system(".\\shaders\\build_shaders.bat");

			// build_shaders.bat will modify directory again. Ignore it.
			FindNextChangeNotification(shaderDir);

			vkDeviceWaitIdle(bz::device.logicalDevice);
			bz::offscreenPipeline.Destroy(bz::device);
			bz::deferredPipeline.Destroy(bz::device);
			bz::skyboxPipeline.Destroy(bz::device);
			CreatePipelines();

			// Request that shaderDir is signaled when the directory is changed again.
			FindNextChangeNotification(shaderDir);
		}

		bz::overlay->frameTimeHistory.Post(deltaTime);

		if (bz::bAnimateLight) {
			float t = float(currentFrame * 0.5);
			bz::dirLight.direction = glm::vec3(glm::cos(t), -1.0f, 0.3f * glm::sin(t));

			//bz::dirLight.direction = glm::vec3(0.5f, -0.5f, -1.0f);

			bz::pointLightR.position = glm::vec3(-2.0f, glm::cos(2.0f * t) + 1.0f, 2.0f);
			bz::pointLightG.position = glm::vec3(2.0f, 0.25f, 0.0f);
			bz::pointLightB.position = glm::vec3(glm::cos(4.0f * t), 0.25f, -2.0f);

			bz::pointLightR.position = glm::vec3(100.0f, 100.0f, 100.0f);
			bz::pointLightG.position = glm::vec3(100.0f, 100.0f, 100.0f);
			bz::pointLightB.position = glm::vec3(100.0f, 100.0f, 100.0f);
		}
		else {
			//bz::dirLight.direction = glm::vec3(-0.5f, -0.5f, 1.0f);
		}

#if 0
		plane->nodes[0]->transform = glm::translate(
			glm::rotate(
				glm::scale(glm::mat4(1.0f), glm::vec3(0.2f)),
				glm::cos(float(currentFrame)),
				glm::vec3(0.5f, 0.5f, 1.0f)),
			glm::vec3(0.0f, 2.0f, 0.0f));
#endif

		plane->materials[0].parallaxMode = bz::parallaxMode;
		plane->materials[0].parallaxSteps = bz::parallaxSteps;
		plane->materials[0].parallaxScale = bz::parallaxScale;

		bz::camera.Update(deltaTime);
		bz::overlay->Update();

		DrawFrame();

		glfwPollEvents();
	}

	FindCloseChangeNotification(shaderDir);

	// Wait until all commandbuffers are done so we can safely clean up semaphores they might potentially be using.
	vkDeviceWaitIdle(bz::device.logicalDevice);

	delete bz::shadowMap;

	bz::skybox.Destroy(bz::device);
	bz::skyboxPipeline.Destroy(bz::device);
	bz::skyboxLayout.Destroy(bz::device);

	delete plane;
	delete lightpoles;
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