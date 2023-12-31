#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "GLTF.h"	// include tiny_gltf

#include <string>

#define GLM_FORCE_QUAT_DATA_XYZW	// glTF stores quaternions with xyzw layout. GLM defaults to wxyz.
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/matrix_transform.hpp>

const VkVertexInputBindingDescription GLTFModel::Vertex::BindingDescripton[1] = {
	{
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
	} 
};

const VkVertexInputAttributeDescription GLTFModel::Vertex::AttributeDescription[5] = {
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
	{	// Location 2: Tangent
		.location = 2,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.offset = offsetof(Vertex, tangent)
	},
	{	// Location 3: Texture coordinates
		.location = 3,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = offsetof(Vertex, uv)
	},
	{	// Location 4: Color
		.location = 4,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(Vertex, color)
	}
};

GLTFModel::Node::~Node() {
	for (Node* child : children) {
		delete child;
	}
}

GLTFModel::GLTFModel(Device& device, BindGroupLayout materialLayout, const char* path)
	: device{ device }, materialBindGroupLayout{ materialLayout }
{
	LoadGLTFFile(path);
}

GLTFModel::~GLTFModel() {
	for (auto node : nodes) {
		delete node;
	}

	vertices.Destroy(device);
	indices.Destroy(device);

	for (Texture image : images) {
		image.Destroy(device);
	}
}

void GLTFModel::Draw(VkCommandBuffer cmdBuffer, const Pipeline& pipeline, bool shadowMap) {
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertices.buffer, offsets);
	vkCmdBindIndexBuffer(cmdBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);

	for (const auto& node : nodes) {
		DrawNode(cmdBuffer, pipeline, node, shadowMap);
	}
}

void GLTFModel::DrawNode(VkCommandBuffer cmdBuffer, const Pipeline& pipeline, Node* node, bool shadowMap) {
	if (node->mesh.primitives.size() > 0) {
		// Calculate primitive matrix transform by traversing the node hiearchy to the root
		glm::mat4 nodeTransform = node->transform;
		Node* parent = node->parent;
		while (parent) {
			nodeTransform = parent->transform * nodeTransform;
			parent = parent->parent;
		}

		for (const Primitive& primitive : node->mesh.primitives) {
			if (primitive.indexCount <= 0) continue;

			// If rendering to shadow map we dont need to bind material textures and push constants can be simplified.
			if (shadowMap) {
				vkCmdPushConstants(cmdBuffer, pipeline.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &nodeTransform);
			}
			else {
				// Bind the descriptor for the current primitives' material
				vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipelineLayout, 1, 1, &materials[primitive.materialIndex].bindGroup.descriptorSet, 0, nullptr);

				PushConstants p = {
					.model = nodeTransform,
					.parallaxMode = materials[primitive.materialIndex].parallaxMode,
					.parallaxSteps = materials[primitive.materialIndex].parallaxSteps,
					.parallaxScale = materials[primitive.materialIndex].parallaxScale
				};

				vkCmdPushConstants(cmdBuffer, pipeline.pipelineLayout, pipeline.pushConstants.stageFlags, 0, sizeof(p), &p);
			}

			vkCmdDrawIndexed(cmdBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
		}
	}

	for (const auto& child : node->children) {
		DrawNode(cmdBuffer, pipeline, child, shadowMap);
	}
}

void GLTFModel::LoadImages(tinygltf::Model& model) {
	images.resize(model.images.size() + 1);

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

		// Ugly hack, idk how this should be handled properly.
		bool srgb = false;
		for (tinygltf::Material m : model.materials) { 
			int idx = m.pbrMetallicRoughness.baseColorTexture.index;
			if (m.extensions.find("KHR_materials_pbrSpecularGlossiness") != m.extensions.end())
				idx = m.extensions["KHR_materials_pbrSpecularGlossiness"].Get<tinygltf::Value::Object>()["diffuseTexture"].Get<tinygltf::Value::Object>()["index"].GetNumberAsInt();

			if (idx != -1 && i == model.textures[idx].source) { srgb = true; break; }
		}

		images[i] = Texture::Create(device, TextureDesc{
			.width = (u32)gltfImage.width,
			.height = (u32)gltfImage.height,
			.format = srgb ? Format::RGBA8_SRGB : Format::RGBA8_UNORM,
			.usage = Usage::SHADER_RESOURCE,
			.generateMipLevels = true,
			.initialData = span<const u8>(buffer, bufferSize)
		});
	}

	images.back() = Texture::Create(device, TextureDesc{
		.width = 1,
		.height = 1,
		.format = Format::RGBA8_UNORM,
		.usage = Usage::SHADER_RESOURCE,
		.generateMipLevels = false,
		.initialData = { 0xFF, 0x00, 0xFF, 0xFF }
	});
}

void GLTFModel::LoadMaterials(tinygltf::Model& model) {
	materials.resize(model.materials.size());

	for (size_t i = 0; i < model.materials.size(); i++) {
		tinygltf::Material gltfMaterial = model.materials[i];
		GLTFModel::Material& material = materials[i];

		material.albedo = &images.back();
		material.metallicRoughness = &images.back();
		material.normal = &images.back();

		if (gltfMaterial.values.find("baseColorTexture") != gltfMaterial.values.end()) {
			material.albedo = &images[model.textures[gltfMaterial.values["baseColorTexture"].TextureIndex()].source];
		}
		else if (gltfMaterial.extensions.find("KHR_materials_pbrSpecularGlossiness") != gltfMaterial.extensions.end()) {
			material.albedo = &images[model.textures[gltfMaterial.extensions["KHR_materials_pbrSpecularGlossiness"].Get<tinygltf::Value::Object>()["diffuseTexture"].Get<tinygltf::Value::Object>()["index"].GetNumberAsInt()].source];
		}
		if (gltfMaterial.values.find("metallicRoughnessTexture") != gltfMaterial.values.end()) {
			material.metallicRoughness = &images[model.textures[gltfMaterial.values["metallicRoughnessTexture"].TextureIndex()].source];
		}
		if (gltfMaterial.additionalValues.find("normalTexture") != gltfMaterial.additionalValues.end()) {
			material.normal = &images[model.textures[gltfMaterial.additionalValues["normalTexture"].TextureIndex()].source];
		}

		material.bindGroup = BindGroup::Create(device, materialBindGroupLayout, {
			.textures = { material.albedo->GetBinding(0), material.normal->GetBinding(1), material.metallicRoughness->GetBinding(2) }
		});
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
			const float* tangentsBuffer = nullptr;
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

			// Get buffer data for vertex tangents
			if (primitive.attributes.find("TANGENT") != primitive.attributes.end()) {
				const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("TANGENT")->second];
				const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
				tangentsBuffer = (float*)(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
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
					.tangent = tangentsBuffer ? glm::vec4(glm::make_vec4(&tangentsBuffer[v * 4])) : glm::vec4(0.0f),
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

	if (filename[strlen(filename) - 1] == 'b') {
		Check(gltfContext.LoadBinaryFromFile(&gltfInput, &error, &warning, filename), "tinygltf failed to load glb file");
	}
	else {
		Check(gltfContext.LoadASCIIFromFile(&gltfInput, &error, &warning, filename), "tinygltf failed to load gltf file");
	}

	LoadImages(gltfInput);
	LoadMaterials(gltfInput);

	std::vector<u32> indexBuffer;
	std::vector<Vertex> vertexBuffer;

	const tinygltf::Scene& scene = gltfInput.scenes[gltfInput.defaultScene == -1 ? 0 : gltfInput.defaultScene];

	// Reserve space for the number of nodes in the scene.
	nodes.reserve(nodes.size() + scene.nodes.size());
	for (size_t i = 0; i < scene.nodes.size(); i++) {
		const tinygltf::Node node = gltfInput.nodes[scene.nodes[i]];
		LoadNode(node, gltfInput, nullptr, indexBuffer, vertexBuffer);
	}

	// Upload the gltf vertex and index buffer ot the GPU.
	size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
	size_t indexBufferSize = indexBuffer.size() * sizeof(u32);

	Buffer vertexStaging = Buffer::Create(device, {
		.debugName = "glTF vertex staging",
		.usage = Usage::TRANSFER_SRC,
		.memory = Memory::UPLOAD,
		.initialData = span((const u8*)vertexBuffer.data(), vertexBufferSize)
	});

	Buffer indexStaging = Buffer::Create(device, {
		.debugName = "glTF index staging",
		.usage = Usage::TRANSFER_SRC,
		.memory = Memory::UPLOAD,
		.initialData = span((const u8*)indexBuffer.data(), indexBufferSize)
	});

	vertices = Buffer::Create(device, {
		.debugName = "glTF vertex buffer",
		.byteSize = vertexBufferSize,
		.usage = Usage::VERTEX_BUFFER | Usage::TRANSFER_DST
	});

	indices = Buffer::Create(device, {
		.debugName = "glTF vertex buffer",
		.byteSize = indexBufferSize,
		.usage = Usage::INDEX_BUFFER | Usage::TRANSFER_DST
	});

	VkBufferCopy copyRegion = {};
	VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	copyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vertices.buffer, 1, &copyRegion);

	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(copyCmd, indexStaging.buffer, indices.buffer, 1, &copyRegion);

	device.FlushCommandBuffer(copyCmd, device.graphicsQueue);

	vertexStaging.Destroy(device);
	indexStaging.Destroy(device);
}