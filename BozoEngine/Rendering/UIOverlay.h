#pragma once

#include "../Core/Graphics.h"
#include "../Core/Device.h"

#include <imgui.h>
#include <glm/glm.hpp>

// Custom Dear ImGUI backend implementation

// Clean-up todos:
// Remove hardcoded 1<<20 values. DrawDataBuffer size should instead be retrieved from resourcemanager

class UIOverlay {
public:
	UIOverlay(Window* window, Device* device, Format colorFormat, Format depthFormat, void (*ImGUIRenderCallback)());
	~UIOverlay();

	void Update(float deltaTime);
	void Render(CommandBuffer& cmd);
	void DrawFrameTimeGraph();

private:
	struct {
		float entries[1024] = {};
		u32  front  = 0;
		u32  back   = 0;
		u32  count  = 0;
		bool freeze = false;

		float Get(u32 i) {
			i = (back + count - i - 1) % arraysize(entries);
			return entries[i];
		}

		void Post(float dt) {
			if (freeze) return;

			entries[front] = dt;
			front = (front + 1) % arraysize(entries);
			if (count == arraysize(entries)) {
				back = front;
			}
			else {
				count++;
			}
		}
	} m_frameTimeHistory = {};

	Device* m_device;

	// ImGui render function
	void (*m_ImGUIRenderCallback)(void);

	Handle<BindGroupLayout> m_bindgroupLayout;
	Handle<BindGroup>		m_bindgroup;
	Handle<Pipeline>		m_pipeline;
	Handle<Texture>			m_font;
	Handle<Buffer>			m_drawDataBuffer;

	void* m_vertexBufferStart  = nullptr;
	void* m_indexBufferStart	 = nullptr;
	u64   m_vertexBufferOffset = 0;
	u64   m_indexBufferOffset	 = 0;

	struct PushConstantBlock {
		glm::vec2 scale;
		glm::vec2 translate;
	} m_pushConstants = {};
};