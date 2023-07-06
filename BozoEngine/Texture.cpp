// At the moment, we generate our own mip maps if they are not provided by the texture file being loaded
// Look into what the appropriate way to handle this is. Idk if this is the right place to be doing it.

#include "Texture.h"

#pragma warning(disable : 26451 6262)
	#define STB_IMAGE_IMPLEMENTATION
	#include <stb_image.h>
#pragma warning(enable : 26451 6262)

// TODO: move this later - probably to some vulkan::utility header?
void SetImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageSubresourceRange subresourceRange) {
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = subresourceRange
	};

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else {
		Check(false, "Unsupported layout transition");
	}

	vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static void GenerateMipmaps(VkCommandBuffer commandBuffer, const Device& device, VkImage image, VkFormat imageFormat, i32 width, i32 height, u32 mipLevels) {
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(device.physicalDevice, imageFormat, &formatProperties);
	Check(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "Texture image does not support linear blitting");

	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};

	i32 mipWidth = width;
	i32 mipHeight = height;
	for (u32 i = 1; i < mipLevels; i++) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		VkImageBlit blit = {
			.srcSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i - 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
			.srcOffsets = {
				{ 0, 0, 0 },
				{ mipWidth, mipHeight, 1 }
			},
			.dstSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
			.dstOffsets = {
				{ 0, 0, 0 },
				{ mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 }
			}
		};

		vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		if (mipWidth > 1)	mipWidth /= 2;
		if (mipHeight > 1)	mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, nullptr,
		0, nullptr,
		1, &barrier);
}

void Texture::Destroy(const Device& device) {
	vkDestroyImageView(device.device, view, nullptr);
	vkDestroyImage(device.device, image, nullptr);
	vkDestroySampler(device.device, sampler, nullptr);
	vkFreeMemory(device.device, deviceMemory, nullptr);
}

void Texture::LoadFromFile(const char* path, const Device& device, VkQueue copyQueue, VkFormat format, VkImageUsageFlags usage, VkImageLayout requestedImageLayout) {
	stbi_set_flip_vertically_on_load(true);
	int channels;
	stbi_uc* pixels = stbi_load(path, (int*)(&width), (int*)(&height), &channels, STBI_rgb_alpha);
	Check(pixels != nullptr, "Failed to load: `%s`", path);

	// TODO: Set mipLevels according to the file being loaded (if itcotains mip levels)
	mipLevels = 1;
	VkDeviceSize size = width * height * STBI_rgb_alpha;

	// Create a host-visible staging buffer that contains the raw image data
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	VkMemoryRequirements memRequirements;

	VkBufferCreateInfo stagingBufferCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};
	VkCheck(vkCreateBuffer(device.device, &stagingBufferCreateInfo, nullptr, &stagingBuffer), "Failed to create staging buffer");

	vkGetBufferMemoryRequirements(device.device, stagingBuffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = device.GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	};

	VkCheck(vkAllocateMemory(device.device, &allocInfo, nullptr, &stagingBufferMemory), "Failed to allocate vertex buffer memory");
	VkCheck(vkBindBufferMemory(device.device, stagingBuffer, stagingBufferMemory, 0), "Failed to bind DeviceMemory to VkBuffer");

	// Copy texture data into the staging buffer
	void* data;
	VkCheck(vkMapMemory(device.device, stagingBufferMemory, 0, memRequirements.size, 0, &data), "Failed to map staging buffer memory");
	memcpy(data, pixels, size);
	vkUnmapMemory(device.device, stagingBufferMemory);

	// Setup buffer copy regions for each mip level.
	// (We don't use this yet, so mipLevels is always 1)
	std::vector<VkBufferImageCopy> bufferCopyRegions;

	for (u32 i = 0; i < mipLevels; i++) {
		u64 offset = 0; // Should get the image offset to the mip level

		bufferCopyRegions.push_back({
			.bufferOffset = offset,
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
			.imageExtent = {
				.width = glm::max(1u, (u32)width >> i),
				.height = glm::max(1u, (u32)height >> i),
				.depth = 1
			}
		});
	}

	// If file didn't provide any mip levels, generate our own.
	bool generateMipmaps = (requestedImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) && (mipLevels == 1);
	if (generateMipmaps) {
		mipLevels = (u32)(std::floor(std::log2(std::max(width, height))) + 1);
		usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	CreateImage(device, format, usage);

	vkGetImageMemoryRequirements(device.device, image, &memRequirements);

	allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = device.GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	VkCheck(vkAllocateMemory(device.device, &allocInfo, nullptr, &deviceMemory), "Failed to allocate image memory");
	VkCheck(vkBindImageMemory(device.device, image, deviceMemory, 0), "Failed to bind VkDeviceMemory to VkImage");

	VkImageSubresourceRange subResourceRange = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = mipLevels,
		.layerCount = 1
	};

	VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	// Image barrier
	SetImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subResourceRange);

	// Copy mip levels from staging buffer to device local memory
	vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (u32)bufferCopyRegions.size(), bufferCopyRegions.data());

	// Change texture image layout to the requested image layout (often SHADER_READ) after all mip levels have been copied.
	layout = requestedImageLayout;

	if (generateMipmaps) {
		GenerateMipmaps(copyCmd, device, image, format, width, height, mipLevels);
	}
	else {
		SetImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layout, subResourceRange);
	}

	device.FlushCommandBuffer(copyCmd, copyQueue);

	// Clean up staging resources
	vkDestroyBuffer(device.device, stagingBuffer, nullptr);
	vkFreeMemory(device.device, stagingBufferMemory, nullptr);
	stbi_image_free(pixels);

	// Create a default sampler
	CreateDefaultSampler(device);

	// Create the image view
	CreateImageView(device, format);

	// Update the descriptor image info
	descriptor = {
		.sampler = sampler,
		.imageView = view,
		.imageLayout = layout
	};
}

void Texture::CreateFromBuffer(void* buffer, VkDeviceSize bufferSize, const Device& device, VkQueue copyQueue, u32 texWidth, u32 texHeight, VkFormat format, VkImageUsageFlags usage, VkImageLayout requestedImageLayout) {
	Check(buffer != nullptr, "Cannot create image from a null buffer");
	width = texWidth;
	height = texHeight;
	mipLevels = 1;

	// Create a host-visible staging buffer that contains the raw image data
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;

	VkBufferCreateInfo stagingBufferCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = bufferSize,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};
	VkCheck(vkCreateBuffer(device.device, &stagingBufferCreateInfo, nullptr, &stagingBuffer), "Failed to create staging buffer");

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device.device, stagingBuffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = device.GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	};

	VkCheck(vkAllocateMemory(device.device, &allocInfo, nullptr, &stagingBufferMemory), "Failed to allocate vertex buffer memory");
	VkCheck(vkBindBufferMemory(device.device, stagingBuffer, stagingBufferMemory, 0), "Failed to bind DeviceMemory to VkBuffer");

	// Copy texture data into the staging buffer
	void* data;
	VkCheck(vkMapMemory(device.device, stagingBufferMemory, 0, memRequirements.size, 0, &data), "Failed to map staging buffer memory");
	memcpy(data, buffer, bufferSize);
	vkUnmapMemory(device.device, stagingBufferMemory);

	VkBufferImageCopy bufferCopyRegion = {
		.bufferOffset = 0,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.imageExtent = {
			.width = width,
			.height = height,
			.depth = 1
		}
	};

	// Generate our own mip maps
	bool generateMipmaps = (requestedImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) && (mipLevels == 1);
	if (generateMipmaps) {
		mipLevels = (u32)(std::floor(std::log2(std::max(width, height))) + 1);
		usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	CreateImage(device, format, usage);

	vkGetImageMemoryRequirements(device.device, image, &memRequirements);

	allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = device.GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	VkCheck(vkAllocateMemory(device.device, &allocInfo, nullptr, &deviceMemory), "Failed to allocate image memory");
	VkCheck(vkBindImageMemory(device.device, image, deviceMemory, 0), "Failed to bind VkDeviceMemory to VkImage");

	VkImageSubresourceRange subResourceRange = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = mipLevels,
		.layerCount = 1
	};

	VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	// Image barrier
	SetImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subResourceRange);

	// Copy mip levels from staging buffer to device local memory
	vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

	// Change texture image layout to the requested image layout (often SHADER_READ) after all mip levels have been copied.
	layout = requestedImageLayout;

	if (generateMipmaps) {
		GenerateMipmaps(copyCmd, device, image, format, width, height, mipLevels);
	}
	else {
		SetImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layout, subResourceRange);
	}

	device.FlushCommandBuffer(copyCmd, copyQueue);

	// Clean up staging resources
	vkDestroyBuffer(device.device, stagingBuffer, nullptr);
	vkFreeMemory(device.device, stagingBufferMemory, nullptr);

	// Create a default sampler
	CreateDefaultSampler(device);

	// Create the image view
	CreateImageView(device, format);

	// Update the descriptor image info
	descriptor = {
		.sampler = sampler,
		.imageView = view,
		.imageLayout = layout
	};
}

void Texture::CreateImage(const Device& device, VkFormat format, VkImageUsageFlags usage) {
	VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {
			.width = width,
			.height = height,
			.depth = 1
		},
		.mipLevels = mipLevels,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,	// ensure TRANSFER_DST is set for staging
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VkCheck(vkCreateImage(device.device, &imageInfo, nullptr, &image), "Failed to create image");
}

void Texture::CreateDefaultSampler(const Device& device) {
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
		.anisotropyEnable = device.enabledFeatures.samplerAnisotropy,
		.maxAnisotropy = device.enabledFeatures.samplerAnisotropy ? device.properties.limits.maxSamplerAnisotropy : 1.0f,
		// Not sure what exactly this does - seems to not be relevant for our use case?
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		// Max level-of-detail should match mip level count
		.minLod = 0.0f,
		.maxLod = (float)mipLevels,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK
	};

	VkCheck(vkCreateSampler(device.device, &samplerInfo, nullptr, &sampler), "Failed to create texture sampler");
}

void Texture::CreateImageView(const Device& device, VkFormat format) {
	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VkCheck(vkCreateImageView(device.device, &viewInfo, nullptr, &view), "Failed to create image view");
}