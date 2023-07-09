#include "UIOverlay.h"

UIOverlay::UIOverlay() {
	ImGui::CreateContext();

	ImGui::StyleColorsDark();

	// TODO: Check if these configFlags are necessary
	// Also, check if io.FontGlobalScale should be changed.
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
}

UIOverlay::~UIOverlay() {
	if (ImGui::GetCurrentContext()) {
		ImGui::DestroyContext();
	}
}

// TODO: We are repeating a lot of the texture.h code word by word.
// TODO: split up into smaller functions
// Initialize all vulkan resources required to render the UI overlay.
void UIOverlay::Initialize(const Device& device, VkSampleCountFlagBits rasterizationSamples, VkFormat colorFormat, VkFormat depthFormat) {
	Check(vertShader.module != VK_NULL_HANDLE, "Cannot initialize ui overlay without vertex shader. Set UIOverlay.shaders before calling UIOverlay.Initialize!");
	Check(fragShader.module != VK_NULL_HANDLE, "Cannot initialize ui overlay without fragment shader. Set UIOverlay.shaders before calling UIOverlay.Initialize!");

	ImGuiIO& io = ImGui::GetIO();

	u8* fontData;
	int texWidth, texHeight;
	io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
	VkDeviceSize uploadSize = texWidth * texHeight * sizeof(u32);

	VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = {
			.width = (u32)texWidth,
			.height = (u32)texHeight,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkCheck(vkCreateImage(device.logicalDevice, &imageInfo, nullptr, &fontImage), "Failed to create font image");

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device.logicalDevice, fontImage, &memReqs);
	VkMemoryAllocateInfo allocInfo = { 
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO ,
		.allocationSize = memReqs.size,
		.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};

	VkCheck(vkAllocateMemory(device.logicalDevice, &allocInfo, nullptr, &fontMemory), "Failed to allocate memory for font image");
	VkCheck(vkBindImageMemory(device.logicalDevice, fontImage, fontMemory, 0), "Failed to bind font image to memory");

	// Create font image view
	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = fontImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};

	VkCheck(vkCreateImageView(device.logicalDevice, &viewInfo, nullptr, &fontView), "Failed to create image view of font image");

	// Create staging buffer for font data upload
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	VkMemoryRequirements memRequirements;

	VkBufferCreateInfo stagingBufferCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = uploadSize,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};
	VkCheck(vkCreateBuffer(device.logicalDevice, &stagingBufferCreateInfo, nullptr, &stagingBuffer), "Failed to create staging buffer");

	vkGetBufferMemoryRequirements(device.logicalDevice, stagingBuffer, &memRequirements);

	VkMemoryAllocateInfo memoryAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = device.GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	};

	VkCheck(vkAllocateMemory(device.logicalDevice, &memoryAllocInfo, nullptr, &stagingBufferMemory), "Failed to allocate vertex buffer memory");
	VkCheck(vkBindBufferMemory(device.logicalDevice, stagingBuffer, stagingBufferMemory, 0), "Failed to bind DeviceMemory to VkBuffer");

	void* data;
	VkCheck(vkMapMemory(device.logicalDevice, stagingBufferMemory, 0, memRequirements.size, 0, &data), "Failed to map staging buffer memory");
	memcpy(data, fontData, uploadSize);
	vkUnmapMemory(device.logicalDevice, stagingBufferMemory);

	VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	// prepare font image for transfer
	SetImageLayout(copyCmd, fontImage, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_UNDEFINED, 
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
		VK_PIPELINE_STAGE_HOST_BIT, 
		VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy bufferCopyRegion = {
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1,
		},
		.imageExtent = {
			.width = (u32)texWidth,
			.height = (u32)texHeight,
			.depth = 1
		}
	};

	vkCmdCopyBufferToImage(copyCmd, stagingBuffer, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

	// prepare font image for shader read
	SetImageLayout(copyCmd, fontImage, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
		VK_PIPELINE_STAGE_TRANSFER_BIT, 
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	device.FlushCommandBuffer(copyCmd, device.graphicsQueue);
	
	// Clean up staging buffer
	vkDestroyBuffer(device.logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(device.logicalDevice, stagingBufferMemory, nullptr);

	// Font texture sampler
	VkSamplerCreateInfo samplerInfo = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
	};

	VkCheck(vkCreateSampler(device.logicalDevice, &samplerInfo, nullptr, &sampler), "Failed to create font image sampler");

	// Create descriptor pool
	VkDescriptorPoolSize poolSizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 }
	};
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 2,
		.poolSizeCount = arraysize(poolSizes),
		.pPoolSizes = poolSizes
	};

	VkCheck(vkCreateDescriptorPool(device.logicalDevice, &descriptorPoolCreateInfo, nullptr, &descriptorPool), "Failed to create UIOverlay descriptor pool");

	// Descriptor set layout
	VkDescriptorSetLayoutBinding setLayoutBindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		}
	};
	VkDescriptorSetLayoutCreateInfo descriptorLayoutCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = arraysize(setLayoutBindings),
		.pBindings = setLayoutBindings
	};

	VkCheck(vkCreateDescriptorSetLayout(device.logicalDevice, &descriptorLayoutCreateInfo, nullptr, &descriptorSetLayout), "Failed to create UIOverlay descriptor set layout");

	// Descriptor set
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &descriptorSetLayout
	};

	VkCheck(vkAllocateDescriptorSets(device.logicalDevice, &descriptorSetAllocInfo, &descriptorSet), "Failed to allocate UIOverlay descriptor set");

	VkDescriptorImageInfo fontDescriptor = {
		.sampler = sampler,
		.imageView = fontView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	VkWriteDescriptorSet writeDescriptorSets[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptorSet,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &fontDescriptor
		}
	};

	vkUpdateDescriptorSets(device.logicalDevice, arraysize(writeDescriptorSets), writeDescriptorSets, 0, nullptr);

	// Prepare a dedicated pipeline for ui overlay rendering.
	VkPushConstantRange pushConstantRange = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(PushConstantBlock)
	};

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptorSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
	};

	VkCheck(vkCreatePipelineLayout(device.logicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout), "Failed to create UI overlay pipeline layout");

	VkVertexInputBindingDescription vertexInputBindings[] = {
		{
			.binding = 0,
			.stride = sizeof(ImDrawVert),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		}
	};

	VkVertexInputAttributeDescription vertexAttributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(ImDrawVert, pos)
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(ImDrawVert, uv)
		},
		{
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.offset = offsetof(ImDrawVert, col)
		},
	};

	VkPipelineVertexInputStateCreateInfo vertexInputState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = arraysize(vertexInputBindings),
		.pVertexBindingDescriptions = vertexInputBindings,
		.vertexAttributeDescriptionCount = arraysize(vertexAttributes),
		.pVertexAttributeDescriptions = vertexAttributes
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE
	};

	VkPipelineRasterizationStateCreateInfo rasterizationState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f
	};

	VkPipelineColorBlendAttachmentState blendAttachmentState = {
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo colorBlendState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendAttachmentState
	};

	VkPipelineDepthStencilStateCreateInfo depthStencilState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_FALSE,
		.depthWriteEnable = VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_ALWAYS,
		.back = {
			.compareOp = VK_COMPARE_OP_ALWAYS
		}
	};

	VkPipelineViewportStateCreateInfo viewportState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1
	};

	VkPipelineMultisampleStateCreateInfo multisampleState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = rasterizationSamples
	};

	VkDynamicState dynamicStateEnables[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = arraysize(dynamicStateEnables),
		.pDynamicStates = dynamicStateEnables
	};

	VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &colorFormat,
		.depthAttachmentFormat = depthFormat,
		.stencilAttachmentFormat = depthFormat
	};

	VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &pipelineRenderingCreateInfo,
		.stageCount = arraysize(shaders),
		.pStages = shaders,
		.pVertexInputState = &vertexInputState,
		.pInputAssemblyState = &inputAssemblyState,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizationState,
		.pMultisampleState = &multisampleState,
		.pDepthStencilState = &depthStencilState,
		.pColorBlendState = &colorBlendState,
		.pDynamicState = &dynamicState,
		.layout = pipelineLayout
	};

	VkCheck(vkCreateGraphicsPipelines(device.logicalDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline), "Failed to create UI overlay pipeline");
}

// TODO: we currently free vertex buffers while they are being used by a command buffer
//		 seems like imgui_implVulkan gets around this by keeping a vertex/index buffer
//		 for each frame in flight. Sacha's implementation doesn't seem to do this, however, so idk.
void UIOverlay::Update(const Device& device) {
	ImDrawData* drawData = ImGui::GetDrawData();

	if (!drawData) { return; }

	VkDeviceSize vertexBufferSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
	VkDeviceSize indexBufferSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

	if ((vertexBufferSize == 0) || (indexBufferSize == 0)) { return; }

	// Create or resize the vertex / index buffers
	if ((vertexBuffer.buffer == VK_NULL_HANDLE) || (vertexCount < drawData->TotalVtxCount)) {
		vertexBuffer.unmap(device.logicalDevice);	// unmap
		vertexBuffer.destroy(device.logicalDevice);	// free buffer + buffer memory
		VkCheck(device.CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, vertexBufferSize, &vertexBuffer), "Failed to create ui overlay vertex buffer");
		vertexCount = drawData->TotalVtxCount;
		VkCheck(vertexBuffer.map(device.logicalDevice), "Failed to map ui overlay vertex buffer");
	}

	if ((indexBuffer.buffer == VK_NULL_HANDLE) || (indexCount < drawData->TotalIdxCount)) {
		indexBuffer.unmap(device.logicalDevice);	// unmap
		indexBuffer.destroy(device.logicalDevice);	// free buffer + buffer memory
		VkCheck(device.CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, indexBufferSize, &indexBuffer), "Failed to create ui overlay index buffer");
		indexCount = drawData->TotalIdxCount;
		VkCheck(indexBuffer.map(device.logicalDevice), "Failed to map ui overlay index buffer");
	}

	// Upload vertex / index data into a single contiguous GPU buffer
	ImDrawVert* vertexDst = (ImDrawVert*)vertexBuffer.mapped;
	ImDrawIdx* indexDst = (ImDrawIdx*)indexBuffer.mapped;
	for (int i = 0; i < drawData->CmdListsCount; i++) {
		ImDrawList* cmdList = drawData->CmdLists[i];

		memcpy(vertexDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(indexDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

		vertexDst += cmdList->VtxBuffer.Size;
		indexDst += cmdList->IdxBuffer.Size;
	}

	VkCheck(vertexBuffer.Flush(device.logicalDevice), "Failed to flush ui overlay vertex buffer");
	VkCheck(indexBuffer.Flush(device.logicalDevice), "Failed to flush ui overlay index buffer");
}

void UIOverlay::Draw(VkCommandBuffer cmdBuffer) {
	ImDrawData* imDrawData = ImGui::GetDrawData();
	int32_t vertexOffset = 0;
	int32_t indexOffset = 0;

	if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, 0);

	pushConstantBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
	pushConstantBlock.translate = glm::vec2(-1.0f);
	vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantBlock), &pushConstantBlock);

	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.buffer, offsets);
	vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

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

void UIOverlay::Resize(u32 width, u32 height) {
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)width, (float)height);
}

void UIOverlay::Free(const Device& device) {
	if (vertShader.module) {
		vkDestroyShaderModule(device.logicalDevice, vertShader.module, nullptr);
	}
	if (fragShader.module) {
		vkDestroyShaderModule(device.logicalDevice, fragShader.module, nullptr);
	}

	vertexBuffer.destroy(device.logicalDevice);
	indexBuffer.destroy(device.logicalDevice);

	vkDestroyImageView(device.logicalDevice, fontView, nullptr);
	vkDestroyImage(device.logicalDevice, fontImage, nullptr);
	vkFreeMemory(device.logicalDevice, fontMemory, nullptr);
	vkDestroySampler(device.logicalDevice, sampler, nullptr);
	vkDestroyDescriptorSetLayout(device.logicalDevice, descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(device.logicalDevice, descriptorPool, nullptr);
	vkDestroyPipelineLayout(device.logicalDevice, pipelineLayout, nullptr);
	vkDestroyPipeline(device.logicalDevice, pipeline, nullptr);
}