#include "UIOverlay.h"

#include "../Core/ResourceManager.h"

#include <backends/imgui_impl_glfw.h>

UIOverlay::UIOverlay(Window* window, Device* device, Format colorFormat, Format depthFormat, void (*ImGUIRenderCallback)())
	: m_device{ device }, m_ImGUIRenderCallback{ ImGUIRenderCallback }
{
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	ImGui::StyleColorsDark();

	// Initialize glfw callbacks
	ImGui_ImplGlfw_InitForVulkan(window->m_window, true);

	// Initialize vulkan resources for the ui overlay such as the font texture + vertex/index buffers.
	ResourceManager* rm = ResourceManager::ptr;

	u8* fontData;
	int texWidth, texHeight;
	io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
	u64 uploadSize = texWidth * texHeight * sizeof(u32);

	m_font = rm->CreateTexture(fontData, {
		.debugName = "UI font texture",
		.width  = (u32)texWidth,
		.height = (u32)texHeight,
		.format = Format::RGBA8_UNORM,
		.usage  = Usage::SHADER_RESOURCE
	});

	m_bindgroupLayout = rm->CreateBindGroupLayout({
		.debugName = "UI font bindgroup layout",
		.bindings  = { {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT} }
	});

	m_bindgroup = rm->CreateBindGroup({
		.debugName = "UI font bindgroup",
		.layout	   = m_bindgroupLayout,
		.textures  = { {.binding = 0, .texture = m_font} }
	});

	m_drawDataBuffer = rm->CreateBuffer({
		.debugName = "UI combined vertex/index buffer",
		.byteSize  = 1 << 20,
		.usage	   = Usage::VERTEX_BUFFER | Usage::INDEX_BUFFER,
		.memory	   = Memory::Upload
	});

	// We keep the UI draw data persistently mapped
	Check(rm->MapBuffer(m_drawDataBuffer), "Failed to map UI vertex/index buffer");

	m_vertexBufferOffset = 0;
	m_vertexBufferStart  = rm->GetMapped(m_drawDataBuffer) + m_vertexBufferOffset;
	m_indexBufferOffset  = (1 << 20) - (1 << 18);
	m_indexBufferStart   = rm->GetMapped(m_drawDataBuffer) + m_indexBufferOffset;

	// Load the UI shaders and initialize the vulkan pipeline used for rendering the overlay
	std::vector<u32> vertShader = ReadShaderSpv("shaders/uioverlay.vert.spv");
	std::vector<u32> fragShader = ReadShaderSpv("shaders/uioverlay.frag.spv");

	m_pipeline = rm->CreatePipeline({
		.debugName = "UI pipeline",
		.shaderDescs = {
			{ .spirv = vertShader, .stage = ShaderStage::VERTEX},
			{ .spirv = fragShader, .stage = ShaderStage::FRAGMENT}
		},
		.bindgroupLayouts = { m_bindgroupLayout },
		.graphicsState = {
			.colorAttachments = { colorFormat },
			.blendStates = { Blend::PREMULTIPLY(0xF) },
			.rasterizationState = { .cullMode = CullMode::None },
			.vertexInputState = {
				.vertexStride = sizeof(ImDrawVert),
				.attributes = {
					{ .offset = offsetof(ImDrawVert, pos), .format = Format::RG32_SFLOAT},
					{ .offset = offsetof(ImDrawVert, uv),  .format = Format::RG32_SFLOAT},
					{ .offset = offsetof(ImDrawVert, col), .format = Format::RGBA8_UNORM}
				}
			}
		}
	});
}

UIOverlay::~UIOverlay() {
	ResourceManager* rm = ResourceManager::ptr;

	rm->DestroyPipeline(m_pipeline);
	rm->DestroyBuffer(m_drawDataBuffer);
	rm->DestroyBindGroupLayout(m_bindgroupLayout);
	rm->DestroyTexture(m_font);
}

void UIOverlay::Update(float deltaTime) {
	m_frameTimeHistory.Post(deltaTime);

	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// Always render the frametime graph in the top left corner
	ImGui::SetNextWindowSize({ 256, 80 });
	ImGui::SetNextWindowPos({ 0, 0 });
	ImGui::Begin("FrameTimeGraph", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
	DrawFrameTimeGraph();
	ImGui::End();

	// Render the user-specified imgui window
	m_ImGUIRenderCallback();

	ImGui::Render();

	ImDrawData* drawData = ImGui::GetDrawData();

	if (!drawData) { return; }

	u64 vertexBufferSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
	u64 indexBufferSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

	if ((vertexBufferSize == 0) || (indexBufferSize == 0)) { return; }

	Check(vertexBufferSize < m_indexBufferOffset, "Vertex buffer size exceeded the maximum limit!");
	Check(m_indexBufferOffset + indexBufferSize < (1 << 20), "Index buffer size exceeded the maximum limit!");

	// Write the new vertex and index data to the unified drawdata buffer on the GPU
	ImDrawVert* vertexDst = (ImDrawVert*)m_vertexBufferStart;
	ImDrawIdx* indexDst = (ImDrawIdx*)m_indexBufferStart;
	for (int i = 0; i < drawData->CmdListsCount; i++) {
		ImDrawList* cmdList = drawData->CmdLists[i];

		memcpy(vertexDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(indexDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

		vertexDst += cmdList->VtxBuffer.Size;
		indexDst += cmdList->IdxBuffer.Size;
	}
}

void UIOverlay::Render(CommandBuffer& cmd) {
	ImDrawData* imDrawData = ImGui::GetDrawData();
	int32_t vertexOffset = 0;
	int32_t indexOffset = 0;

	if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
		return;
	}

	Extent2D extent = m_device->GetSwapchainExtent();
	cmd.SetViewport((float)extent.width, (float)extent.height);

	cmd.BeginRenderingSwapchain();

	cmd.SetPipeline(m_pipeline);
	cmd.SetBindGroup(m_bindgroup, 0);
	cmd.SetVertexBuffer(m_drawDataBuffer, m_vertexBufferOffset);
	cmd.SetIndexBuffer(m_drawDataBuffer, m_indexBufferOffset, IndexType::UINT16);

	ImGuiIO& io = ImGui::GetIO();
	m_pushConstants.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
	m_pushConstants.translate = glm::vec2(-1.0f);
	
	cmd.PushConstants(&m_pushConstants, 0, sizeof(PushConstantBlock), ShaderStage::VERTEX);

	for (i32 i = 0; i < imDrawData->CmdListsCount; i++) {
		ImDrawList* drawList = imDrawData->CmdLists[i];
		for (i32 j = 0; j < drawList->CmdBuffer.Size; j++) {
			ImDrawCmd* draw = &drawList->CmdBuffer[j];

			cmd.SetScissor({
				.offset = {
					.x = glm::max((i32)draw->ClipRect.x, 0),
					.y = glm::max((i32)draw->ClipRect.y, 0)
				},
				.extent = {
					.width  = u32(draw->ClipRect.z - draw->ClipRect.x),
					.height = u32(draw->ClipRect.w - draw->ClipRect.y)
				}
			});

			cmd.DrawIndexed(draw->ElemCount, 1, indexOffset, vertexOffset, 0);

			indexOffset += draw->ElemCount;
		}
		vertexOffset += drawList->VtxBuffer.Size;
	}

	cmd.EndRendering();
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
void UIOverlay::DrawFrameTimeGraph() {
	constexpr float minHeight = 2.0f;
	constexpr float maxHeight = 64.0f;
	constexpr float dtMin = 1.0f / 120.0f;
	constexpr float dtMax = 1.0f / 15.0f;
	const float dtMin_Log2 = glm::log2(dtMin);
	const float dtMax_Log2 = glm::log2(dtMax);

	const float width = ImGui::GetWindowWidth();
	const u32 frameCount = arraysize(m_frameTimeHistory.entries);

	if (width > 0.0f && frameCount > 0) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 basePos = ImGui::GetCursorScreenPos();

		float endX = width;
		drawList->AddRectFilled(basePos, ImVec2(basePos.x + width, basePos.y + maxHeight), 0x40404040);

		float mouseX = ImGui::GetMousePos().x;
		u32 mouseFrame = 0;

		for (u32 frameIndex = 0; (frameIndex < frameCount) && (endX > 0.0f); frameIndex++) {
			float dt = m_frameTimeHistory.Get(frameIndex);

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

		m_frameTimeHistory.freeze = false;
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			m_frameTimeHistory.freeze = true;

			drawList->AddRectFilled(
				ImVec2(mouseX, basePos.y),
				ImVec2(mouseX + 2.0f, basePos.y + maxHeight),
				~0u);

			ImGui::SetTooltip("FPS: %.1f (%.2f ms)", 1.0f / m_frameTimeHistory.Get(mouseFrame), 1000.0f * m_frameTimeHistory.Get(mouseFrame));
		}
	}
}