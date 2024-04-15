#pragma once

#include "../Core/Graphics.h"
#include "../Core/Device.h"

#include <glm/glm.hpp>

class GLTFModel {
public:
	GLTFModel(Device* device, Handle<BindGroupLayout> materialLayout, const char* path);
	~GLTFModel();

	// NOTE: temporary interface to allow the use of parallax mapping. This should be removed once we can load parallax settings directly.
	void UpdateMaterialParallax(u32 mode, u32 steps, float scale) {
		for (Material& material : m_materials) {
			material.parallaxMode = mode;
			material.parallaxSteps = steps;
			material.parallaxScale = scale;
		}
	}

	void Draw(CommandBuffer& cmd, bool shadowMap = false) const;

	// TODO: slim down vertices
	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec4 tangent;
		glm::vec2 uv;
		glm::vec3 color;

		static const GraphicsState::VertexInputState InputState;
	};

	struct Primitive {
		u32 firstIndex;
		u32 indexCount;
		i32 materialIndex;
	};

	struct Mesh {
		std::vector<Primitive> primitives;
	};

	struct Node {
		Node* parent;
		std::vector<Node*> children;
		Mesh mesh;
		glm::mat4 transform;

		~Node() {
			for (Node* child : children)
				delete child;
		}
	};

private:
	struct PushConstants {
		glm::mat4 model;
		u32   parallaxMode;
		u32   parallaxSteps;
		float parallaxScale;
	};

	struct Material {
		Handle<Texture>   albedo;
		Handle<Texture>   normal;
		Handle<Texture>   metallicRoughness;
		Handle<BindGroup> bindgroup;

		u32 parallaxMode = 0;
		u32 parallaxSteps = 0;
		float parallaxScale = 0.0f;
	};

	void DrawNode(CommandBuffer& cmd, Node* node, bool shadowMap) const;

	Device* m_device;
	Handle<BindGroupLayout> m_materialBindGroupLayout;

	std::vector<Handle<Texture>> m_images;
	std::vector<Material>		 m_materials;
	std::vector<Node*>			 m_nodes;

	// TODO: get rid of this dummy. Invalid handles should be handled by backend code.
	Handle<Texture> m_dummyTexture;
	Handle<Buffer>  m_vertices;
	Handle<Buffer>  m_indices;
};