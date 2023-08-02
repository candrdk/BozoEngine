#include "UIOverlay.h"
#include "Tools.h"

#include <backends/imgui_impl_glfw.h>

UIOverlay::UIOverlay(GLFWwindow* window, Device& device, VkFormat colorFormat, VkFormat depthFormat) : device{ device } {
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// TODO: set our own imgui style here
	ImGui::StyleColorsDark();

	// Initialize glfw callbacks
	ImGui_ImplGlfw_InitForVulkan(window, true);

	// Initialize vulkan resources for the ui overlay such as the font texture + vertex/index buffers.
	InitializeVulkanResources();

	// Load the ui overlay shaders and initialize the vulkan pipeline used for rendering the overlay
	InitializeVulkanPipeline(colorFormat, depthFormat);
}

UIOverlay::~UIOverlay() {
	ImGui_ImplGlfw_Shutdown();

	if (ImGui::GetCurrentContext()) {
		ImGui::DestroyContext();
	}


	drawDataBuffer.unmap(device.logicalDevice);
	drawDataBuffer.destroy(device.logicalDevice);

	font.Destroy(device);
	bindGroupLayout.Destroy(device);
	pipeline.Destroy(device);
}

void UIOverlay::InitializeVulkanResources() {
	ImGuiIO& io = ImGui::GetIO();

	u8* fontData;
	int texWidth, texHeight;
	io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
	VkDeviceSize uploadSize = (u64)texWidth * (u64)texHeight * sizeof(u32);	// u64 cast to satisfy arithmetic warning

	// TODO: currently generates mip maps - do we want that?
	font.CreateFromBuffer(fontData, uploadSize, device, device.graphicsQueue, texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

	bindGroupLayout = BindGroupLayout::Create(device, {
		{.binding = 0, .type = Binding::TEXTURE, .stages = Binding::FRAGMENT }
	});

	bindGroup = BindGroup::Create(device, bindGroupLayout, {
		.textures = { {.binding = 0, .sampler = font.sampler, .view = font.view, .layout = font.layout} }
	});

	// Allocate draw data buffer for vertices and indides up front. Fixed size of 1 mb for now.
	// The bottom 3/4 of the draw data is used for storing vertices. The remaining 1/4 is used for indices.
	// [  VkDeviceMemory  ]
	// [     VkBuffer     ]
	// [ vertex ] [ index ]
	device.CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 << 20, &drawDataBuffer);
	drawDataBuffer.map(device.logicalDevice);
	vertexBufferOffset = 0;
	vertexBufferStart = (u8*)drawDataBuffer.mapped + vertexBufferOffset;
	indexBufferOffset = drawDataBuffer.size - (drawDataBuffer.size >> 2);
	indexBufferStart = (u8*)drawDataBuffer.mapped + indexBufferOffset;
}

void UIOverlay::InitializeVulkanPipeline(VkFormat colorFormat, VkFormat depthFormat) {
	Shader vertShader = Shader::Create(device, "shaders/uioverlay.vert.spv");
	Shader fragShader = Shader::Create(device, "shaders/uioverlay.frag.spv");

	pipeline = Pipeline::Create(device, VK_PIPELINE_BIND_POINT_GRAPHICS, {
		.debugName = "UI overlay pipeline",
		.shaders = { vertShader, fragShader},
		.bindGroups = { bindGroupLayout },
		.graphicsState = {
			.attachments = {
				.formats = { colorFormat },
				.depthStencilFormat = depthFormat,
				.blendStates = {{
					.blendEnable = VK_TRUE,
					.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
					.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
					.colorBlendOp = VK_BLEND_OP_ADD,
					.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
					.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
					.alphaBlendOp = VK_BLEND_OP_ADD,
					.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
				}}
			},
			.rasterization = {
				.cullMode = VK_CULL_MODE_NONE,
				.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
			},
			.sampleCount = VK_SAMPLE_COUNT_1_BIT,
			.vertexInput = {
				.bindingDesc = {
					{
						.binding = 0,
						.stride = sizeof(ImDrawVert),
						.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
					}
				},
				.attributeDesc = {
					{
						.location = 0,
						.binding = 0,
						.format = VK_FORMAT_R32G32_SFLOAT,
						.offset = offsetof(ImDrawVert, pos)
					}, {
						.location = 1,
						.binding = 0,
						.format = VK_FORMAT_R32G32_SFLOAT,
						.offset = offsetof(ImDrawVert, uv)
					}, {
						.location = 2,
						.binding = 0,
						.format = VK_FORMAT_R8G8B8A8_UNORM,
						.offset = offsetof(ImDrawVert, col)
					},
				}
			}
		}
	});

	// We don't need to keep the shaders around once the pipeline has been created.
	vertShader.Destroy(device);
	fragShader.Destroy(device);
}

void UIOverlay::RenderFrame() {
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGui::ShowDemoWindow();

	ImGui::Render();
}

void UIOverlay::Update() {
	RenderFrame();

	ImDrawData* drawData = ImGui::GetDrawData();

	if (!drawData) { return; }

	VkDeviceSize vertexBufferSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
	VkDeviceSize indexBufferSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

	if ((vertexBufferSize == 0) || (indexBufferSize == 0)) { return; }

	// TODO: log and return instead of assert
	Check(vertexBufferSize < indexBufferOffset, "Vertex buffer size exceeded the maximum limit!");
	Check(indexBufferOffset + indexBufferSize < drawDataBuffer.size, "Index buffer size exceeded the maximum limit!");


	// Upload vertex / index data into a single contiguous GPU buffer
	ImDrawVert* vertexDst = (ImDrawVert*)vertexBufferStart;
	ImDrawIdx* indexDst = (ImDrawIdx*)indexBufferStart;
	for (int i = 0; i < drawData->CmdListsCount; i++) {
		ImDrawList* cmdList = drawData->CmdLists[i];

		memcpy(vertexDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(indexDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

		vertexDst += cmdList->VtxBuffer.Size;
		indexDst += cmdList->IdxBuffer.Size;
	}

	VkCheck(drawDataBuffer.Flush(device.logicalDevice), "Failed to flush ui overlay draw data buffer to device memory");
}

void UIOverlay::Draw(VkCommandBuffer cmdBuffer) {
	ImDrawData* imDrawData = ImGui::GetDrawData();
	int32_t vertexOffset = 0;
	int32_t indexOffset = 0;

	if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
	vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipelineLayout, 0, 1, &bindGroup.descriptorSet, 0, 0);

	pushConstantBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
	pushConstantBlock.translate = glm::vec2(-1.0f);
	vkCmdPushConstants(cmdBuffer, pipeline.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantBlock), &pushConstantBlock);

	vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &drawDataBuffer.buffer, &vertexBufferOffset);
	vkCmdBindIndexBuffer(cmdBuffer, drawDataBuffer.buffer, indexBufferOffset, VK_INDEX_TYPE_UINT16);

	for (i32 i = 0; i < imDrawData->CmdListsCount; i++) {
		ImDrawList* cmdList = imDrawData->CmdLists[i];
		for (i32 j = 0; j < cmdList->CmdBuffer.Size; j++) {
			ImDrawCmd* cmd = &cmdList->CmdBuffer[j];
			VkRect2D scissorRect = {
				.offset = {
					glm::max((i32)cmd->ClipRect.x, 0),
					glm::max((i32)cmd->ClipRect.y, 0)
				},
				.extent = {
					.width  = u32(cmd->ClipRect.z - cmd->ClipRect.x),
					.height = u32(cmd->ClipRect.w - cmd->ClipRect.y)
				}
			};
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissorRect);
			vkCmdDrawIndexed(cmdBuffer, cmd->ElemCount, 1, indexOffset, vertexOffset, 0);
			indexOffset += cmd->ElemCount;
		}
		vertexOffset += cmdList->VtxBuffer.Size;
	}
}