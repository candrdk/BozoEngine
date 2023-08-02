#pragma once
#include "Common.h"

#include "Device.h"
#include "Texture.h"
#include "BindGroup.h"

#pragma warning(push, 0)
#include <tiny_gltf.h>
#pragma warning(pop)

class GLTFModel {
public:

	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 color;

		static const VkVertexInputBindingDescription BindingDescripton[1];
		static const VkVertexInputAttributeDescription AttributeDescription[4];
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
		Texture2D* albedo;
		Texture2D* normal;
		Texture2D* OccMetRough;
		BindGroup bindGroup;
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

	std::vector<Texture2D> images;
	std::vector<Material> materials;
	std::vector<Node*> nodes;

	Buffer vertices, indices;

	GLTFModel(Device& device, BindGroupLayout materialLayout, const char* path);
	~GLTFModel();

	void Draw(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout);

private:
	void DrawNode(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout, Node* node);

	void LoadImages(tinygltf::Model& model);

	void LoadMaterials(tinygltf::Model& model);

	void LoadNode(const tinygltf::Node& inputNode, const tinygltf::Model& model, Node* parent, std::vector<u32>& indexBuffer, std::vector<Vertex>& vertexBuffer);

	void LoadGLTFFile(const char* filename);
};