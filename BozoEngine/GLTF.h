#pragma once

#include "Common.h"

#include "Device.h"
#include "Texture.h"
#include "Buffer.h"
#include "BindGroup.h"
#include "Pipeline.h"

#pragma warning(push, 0)
#include <tiny_gltf.h>
#pragma warning(pop)

class GLTFModel {
public:

	// TODO: slim down vertices. We for example dont need vertex colors
	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec4 tangent;
		glm::vec2 uv;
		glm::vec3 color;

		static const VkVertexInputBindingDescription BindingDescripton[1];
		static const VkVertexInputAttributeDescription AttributeDescription[5];
	};

	struct Primitive {
		u32 firstIndex;
		u32 indexCount;
		i32 materialIndex;
	};

	struct Mesh {
		std::vector<Primitive> primitives;
	};

	struct Material {
		Texture* albedo;
		Texture* normal;
		Texture* metallicRoughness;
		BindGroup bindGroup;

		u32 parallaxMode = 0;
		u32 parallaxSteps = 0;
		float parallaxScale = 0.0f;
	};

	struct Node {
		Node* parent;
		std::vector<Node*> children;
		Mesh mesh;
		glm::mat4 transform;

		~Node();
	};

	Device& device;
	BindGroupLayout materialBindGroupLayout;

	std::vector<Texture> images;
	std::vector<Material> materials;
	std::vector<Node*> nodes;

	Buffer vertices, indices;

	GLTFModel(Device& device, BindGroupLayout materialLayout, const char* path);
	~GLTFModel();

	void Draw(VkCommandBuffer cmdBuffer, const Pipeline& pipeline, bool shadowMap = false);

private:
	struct PushConstants {
		glm::mat4 model;
		u32   parallaxMode;
		u32   parallaxSteps;
		float parallaxScale;
	};

	void DrawNode(VkCommandBuffer cmdBuffer, const Pipeline& pipeline, Node* node, bool shadowMap);

	void LoadImages(tinygltf::Model& model);

	void LoadMaterials(tinygltf::Model& model);

	void LoadNode(const tinygltf::Node& inputNode, const tinygltf::Model& model, Node* parent, std::vector<u32>& indexBuffer, std::vector<Vertex>& vertexBuffer);

	void LoadGLTFFile(const char* filename);
};