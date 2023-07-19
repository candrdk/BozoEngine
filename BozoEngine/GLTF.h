#pragma once
#include "Common.h"

#include "Device.h"
#include "Texture.h"

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

		static VkVertexInputBindingDescription GetBindingDescription();
		static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
	};

	struct Primitive {
		u32 firstIndex;
		u32 indexCount;
		i32 materialIndex;
	};

	struct Mesh {
		std::vector<Primitive> primitives;
	};

	struct Image {
		Texture2D texture;
		VkDescriptorSet descriptorSet;
	};

	struct Texture {
		u32 imageIndex;
	};

	struct Material {
		Texture albedo;
		Texture normal;
		Texture OccMetRough;
		VkDescriptorSet descriptorSet;
	};

	struct Node {
		Node* parent;
		std::vector<Node*> children;
		Mesh mesh;
		glm::mat4 transform;

		~Node();
	};

	Device& device;

	std::vector<Image> images;
	std::vector<Material> materials;
	std::vector<Node*> nodes;

	Buffer vertices, indices;

	GLTFModel(Device& device) : device{ device } {}
	~GLTFModel();

	void Draw(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout);

	void DrawNode(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout, Node* node);

	void LoadImages(tinygltf::Model& model);

	void LoadMaterials(tinygltf::Model& model);

	void LoadNode(const tinygltf::Node& inputNode, const tinygltf::Model& model, Node* parent, std::vector<u32>& indexBuffer, std::vector<Vertex>& vertexBuffer);

	void LoadGLTFFile(const char* filename);
};