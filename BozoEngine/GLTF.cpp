#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "GLTF.h"	// include tiny_gltf

#include <string>

#define GLM_FORCE_QUAT_DATA_XYZW	// glTF stores quaternions with xyzw layout. GLM defaults to wxyz.
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/matrix_transform.hpp>


VkVertexInputBindingDescription GLTFModel::Vertex::GetBindingDescription() {
	return {
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
	};
}

std::vector<VkVertexInputAttributeDescription> GLTFModel::Vertex::GetAttributeDescriptions() {
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
		{	// Location 0: Position
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, pos)
		},
		{	// Location 1: Normal
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, normal)
		},
		{	// Location 2: Texture coordinates
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vertex, uv)
		},
		{	// Location 3: Color
			.location = 3,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, color)
		}
	};

	return attributeDescriptions;
}

GLTFModel::Node::~Node() {
	for (Node* child : children) {
		delete child;
	}
}

GLTFModel::~GLTFModel() {
	for (auto node : nodes) {
		delete node;
	}

	vkDestroyBuffer(device.logicalDevice, vertices.buffer, nullptr);
	vkFreeMemory(device.logicalDevice, vertices.memory, nullptr);
	vkDestroyBuffer(device.logicalDevice, indices.buffer, nullptr);
	vkFreeMemory(device.logicalDevice, indices.memory, nullptr);

	for (Image image : images) {
		image.texture.Destroy(device);
	}
}

void GLTFModel::Draw(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout) {
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertices.buffer, offsets);
	vkCmdBindIndexBuffer(cmdBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);

	for (const auto& node : nodes) {
		DrawNode(cmdBuffer, pipelineLayout, node);
	}
}

void GLTFModel::DrawNode(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout, Node* node) {
	if (node->mesh.primitives.size() > 0) {
		// Calculate primitive matrix transform by traversing the node hiearchy to the root
		glm::mat4 nodeTransform = node->transform;
		Node* parent = node->parent;
		while (parent) {
			nodeTransform = parent->transform * nodeTransform;
			parent = parent->parent;
		}

		// Pass final matrix to the vertex shader using push constants
		vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &nodeTransform);

		for (const Primitive& primitive : node->mesh.primitives) {
			if (primitive.indexCount > 0) {
				Texture texture = textures[materials[primitive.materialIndex].baseColorTextureIndex];

				// Bind the descriptor for the current primitives' texture
				vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &images[texture.imageIndex].descriptorSet, 0, nullptr);
				vkCmdDrawIndexed(cmdBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
			}
		}
	}

	for (const auto& child : node->children) {
		DrawNode(cmdBuffer, pipelineLayout, child);
	}
}

void GLTFModel::LoadImages(tinygltf::Model& model) {
	images.resize(model.images.size());

	for (size_t i = 0; i < model.images.size(); i++) {
		tinygltf::Image& gltfImage = model.images[i];

		u8* buffer = nullptr;
		VkDeviceSize bufferSize = 0;

		if (gltfImage.component == 3) {
			Check(false, "RGB-only images are not supported atm.");
		}
		else {
			buffer = &gltfImage.image[0];
			bufferSize = gltfImage.image.size();
		}

		images[i].texture.CreateFromBuffer(buffer, bufferSize, device, device.graphicsQueue, gltfImage.width, gltfImage.height, VK_FORMAT_R8G8B8A8_SRGB);
	}
}

void GLTFModel::LoadTextures(tinygltf::Model& model) {
	textures.resize(model.textures.size());

	for (size_t i = 0; i < model.textures.size(); i++) {
		textures[i].imageIndex = model.textures[i].source;
	}
}

void GLTFModel::LoadMaterials(tinygltf::Model& model) {
	materials.resize(model.materials.size());

	for (size_t i = 0; i < model.materials.size(); i++) {
		tinygltf::Material gltfMaterial = model.materials[i];

		if (gltfMaterial.values.find("baseColorFactor") != gltfMaterial.values.end()) {
			materials[i].baseColorFactor = glm::make_vec4(gltfMaterial.values["baseColorFactor"].ColorFactor().data());
		}

		if (gltfMaterial.values.find("baseColorTexture") != gltfMaterial.values.end()) {
			materials[i].baseColorTextureIndex = gltfMaterial.values["baseColorTexture"].TextureIndex();
		}
	}
}

void GLTFModel::LoadNode(const tinygltf::Node& inputNode, const tinygltf::Model& model, Node* parent, std::vector<u32>& indexBuffer, std::vector<Vertex>& vertexBuffer) {
	Node* node = new Node();
	node->transform = glm::mat4(1.0f);
	node->parent = parent;

	// Get the local node transform matrix.
	// Can either be given as a matrix array, in which case node->transform = matrix 
	// Or with separate Translation, Rotation and Scale, in which case the matrix is computed by M = T * R * S.
	if (inputNode.matrix.size() == 16) {
		node->transform = glm::make_mat4x4(inputNode.matrix.data());
	}
	else {
		if (inputNode.translation.size() == 3) {
			node->transform = glm::translate(node->transform, glm::vec3(glm::make_vec3(inputNode.translation.data())));
		}
		if (inputNode.rotation.size() == 4) {
			node->transform *= glm::mat4(glm::quat(glm::make_quat(inputNode.rotation.data())));
		}
		if (inputNode.scale.size() == 3) {
			node->transform = glm::scale(node->transform, glm::vec3(glm::make_vec3(inputNode.scale.data())));
		}
	}

	// Recursively load the children of the input node.
	if (inputNode.children.size() > 0) {
		for (size_t i = 0; i < inputNode.children.size(); i++) {
			LoadNode(model.nodes[inputNode.children[i]], model, node, indexBuffer, vertexBuffer);
		}
	}

	if (inputNode.mesh > -1) {
		const tinygltf::Mesh mesh = model.meshes[inputNode.mesh];

		// Reserve space for the mesh primitives
		node->mesh.primitives.reserve(node->mesh.primitives.size() + mesh.primitives.size());

		for (size_t i = 0; i < mesh.primitives.size(); i++) {
			const tinygltf::Primitive& primitive = mesh.primitives[i];

			u32 firstIndex = (u32)indexBuffer.size();
			u32 vertexStart = (u32)vertexBuffer.size();

			// Load vertices
			const float* positionBuffer = nullptr;
			const float* normalsBuffer = nullptr;
			const float* texCoordsBuffer = nullptr;
			size_t vertexCount = 0;

			// Get buffer data for vertex positions
			if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
				const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
				positionBuffer = (float*)(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
				vertexCount = accessor.count;
			}

			// Get buffer data for vertex normals
			if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
				const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("NORMAL")->second];
				const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
				normalsBuffer = (float*)(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
			}

			// Get buffer data for texture coordinates normals
			if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
				const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
				const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
				texCoordsBuffer = (float*)(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
			}

			// Reserve space for and append the vertices to the model's vertexBuffer
			vertexBuffer.reserve(vertexBuffer.size() + vertexCount);
			for (size_t v = 0; v < vertexCount; v++) {
				vertexBuffer.push_back({
					.pos = glm::vec4(glm::make_vec3(&positionBuffer[v * 3]), 1.0f),
					.normal = normalsBuffer ? glm::normalize(glm::vec3(glm::make_vec3(&normalsBuffer[v * 3]))) : glm::vec3(0.0f),
					.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec2(0.0f),
					.color = glm::vec3(1.0f)
					});
			}

			// Load indices
			const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
			const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
			u32 indexCount = (u32)accessor.count;

			indexBuffer.reserve(indexBuffer.size() + indexCount);

			// glTF supports different component types of indices
			switch (accessor.componentType) {
			case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
				const u32* buf = (const u32*)(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
				for (size_t index = 0; index < indexCount; index++) {
					indexBuffer.push_back(buf[index] + vertexStart);
				}
				break;
			}
			case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
				const u16* buf = (const u16*)(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
				for (size_t index = 0; index < indexCount; index++) {
					indexBuffer.push_back(buf[index] + vertexStart);
				}
				break;
			}
			case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
				const u8* buf = (const u8*)(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
				for (size_t index = 0; index < indexCount; index++) {
					indexBuffer.push_back(buf[index] + vertexStart);
				}
				break;
			}
			default: {
				Check(false, "Index component type %i is not supported", accessor.componentType);
			}
			}

			node->mesh.primitives.push_back({
				.firstIndex = firstIndex,
				.indexCount = indexCount,
				.materialIndex = primitive.material
				});
		}
	}

	if (parent) {
		parent->children.push_back(node);
	}
	else {
		nodes.push_back(node);
	}
}

void GLTFModel::LoadGLTFFile(const char* filename) {
	tinygltf::Model gltfInput;
	tinygltf::TinyGLTF gltfContext;
	std::string error, warning;

	Check(gltfContext.LoadASCIIFromFile(&gltfInput, &error, &warning, filename), "tinygltf failed to load gltf file");

	LoadImages(gltfInput);
	LoadMaterials(gltfInput);
	LoadTextures(gltfInput);

	std::vector<u32> indexBuffer;
	std::vector<Vertex> vertexBuffer;

	const tinygltf::Scene& scene = gltfInput.scenes[0];

	// Reserve space for the number of nodes in the scene.
	nodes.reserve(nodes.size() + scene.nodes.size());
	for (size_t i = 0; i < scene.nodes.size(); i++) {
		const tinygltf::Node node = gltfInput.nodes[scene.nodes[i]];
		LoadNode(node, gltfInput, nullptr, indexBuffer, vertexBuffer);
	}

	// Upload the gltf vertex and index buffer ot the GPU.
	size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
	size_t indexBufferSize = indexBuffer.size() * sizeof(u32);

	Buffer vertexStaging, indexStaging;
	device.CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBufferSize, &vertexStaging, vertexBuffer.data());
	device.CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexBufferSize, &indexStaging, indexBuffer.data());

	device.CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBufferSize, &vertices);
	device.CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBufferSize, &indices);

	VkBufferCopy copyRegion = {};
	VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	copyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vertices.buffer, 1, &copyRegion);

	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(copyCmd, indexStaging.buffer, indices.buffer, 1, &copyRegion);

	device.FlushCommandBuffer(copyCmd, device.graphicsQueue);

	vertexStaging.destroy(device.logicalDevice);
	indexStaging.destroy(device.logicalDevice);
}