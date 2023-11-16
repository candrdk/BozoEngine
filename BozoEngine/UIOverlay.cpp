#include "UIOverlay.h"
#include "Tools.h"

#include <backends/imgui_impl_glfw.h>

UIOverlay::UIOverlay(GLFWwindow* window, Device& device, VkFormat colorFormat, VkFormat depthFormat, void (*RenderFunction)()) : device{ device }, RenderImGui{ RenderFunction } {
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

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

	drawDataBuffer.Destroy(device);

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

	font = Texture::Create(device, {
		.width = (u32)texWidth,
		.height = (u32)texHeight,
		.format = Format::RGBA8_UNORM,
		.usage = Usage::SHADER_RESOURCE,
		.initialData = span<const u8>(fontData, uploadSize)
	});

	bindGroupLayout = BindGroupLayout::Create(device, {
		{.binding = 0, .type = Binding::TEXTURE, .stages = ShaderStage::FRAGMENT }
	});

	bindGroup = BindGroup::Create(device, bindGroupLayout, {
		.textures = { font.GetBinding(0) }
	});

	// Allocate draw data buffer for vertices and indides up front. Fixed size of 1 mb for now.
	// The bottom 3/4 of the draw data is used for storing vertices. The remaining 1/4 is used for indices.
	// [  VkDeviceMemory  ]
	// [     VkBuffer     ]
	// [ vertex ] [ index ]
	drawDataBuffer = Buffer::Create(device, {
		.debugName = "UI Overlay combined vertex/index buffer",
		.byteSize = 1 << 20,
		.usage = Usage::VERTEX_BUFFER | Usage::INDEX_BUFFER,
		.memory = Memory::UPLOAD
	});

	drawDataBuffer.Map(device);

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
	RenderImGui();
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
}

void UIOverlay::Draw(VkCommandBuffer cmdBuffer, VkExtent2D extent, const VkRenderingAttachmentInfo& colorAttachment) {
	ImDrawData* imDrawData = ImGui::GetDrawData();
	int32_t vertexOffset = 0;
	int32_t indexOffset = 0;

	if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	VkRenderingInfo renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
			.offset = {0, 0},
			.extent = extent
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
		.pDepthAttachment = nullptr,
		.pStencilAttachment = nullptr
	};

	vkCmdBeginRendering(cmdBuffer, &renderingInfo);

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

	vkCmdEndRendering(cmdBuffer);
}

static glm::vec4 DeltaTimeToColor(float dt) {
	constexpr glm::vec3 colors[] = {
		{0.0f, 0.0f, 1.0f}, // blue
		{0.0f, 1.0f, 0.0f}, // green
		{1.0f, 1.0f, 0.0f}, // yellow
		{1.0f, 0.0f, 0.0f}, // red
	};
	constexpr float dts[] = {
		1.0f / 120.0f,
		1.0f / 60.0f,
		1.0f / 30.0f,
		1.0f / 15.0f,
	};

	if (dt < dts[0]) {
		return glm::vec4(colors[0], 1.f);
	}

	for (size_t i = 1; i < arraysize(dts); ++i) {
		if (dt < dts[i]) {
			const float t = (dt - dts[i - 1]) / (dts[i] - dts[i - 1]);
			return glm::vec4(glm::mix(colors[i - 1], colors[i], t), 1.f);
		}
	}
	return glm::vec4(colors[arraysize(dts) - 1], 1.f);
}

// Based on https://asawicki.info/news?x=view&year=2022&month=5
void UIOverlay::ShowFrameTimeGraph() {
	constexpr float minHeight = 2.0f;
	constexpr float maxHeight = 64.0f;
	constexpr float dtMin = 1.0f / 120.0f;
	constexpr float dtMax = 1.0f / 15.0f;
	const float dtMin_Log2 = glm::log2(dtMin);
	const float dtMax_Log2 = glm::log2(dtMax);

	const float width = ImGui::GetWindowWidth();
	const u32 frameCount = arraysize(frameTimeHistory.entries);

	if (width > 0.0f && frameCount > 0) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 basePos = ImGui::GetCursorScreenPos();

		float endX = width;
		drawList->AddRectFilled(basePos, ImVec2(basePos.x + width, basePos.y + maxHeight), 0xFF404040);

		float mouseX = ImGui::GetMousePos().x;
		u32 mouseFrame = 0;

		for (u32 frameIndex = 0; (frameIndex < frameCount) && (endX > 0.0f); frameIndex++) {
			float dt = frameTimeHistory.Get(frameIndex);

			const float frameWidth = dt / dtMin;
			const float frameHeightFactor = (glm::log2(dt) - dtMin_Log2) / (dtMax_Log2 - dtMin_Log2);
			const float frameHeightFactor_Nrm = glm::clamp(frameHeightFactor, 0.0f, 1.0f);
			const float frameHeight = glm::mix(minHeight, maxHeight, frameHeightFactor_Nrm);
			const float begX = endX - frameWidth;

			glm::vec4 color = DeltaTimeToColor(dt);
			const u32 packedColor = glm::packUnorm4x8(color);

			drawList->AddRectFilled(
				ImVec2(basePos.x + glm::max(0.0f, glm::floor(begX)), basePos.y + maxHeight - frameHeight),
				ImVec2(basePos.x + glm::ceil(endX), basePos.y + maxHeight),
				packedColor);

			endX = begX;

			if (!mouseFrame && mouseX >= basePos.x + glm::max(0.0f, glm::floor(begX))) {
				mouseFrame = frameIndex;
			}
		}
		ImGui::Dummy(ImVec2(width, maxHeight));

		frameTimeHistory.freeze = false;
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			frameTimeHistory.freeze = true;

			drawList->AddRectFilled(
				ImVec2(mouseX, basePos.y),
				ImVec2(mouseX + 2.0f, basePos.y + maxHeight),
				~0u);

			ImGui::SetTooltip("FPS: %.1f (%.2f ms)", 1.0f / frameTimeHistory.Get(mouseFrame), 1000.0f * frameTimeHistory.Get(mouseFrame));
		}
	}
}