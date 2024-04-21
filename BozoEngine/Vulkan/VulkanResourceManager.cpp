#include "VulkanResourceManager.h"

#include "VulkanHelpers.h"
#include <glm/glm.hpp>

#include <SPIRV-Cross/spirv_cross.hpp>

ResourceManager* ResourceManager::ptr = nullptr;

Handle<Buffer> VulkanResourceManager::CreateBuffer(const BufferDesc&& desc) {
    VulkanBuffer buffer = { .size = desc.byteSize };

    VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = desc.byteSize,
		.usage = ParseUsageFlags(desc.usage)
	};
    
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
    VmaAllocationCreateInfo allocInfo = { .usage = VMA_MEMORY_USAGE_AUTO };
    if (desc.memory == Memory::Default) {
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    else if (desc.memory == Memory::Upload) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                        | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    else if (desc.memory == Memory::Readback) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }

    VkCheck(vmaCreateBuffer(m_device->vmaAllocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, nullptr), "VMA failed to create buffer");
    VkNameObject(buffer.buffer, desc.debugName);

    return m_buffers.insert(buffer);
}

void VulkanResourceManager::DestroyBuffer(Handle<Buffer> handle) {
    VulkanBuffer* buffer = m_buffers.get(handle);

    if (!buffer) return; // TODO: log

    if (IsMapped(handle)) { UnmapBuffer(handle); }

    vmaDestroyBuffer(m_device->vmaAllocator, buffer->buffer, buffer->allocation);
    
    m_buffers.free(handle);
}

bool VulkanResourceManager::IsMapped(Handle<Buffer> handle) {
    VulkanBuffer* buffer = m_buffers.get(handle);

    if (!buffer) return false;

    return buffer->mapped != nullptr;
}

u8* VulkanResourceManager::GetMapped(Handle<Buffer> handle) {
    VulkanBuffer* buffer = m_buffers.get(handle);

    if (!buffer) return nullptr; // TODO: log error
    // if (!buffer->mapped) // TODO: log error

    return buffer->mapped;
}

bool VulkanResourceManager::MapBuffer(Handle<Buffer> handle) {
    VulkanBuffer* buffer = m_buffers.get(handle);

    if (!buffer) return false; // TODO: log error

    return VK_SUCCESS == vmaMapMemory(m_device->vmaAllocator, buffer->allocation, (void**)&buffer->mapped);
}

void VulkanResourceManager::UnmapBuffer(Handle<Buffer> handle) {
    VulkanBuffer* buffer = m_buffers.get(handle);
    
    if (!buffer) return; // TODO: log error

    vmaUnmapMemory(m_device->vmaAllocator, buffer->allocation);
    buffer->mapped = nullptr;
}

static VkImageView CreateView(VkImage image, VkFormat format, TextureDesc::Type type, VkImageAspectFlags aspect, u32 firstLayer, u32 layerCount, u32 firstMip, u32 mipCount) {
	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.format = format,
		.subresourceRange = {
			.aspectMask = aspect,
			.baseMipLevel = firstMip,
			.levelCount = mipCount,
			.baseArrayLayer = firstLayer,
			.layerCount = layerCount
		}
	};

	switch (type) {
	case TextureDesc::Type::TEXTURE2D:		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;		 break;
	case TextureDesc::Type::TEXTURE2DARRAY:	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; break;
	case TextureDesc::Type::TEXTURE3D:		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;		 break;
	case TextureDesc::Type::TEXTURECUBE:	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;	 break;

    default:
        Check(false, "Unknown texture type: %u", type);
	}

	VkImageView view;
	VkCheck(vkCreateImageView(VulkanDevice::impl()->vkDevice, &viewInfo, nullptr, &view), "Failed to create image view");

	return view;
}

void VulkanResourceManager::GenerateMipmaps(Handle<Texture> handle) {
    VulkanTexture* texture = m_textures.get(handle);

    VkCommandBuffer cmd = m_device->GetCommandBufferVK();

    VkImageSubresourceRange subresourceRange = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = texture->numLayers
    };

    // Transition mip0 to to TRANSFER_SRC_OPTIMAL layout 
    ImageBarrier(cmd, texture->image, subresourceRange,
        VK_PIPELINE_STAGE_2_BLIT_BIT,   VK_PIPELINE_STAGE_2_BLIT_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,   VK_ACCESS_TRANSFER_READ_BIT,
        texture->layout,                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Transition the remaining mip levels to to TRANSFER_DST_OPTIMAL layout 
    subresourceRange.baseMipLevel = 1;
    subresourceRange.levelCount = texture->numMipLevels - 1;
    
    ImageBarrier(cmd, texture->image, subresourceRange,
        VK_PIPELINE_STAGE_2_BLIT_BIT,   VK_PIPELINE_STAGE_2_BLIT_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,   VK_ACCESS_TRANSFER_READ_BIT,
        texture->layout,                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    subresourceRange.levelCount = 1;

    for (u32 mip = 1; mip < texture->numMipLevels; mip++) {
        VkImageBlit blit = {
            { VK_IMAGE_ASPECT_COLOR_BIT, mip-1, 0, texture->numLayers },
            { { 0, 0, 0 }, {
                .x = (i32)glm::max(texture->width  >> (mip-1), 1u),
                .y = (i32)glm::max(texture->height >> (mip-1), 1u),
                .z = 1
            } },
            { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, texture->numLayers },
            { { 0, 0, 0 }, {
                .x = (i32)glm::max(texture->width  >> mip, 1u),
                .y = (i32)glm::max(texture->height >> mip, 1u),
                .z = 1
            } }
        };

        vkCmdBlitImage(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        // Transition the newly blitted mip to TRANSFER_SRC_OPTIMAL, so the next mip can be blitted from it
        subresourceRange.baseMipLevel = mip;
        ImageBarrier(cmd, texture->image, subresourceRange,
            VK_PIPELINE_STAGE_2_BLIT_BIT,         VK_PIPELINE_STAGE_2_BLIT_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,         VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }

    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = texture->numMipLevels;
    ImageBarrier(cmd, texture->image, subresourceRange,
        VK_PIPELINE_STAGE_2_BLIT_BIT,			VK_PIPELINE_STAGE_NONE,
        VK_ACCESS_TRANSFER_WRITE_BIT,			VK_ACCESS_NONE,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,	texture->layout);

    m_device->FlushCommandBufferVK(cmd);
}

static u32 CalculateMiplevels(u32 width, u32 height) {
    return (u32)glm::floor(glm::log2(glm::max((float)width, (float)height))) + 1;
}

Handle<Texture> VulkanResourceManager::CreateTexture(const void* data, const TextureDesc&& desc) {
    if (!data) return Handle<Texture>{};

    Handle<Texture> texture = CreateTexture(std::forward<const TextureDesc&&>(desc));

    // If we are generating mips, the data pointer should just contain mip0
    // Otherwise, the data pointer should contain all mips of all levels
    Upload(texture, data, {
        .width = desc.width,
        .height = desc.height,
        .numLayers = desc.numLayers,
        .numMipLevels = desc.generateMips ? 1 : desc.numMipLevels
    });

    if (desc.generateMips) {
        GenerateMipmaps(texture);
    }

    return texture;
}

Handle<Texture> VulkanResourceManager::CreateTexture(const TextureDesc&& desc) {
    VulkanTexture texture = {
        .format       = ConvertFormat(desc.format),
        .layout       = ParseImageLayout(desc.usage),
        .type         = ParseImageType(desc.type),
        .usage        = desc.usage,
        .width        = desc.width,
        .height       = desc.height,
        .numLayers    = desc.numLayers,
        .numMipLevels = desc.generateMips ? CalculateMiplevels(desc.width, desc.height) : desc.numMipLevels,
        .samples      = desc.samples
    };

    VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = texture.type,
		.format = texture.format,
		.extent = {
			.width = texture.width,
			.height = texture.height,
			.depth = 1
		},
		.mipLevels = texture.numMipLevels,
		.arrayLayers = texture.numLayers,
		.samples = (VkSampleCountFlagBits)texture.samples,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = ParseImageUsage(desc.usage)		// Parse usage (shader_resource -> sampled, rendertarget -> attachmemt, etc)
		       | VK_IMAGE_USAGE_TRANSFER_DST_BIT	// We will be copying from staging buffer to the image
			   | (desc.generateMips ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0ull),	// We will be copying from the image to the image to create mip levels
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

    // TODO: Check if any weird cases are missed here?
	if (desc.type == TextureDesc::Type::TEXTURECUBE) {
		imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		Check(texture.numLayers == 6, "Cubemaps must have 6 layers.");
	}

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    };

    vmaCreateImage(m_device->vmaAllocator, &imageInfo, &allocInfo, &texture.image, &texture.allocation, nullptr);

    // TODO: temporary hack. Transition image to desired layout. Not sure how to handle this nicely.
    //       If we are transitioning anyway, user should prob be allowed to specify the layout by passing
    //       an { .initialUsage } in the TextureDesc.
    VkCommandBuffer cmd = m_device->GetCommandBufferVK();

    VkImageSubresourceRange subresourceRange = {
        .aspectMask = GetImageAspect(desc.format),
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS
    };

    ImageBarrier(cmd, texture.image, subresourceRange,
        VK_PIPELINE_STAGE_NONE,     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_NONE,             VK_ACCESS_NONE,
        VK_IMAGE_LAYOUT_UNDEFINED,  texture.layout);
    
    m_device->FlushCommandBufferVK(cmd);

    // IMAGE VIEWS

    // Shader resource views
    if (HasFlag(desc.usage, Usage::SHADER_RESOURCE)) {
        VkImageAspectFlags aspect = HasDepth(desc.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		texture.srv = CreateView(texture.image, texture.format, desc.type, aspect, 0, -1, 0, -1);
    }

    // Render target views : As each layer is rendered to individually, we create a view for each layer
    if (HasFlag(desc.usage, Usage::RENDER_TARGET)) {
		for (u32 layer = 0; layer < texture.numLayers; layer++) {
			texture.rtv[layer] = CreateView(texture.image, texture.format, TextureDesc::Type::TEXTURE2D, VK_IMAGE_ASPECT_COLOR_BIT, layer, 1, 0, 1);
		}
	}
	if (HasFlag(desc.usage, Usage::DEPTH_STENCIL)) {
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT | (HasStencil(desc.format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
		for (u32 layer = 0; layer < texture.numLayers; layer++) {
			texture.dsv[layer] = CreateView(texture.image, texture.format, TextureDesc::Type::TEXTURE2D, aspect, layer, 1, 0, 1);
		}
	}
    
    // SAMPLER
    VkSamplerCreateInfo samplerInfo = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias = 0.0f,
		// Only enable anisotropic filtering if enabled on the device
		.anisotropyEnable = m_device->features.samplerAnisotropy,
		.maxAnisotropy = m_device->features.samplerAnisotropy ? m_device->properties.limits.maxSamplerAnisotropy : 1.0f,
		// In some cases (like shadow maps) the user might want to enable sampler compare ops.
		.compareEnable = desc.sampler.compareOpEnable,
		.compareOp = ConvertCompareOp(desc.sampler.compareOp),
		// Max level-of-detail should match mip level count
		.minLod = 0.0f,
		.maxLod = (float)texture.numMipLevels,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK
	};

    VkCheck(vkCreateSampler(m_device->vkDevice, &samplerInfo, nullptr, &texture.sampler), "Failed to create texture sampler");

    return m_textures.insert(texture);
}

void VulkanResourceManager::DestroyTexture(Handle<Texture> handle) {
    VulkanTexture* texture = m_textures.get(handle);

    if (texture->srv) vkDestroyImageView(m_device->vkDevice, texture->srv, nullptr);

    for (u32 layer = 0; layer < texture->numLayers; layer++) {
		if (texture->rtv[layer]) vkDestroyImageView(m_device->vkDevice, texture->rtv[layer], nullptr);
		if (texture->dsv[layer]) vkDestroyImageView(m_device->vkDevice, texture->dsv[layer], nullptr);
    }
    
	vkDestroySampler(m_device->vkDevice, texture->sampler, nullptr);

    vmaDestroyImage(m_device->vmaAllocator, texture->image, texture->allocation);

    m_textures.free(handle);
}


Handle<BindGroupLayout> VulkanResourceManager::CreateBindGroupLayout(const BindGroupLayoutDesc&& desc) {
    assert(desc.bindings.size() < 8);
    VulkanBindGroupLayout layout = { .bindingCount = (u32)desc.bindings.size() };

    std::vector<VkDescriptorSetLayoutBinding> descriptors(desc.bindings.size());
    
    for (u32 i = 0; i < desc.bindings.size(); i++) {
        layout.bindings[i] = desc.bindings[i];
        descriptors[i] = {
            .binding = i,
            .descriptorType = ConvertDescriptorType(desc.bindings[i].type),
            .descriptorCount = desc.bindings[i].count,
            .stageFlags = ParseShaderStageFlags(desc.bindings[i].stages)
        };
    }

    VkDescriptorSetLayoutCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (u32)descriptors.size(),
        .pBindings = descriptors.data()
    };

    VkCheck(vkCreateDescriptorSetLayout(m_device->vkDevice, &createInfo, nullptr, &layout.setLayout), "Failed to create descriptor set layout");

    VkNameObject(layout.setLayout, desc.debugName);

    return m_bindgroupLayouts.insert(layout);
}

void VulkanResourceManager::DestroyBindGroupLayout(Handle<BindGroupLayout> handle) {
    VulkanBindGroupLayout* layout = m_bindgroupLayouts.get(handle);

    if (!layout) return; // TODO: log

    vkDestroyDescriptorSetLayout(m_device->vkDevice, layout->setLayout, nullptr);
    
    m_bindgroupLayouts.free(handle);
}

Handle<BindGroup> VulkanResourceManager::CreateBindGroup(const BindGroupDesc&& desc) {
    VulkanBindGroup bindgroup = { .layout = desc.layout };

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_device->descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_bindgroupLayouts.get(desc.layout)->setLayout
    };

    VkCheck(vkAllocateDescriptorSets(m_device->vkDevice, &allocInfo, &bindgroup.set), "Failed to allocate descriptor set");

    Handle<BindGroup> handle = m_bindgroups.insert(bindgroup);

    UpdateBindGroupTextures(handle, desc.textures);
    UpdateBindGroupBuffers(handle, desc.buffers);

    VkNameObject(bindgroup.set, desc.debugName);

    return handle;
}

void VulkanResourceManager::UpdateBindGroupTextures(Handle<BindGroup> bindgroup, span<const TextureBinding> textures) {
    std::vector<VkDescriptorImageInfo> descriptors(textures.size());
    std::vector<VkWriteDescriptorSet>  updates(textures.size(), {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_bindgroups.get(bindgroup)->set,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    });
    
    for (u32 i = 0; i < textures.size(); i++) {
        VulkanTexture* texture = m_textures.get(textures[i].texture);
        descriptors[i] = {
            .sampler = texture->sampler,
            .imageView = texture->srv,
            .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
        };
        updates[i].dstBinding = textures[i].binding;
        updates[i].pImageInfo = &descriptors[i];
    }

    vkUpdateDescriptorSets(m_device->vkDevice, (u32)textures.size(), updates.data(), 0, nullptr);
}

void VulkanResourceManager::UpdateBindGroupBuffers(Handle<BindGroup> bindgroup, span<const BufferBinding> buffers) {
    std::vector<VkDescriptorBufferInfo> descriptors(buffers.size());
    std::vector<VkWriteDescriptorSet>  updates(buffers.size(), {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_bindgroups.get(bindgroup)->set,
        .dstArrayElement = 0,
        .descriptorCount = 1
    });

    Handle<BindGroupLayout> layout = m_bindgroups.get(bindgroup)->layout;
    Binding* bindings = m_bindgroupLayouts.get(layout)->bindings;
    
    for (u32 i = 0; i < buffers.size(); i++) {
        descriptors[i] = {
            .buffer = m_buffers.get(buffers[i].buffer)->buffer,
            .offset = buffers[i].offset,
            .range = buffers[i].size
        };

        updates[i].dstBinding = buffers[i].binding;
        updates[i].pBufferInfo = &descriptors[i];
        updates[i].descriptorType = ConvertDescriptorType(bindings[i].type);
    }

    vkUpdateDescriptorSets(m_device->vkDevice, (u32)buffers.size(), updates.data(), 0, nullptr);
}

static VkStencilOpState GetVulkanStencilOpState(GraphicsState::DepthStencilState::StencilState state) {
    return {
        .failOp = ConvertStencilOp(state.failOp),
        .passOp = ConvertStencilOp(state.passOp),
        .depthFailOp = ConvertStencilOp(state.depthFailOp),
        .compareOp = ConvertCompareOp(state.compareOp),
        .compareMask = state.compareMask,
        .writeMask = state.writeMask,
        .reference = state.reference
    };
}

static VkResult CreateVkPipeline(VkPipeline& pipeline, VkPipelineLayout pipelineLayout, span<const ShaderDesc> shaders, GraphicsState desc) {
    VkDynamicState dynamicState[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamicStateInfo = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, 
        .dynamicStateCount = arraysize(dynamicState), 
        .pDynamicStates = dynamicState 
    };

    VkPipelineViewportStateCreateInfo viewportStateInfo = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, 
        .viewportCount = 1, 
        .scissorCount = 1 
    };

    // TODO: If we at some point want to add specialization constants,
    //       just reflect them from the shader spirv.
    VkSpecializationInfo specializationInfo = {};

    // TODO: Enable VK_KHR_maintenance5 and remove shader module creation + deletion code
    std::vector<VkPipelineShaderStageCreateInfo> shaderStageCreateInfos;
    for (const ShaderDesc& shader : shaders) {
        VkShaderModuleCreateInfo shaderModuleCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shader.spirv.size_bytes(),
            .pCode = shader.spirv.data()
        };

        shaderStageCreateInfos.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = (VkShaderStageFlagBits)ParseShaderStageFlags(shader.stage),
            .pName = shader.entry,
            .pSpecializationInfo = &specializationInfo
        });

        vkCreateShaderModule(VulkanDevice::impl()->vkDevice, &shaderModuleCreateInfo, nullptr, &shaderStageCreateInfos.back().module);
    }

    // Build the vertex input descriptions. For now, we just support a single vertex input binding (0)
    VkVertexInputBindingDescription vertexBindingDesc = { .stride = desc.vertexInputState.vertexStride };
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescs;
    for (u32 location = 0; location < desc.vertexInputState.attributes.size(); location++) {
        vertexAttributeDescs.push_back({
            .location = location,
            .format = ConvertFormat(desc.vertexInputState.attributes[location].format),
            .offset = desc.vertexInputState.attributes[location].offset
        });
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBindingDesc,
        .vertexAttributeDescriptionCount = (u32)vertexAttributeDescs.size(),
        .pVertexAttributeDescriptions = vertexAttributeDescs.data()
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };
    
    VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = desc.rasterizationState.depthClampEnable,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = ConvertCullMode(desc.rasterizationState.cullMode),
        .frontFace = ConvertFrontFace(desc.rasterizationState.frontFace),
        .depthBiasEnable = desc.rasterizationState.depthBiasEnable,
        .depthBiasConstantFactor = desc.rasterizationState.depthBiasConstantFactor,
        .depthBiasClamp = desc.rasterizationState.depthBiasClamp,
        .depthBiasSlopeFactor = desc.rasterizationState.depthBiasSlopeFactor,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampeInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = (VkSampleCountFlagBits)desc.sampleCount,
        .sampleShadingEnable = VK_FALSE
    };

    VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc.depthStencilState.depthTestEnable,
        .depthWriteEnable = desc.depthStencilState.depthWriteEnable,
        .depthCompareOp = ConvertCompareOp(desc.depthStencilState.depthCompareOp),
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = desc.depthStencilState.stencilTestEnable,
        .front = GetVulkanStencilOpState(desc.depthStencilState.frontStencilState),
        .back = GetVulkanStencilOpState(desc.depthStencilState.backStencilState),
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f
    };

    Check(desc.blendStates.size() <= desc.colorAttachments.size(), "More blend states passed than color attachments");
    std::vector<VkPipelineColorBlendAttachmentState> blendStates(desc.colorAttachments.size(), { .blendEnable = VK_FALSE, .colorWriteMask = 0xF });
    for (u32 i = 0; i < desc.blendStates.size(); i++) {
        const Blend& blend = desc.blendStates[i];
        blendStates[i] = {
            .blendEnable         = blend.blendEnable,
            .srcColorBlendFactor = ConvertBlendFactor(blend.srcColorFactor),
            .dstColorBlendFactor = ConvertBlendFactor(blend.dstColorFactor),
            .colorBlendOp        = ConvertBlendOp(blend.colorOp),
            .srcAlphaBlendFactor = ConvertBlendFactor(blend.srcAlphaFactor),
            .dstAlphaBlendFactor = ConvertBlendFactor(blend.dstAlphaFactor),
            .alphaBlendOp        = ConvertBlendOp(blend.alphaOp),
            .colorWriteMask      = blend.colorWriteMask
        };
    }

    VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = (u32)blendStates.size(),
        .pAttachments = blendStates.data()
    };

    std::vector<VkFormat> colorAttachmentFormats;
    for (Format format : desc.colorAttachments) {
        colorAttachmentFormats.push_back(ConvertFormat(format));
    }

    VkPipelineRenderingCreateInfo renderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = (u32)colorAttachmentFormats.size(),
        .pColorAttachmentFormats = colorAttachmentFormats.data(),
        .depthAttachmentFormat = ConvertFormat(desc.depthStencilState.depthStencilFormat),
        .stencilAttachmentFormat = HasStencil(desc.depthStencilState.depthStencilFormat) 
                                 ? ConvertFormat(desc.depthStencilState.depthStencilFormat)
                                 : VK_FORMAT_UNDEFINED
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCreateInfo,
        .stageCount = (u32)shaderStageCreateInfos.size(),
        .pStages = shaderStageCreateInfos.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssemblyInfo,
        .pViewportState = &viewportStateInfo,
        .pRasterizationState = &rasterizationInfo,
        .pMultisampleState = &multisampeInfo,
        .pDepthStencilState = &depthStencilInfo,
        .pColorBlendState = &colorBlendStateInfo,
        .pDynamicState = &dynamicStateInfo,
        .layout = pipelineLayout
    };

    VkResult res = vkCreateGraphicsPipelines(VulkanDevice::impl()->vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    // Clean up shader modules - this is safe to do once the pipeline has been created.
    for (VkPipelineShaderStageCreateInfo& shader : shaderStageCreateInfos) {
        vkDestroyShaderModule(VulkanDevice::impl()->vkDevice, shader.module, nullptr);
    }

    return res;
}

static VkResult CreateVkPipelineLayout(VkPipelineLayout& pipelineLayout, std::vector<VkDescriptorSetLayout>& setLayouts, span<const ShaderDesc> shaders) {
    VkPushConstantRange pushConstants = {};
    std::vector<VkSpecializationMapEntry> mapEntries = {};
    for (const ShaderDesc& shader : shaders) {
        spirv_cross::Compiler comp(shader.spirv.data(), shader.spirv.size());
        spirv_cross::ShaderResources resources = comp.get_shader_resources();
        auto ranges = comp.get_active_buffer_ranges(resources.push_constant_buffers.front().id);

        for (spirv_cross::BufferRange& r : ranges) {
            pushConstants.size = glm::max(pushConstants.size, u32(r.offset + r.range));
        }

        if (ranges.size()) {
            pushConstants.stageFlags |= ParseShaderStageFlags(shader.stage);
        }
        
        if (setLayouts.empty()) {
            // TODO: Consider reflecting descriptor set layouts from shader if not passed explicitly.
            //       Not sure if I like this. For now, we just require them to always be made explicitly.
            Check(false, "Descriptor set layout reflection has not been implemented yet.");
        }
    }
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = (u32)setLayouts.size(),
        .pSetLayouts = setLayouts.data(),
        .pushConstantRangeCount = pushConstants.stageFlags != 0,
        .pPushConstantRanges = (pushConstants.stageFlags != 0) ? &pushConstants : nullptr
    };

    return vkCreatePipelineLayout(VulkanDevice::impl()->vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout);
}

Handle<Pipeline> VulkanResourceManager::CreatePipeline(const PipelineDesc&& desc) {
    VulkanPipeline pipeline;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    for (const Handle<BindGroupLayout> layout : desc.bindgroupLayouts) {
        descriptorSetLayouts.push_back(m_bindgroupLayouts.get(layout)->setLayout);
    }

    CreateVkPipelineLayout(pipeline.layout, descriptorSetLayouts, desc.shaderDescs);
    CreateVkPipeline(pipeline.pipeline, pipeline.layout, desc.shaderDescs, desc.graphicsState);

    VkNameObject(pipeline.layout,   desc.debugName);
    VkNameObject(pipeline.pipeline, desc.debugName);

    return m_pipelines.insert(pipeline);
}

void VulkanResourceManager::DestroyPipeline(Handle<Pipeline> handle) {
    VulkanPipeline* pipeline = m_pipelines.get(handle);
    
    if (!pipeline) return; // TODO log "invalid pipeline handle"

    vkDestroyPipelineLayout(m_device->vkDevice, pipeline->layout, nullptr);
    vkDestroyPipeline(m_device->vkDevice, pipeline->pipeline, nullptr);

    m_pipelines.free(handle);
}

bool VulkanResourceManager::WriteBuffer(Handle<Buffer> handle, const void* data, u32 size, u32 offset) {
    if (!data) return false; // TODO log "no data"

    VulkanBuffer* buffer = m_buffers.get(handle);

    if (!buffer)                      return false; // TODO log "invalid buffer handle"
    if (!MapBuffer(handle))           return false; // TODO log "failed to map buffer"
    if (offset + size > buffer->size) return false; // TODO log "upload size greater than buffer size"

    memcpy(buffer->mapped + offset, data, size);

    // Buffer needs to be flushed if allocation is not coherent.
    // VMA automatically ignores this call if the allocation is coherent.
    VkResult res = vmaFlushAllocation(m_device->vmaAllocator, buffer->allocation, offset, size);

    UnmapBuffer(handle);

    return res == VK_SUCCESS;
}

bool VulkanResourceManager::Upload(Handle<Buffer> handle, const void* data, u32 size) {
    Handle<Buffer> staging = CreateBuffer({
        .debugName = "BufferUploadStagingBuffer",
        .byteSize  = size,
        .usage     = Usage::TRANSFER_SRC,
        .memory    = Memory::Upload
    });

    VulkanBuffer* src = m_buffers.get(staging);
    VulkanBuffer* dst = m_buffers.get(handle);

    Check(data, "data cannot be nullptr");
    Check(dst, "Invalid buffer handle passed to Upload");
    Check(src, "Failed to create staging buffer");
    Check(src->size <= dst->size, "Source data does not fit in destination buffer memory");

    Check(WriteBuffer(staging, data, size), "Failed to write data to staging buffer");

    VkBufferCopy region = { .size = size };

    VkCommandBuffer cmd = m_device->GetCommandBufferVK();
    vkCmdCopyBuffer(cmd, src->buffer, dst->buffer, 1, &region);
    m_device->FlushCommandBufferVK(cmd);

    DestroyBuffer(staging);

    return true;
}

static constexpr u32 CalculateTextureByteSize(VkFormat format, const TextureRange& range) {
    u32 byteSize = 0;

    for (u32 mip = range.mipLevel; mip < range.numMipLevels; mip++) {
        u32 mipWidth  = glm::max(range.width  >> mip, 1u);
        u32 mipHeight = glm::max(range.height >> mip, 1u);
        byteSize += FormatStride(format) * mipWidth * mipHeight;
    }

    return byteSize * range.numLayers;
}

static constexpr bool ValidateTextureRange(VulkanTexture* texture, const TextureRange& range) {
    if (range.width    > texture->width)        return false;
    if (range.height   > texture->height)       return false;
    if (range.layer    > texture->numLayers)    return false;
    if (range.mipLevel > texture->numMipLevels) return false;

    if (range.layer    + range.numLayers    > texture->numLayers)    return false;
    if (range.mipLevel + range.numMipLevels > texture->numMipLevels) return false;
    
    return true;
}

bool VulkanResourceManager::Upload(Handle<Texture> handle, const void* data, const TextureRange& range) {
    VulkanTexture* texture = m_textures.get(handle);

    Check(data, "data cannot be nullptr");
    Check(texture->type == VK_IMAGE_TYPE_2D, "Only 2d texture uploads are supported.");
    Check(ValidateTextureRange(texture, range), "Incompatible Texture / TextureRange pair.");

    u32 byteSize = CalculateTextureByteSize(texture->format, range);

    Handle<Buffer> staging = CreateBuffer({
        .debugName = "TextureUploadStagingBuffer",
        .byteSize  = byteSize,
        .usage     = Usage::TRANSFER_SRC,
        .memory    = Memory::Upload
    });

    Check(WriteBuffer(staging, data, byteSize), "Failed to write data to staging buffer");

    std::vector<VkBufferImageCopy> regions;
    u32 offset = 0;
    for (u32 layer = range.layer; layer < range.numLayers; layer++) {
        for (u32 mip = range.mipLevel; mip < range.numMipLevels; mip++) {
            u32 width  = glm::max(range.width  >> mip, 1u);
            u32 height = glm::max(range.height >> mip, 1u);

            regions.push_back({
                .bufferOffset = offset,
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = mip,
                    .baseArrayLayer = layer,
                    .layerCount = 1
                },
                .imageExtent = { width, height, 1 }
            });

            offset += width * height * FormatStride(texture->format);
        }
    }

    VkImageSubresourceRange subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS
    };

    VkCommandBuffer cmd = m_device->GetCommandBufferVK();

    // Transition Image memory to TRANSFER_DST
    ImageBarrier(cmd, texture->image, subresourceRange,
		VK_PIPELINE_STAGE_NONE,		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_NONE,				VK_ACCESS_TRANSFER_WRITE_BIT,
		texture->layout,	        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkCmdCopyBufferToImage(cmd, m_buffers.get(staging)->buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (u32)regions.size(), regions.data());

    // Transition Image memory back to the intended layout
    ImageBarrier(cmd, texture->image, subresourceRange,
		VK_PIPELINE_STAGE_TRANSFER_BIT,			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,			ParseAccessFlags(texture->usage),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,	texture->layout);

    m_device->FlushCommandBufferVK(cmd);

    DestroyBuffer(staging);

    return true;
}

/*
    Begin transfer cmdBuffer
    Record cmd's
    ImageBarrier w/ queue release op
    Submit transfer cmdBuffer w/ signal semaphore

    Begin gfx cmdBuffer w/ wait semaphore
    ImageBarrier w/ queue acquire op
    Record cmd's
    Submit gfx cmdBuffer

    Design considerations:
    - Batch transfer cmds in 1 submit - single semaphore
    - Set waitSemaphores of gfx cmdBuffer to the transfer semaphore
    - Alternatively, make transfers completely async. Handles to in-transfer
      resources are set to some Transfer state indicating that they cant be
      used. Keep a track of resource handle <-> transfer fence.
      Each BeginFrame, check the transfer fence. If signalled, the resource
      handles can be made valid. Otherwise, they remain in-transfer until the 
      next BeginFrame.
*/