#include "Texture.h"
#include "Tools.h"
#include "Buffer.h"

#pragma warning(disable : 26451 6262)
	#define STB_IMAGE_IMPLEMENTATION
	#include <stb_image.h>
#pragma warning(default : 26451 6262)

static constexpr VkAccessFlags2 ParseAccessFlags(Usage value) {
	VkAccessFlags2 flags = 0;

	if (HasFlag(value, Usage::SHADER_RESOURCE)) {
		flags |= VK_ACCESS_2_SHADER_READ_BIT;
	}
	if (HasFlag(value, Usage::RENDER_TARGET)) {
		flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	}
	if (HasFlag(value, Usage::DEPTH_STENCIL)) {
		flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}

	return flags;
}

static constexpr VkImageLayout ParseImageLayout(Usage value) {
	if (HasFlag(value, Usage::RENDER_TARGET) || HasFlag(value, Usage::DEPTH_STENCIL)) {
		return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
	}
	else if (HasFlag(value, Usage::SHADER_RESOURCE)) {
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	else {
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}
}

static constexpr VkImageUsageFlags ParseImageUsage(Usage value) {
	VkImageUsageFlags usage = 0;

	if (HasFlag(value, Usage::SHADER_RESOURCE)) {
		usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if (HasFlag(value, Usage::RENDER_TARGET)) {
		usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if (HasFlag(value, Usage::DEPTH_STENCIL)) {
		usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	return usage;
}

static constexpr VkFormat ConvertFormat(Format format) {
	switch (format) {
	case Format::UNDEFINED:			return VK_FORMAT_UNDEFINED;
	case Format::RGBA8_UNORM:		return VK_FORMAT_R8G8B8A8_UNORM;
	case Format::RGBA8_SRGB:		return VK_FORMAT_R8G8B8A8_SRGB;
	case Format::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;

	default: return VK_FORMAT_UNDEFINED;
	}
}

static constexpr u32 FormatStride(Format format) {
	switch (format) {
	case Format::RGBA8_UNORM:
	case Format::RGBA8_SRGB:
	case Format::D24_UNORM_S8_UINT:
		return 4;

	default:
		// TODO: maybe warn here?
		Check(false, "Unsupported format or no known stride, %i", (int)format);
		return 0;
	}
}

static constexpr bool HasDepth(Format format) {
	return HasFlag(format, Format::D24_UNORM_S8_UINT);
}

static constexpr bool HasStencil(Format format) {
	return HasFlag(format, Format::D24_UNORM_S8_UINT);
}

static VkImageView CreateView(const Device& device, VkImage image, VkFormat format, TextureDesc::Type type, VkImageAspectFlags aspect, u32 firstMip = 0, u32 mipCount = -1, u32 firstLayer = 0, u32 layerCount = -1) {
	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM,
		.format = format,
		.subresourceRange = {
			.aspectMask = aspect,
			.baseMipLevel = firstMip,
			.levelCount = mipCount,
			.baseArrayLayer = firstLayer,
			.layerCount = layerCount
		}
	};

	// TODO: Lot of special cases to handle here once we take texture arrays into account
	switch (type) {
	case TextureDesc::Type::TEXTURE1D: viewInfo.viewType = VK_IMAGE_VIEW_TYPE_1D; break;
	case TextureDesc::Type::TEXTURE2D: viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	case TextureDesc::Type::TEXTURE3D: viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D; break;
	case TextureDesc::Type::TEXTURECUBE: viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE; break;
	}

	VkImageView view;
	VkCheck(vkCreateImageView(device.logicalDevice, &viewInfo, nullptr, &view), "Failed to create image view");

	return view;
}

void Texture::Destroy(const Device& device) {
	if (rtv) vkDestroyImageView(device.logicalDevice, rtv, nullptr);
	if (srv) vkDestroyImageView(device.logicalDevice, srv, nullptr);
	if (dsv) vkDestroyImageView(device.logicalDevice, dsv, nullptr);

	vkDestroySampler(device.logicalDevice, sampler, nullptr);
	vkDestroyImage(device.logicalDevice, image, nullptr);
	vkFreeMemory(device.logicalDevice, deviceMemory, nullptr);
}

// TODO: handle channels properly
Texture Texture::CreateCubemap(Device& device, Format format, Memory memory, Usage usage, span<const char* const> files) {
	Check(files.size() == 6, "CubeMaps require a texture for each face");

	int width, height, channels;
	stbi_info(files[0], &width, &height, &channels);

	u32 layerStride = width * height * 4;
	u8* textureData = new u8[layerStride * 6];

	for (u32 layer = 0; layer < 6; layer++) {
		int w, h, c;
		stbi_uc* data = stbi_load(files[layer], &w, &h, &c, STBI_rgb);
		Check(w == width, "All layers must have same width");
		Check(h == height, "All layers must have same height");
		Check(c == channels, "All layers must have same number of channels");

		if (channels == 3) {
			for (int i = 0; i < width * height; i++) {
				textureData[layer * layerStride + 4 * i + 0] = data[3 * i + 0];
				textureData[layer * layerStride + 4 * i + 1] = data[3 * i + 1];
				textureData[layer * layerStride + 4 * i + 2] = data[3 * i + 2];
				textureData[layer * layerStride + 4 * i + 3] = 0xFF;
			}
		}
		else {
			memcpy(textureData + layer * layerStride, data, layerStride);
		}

		stbi_image_free(data);
	}

	Texture texture = Texture::Create(device, TextureDesc{
		.type = TextureDesc::Type::TEXTURECUBE,
		.width = (u32)width,
		.height = (u32)height,
		.arrayLayers = 6,
		.format = format,
		.memory = memory,
		.usage = usage,
		.initialData = span<const u8>(textureData, width * height * 4 * 6)
	});

	delete[] textureData;

	return texture;
}

// TODO: handle when image has more or fewer channels than 4 (i.e. rgb textures w/o alpha)
Texture Texture::Create(Device& device, const char* file, TextureDesc&& desc) {
	stbi_set_flip_vertically_on_load(true);
	i32 texWidth, texHeight, channels;
	stbi_uc* buffer = stbi_load(file, &texWidth, &texHeight, &channels, STBI_rgb_alpha);
	Check(buffer != nullptr, "Failed to load texture: `%s`", file);

	u32 bufferSize = texWidth * texHeight * STBI_rgb_alpha;
	desc.width = (u32)texWidth;
	desc.height = (u32)texHeight;
	desc.initialData = span<const u8>(buffer, bufferSize);

	Texture texture = Texture::Create(device, std::forward<TextureDesc>(desc));

	stbi_image_free(buffer);

	return texture;
}

Texture Texture::Create(Device& device, const TextureDesc&& desc) {
	Texture texture = {
		.format = ConvertFormat(desc.format),
		.layout = ParseImageLayout(desc.usage),
		.width = desc.width,
		.height = desc.height,
		.mipLevels = desc.generateMipLevels ? u32(std::floor(std::log2(std::max(desc.width, desc.height))) + 1) : desc.mipLevels
	};

	VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.format = texture.format,
		.extent = {
			.width = texture.width,
			.height = texture.height,
			.depth = desc.depth
		},
		.mipLevels = texture.mipLevels,
		.arrayLayers = desc.arrayLayers,
		.samples = (VkSampleCountFlagBits)desc.samples,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = ParseImageUsage(desc.usage)		// Parse usage (shader_resource -> sampled, rendertarget -> attachmemt, etc)
		       | VK_IMAGE_USAGE_TRANSFER_DST_BIT	// We will be copying from staging buffer to the image
			   | (desc.generateMipLevels ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0ull),	// We will be coping from the image to the image to create mip levels
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	switch (desc.type) {
	case TextureDesc::Type::TEXTURE1D:	 imageInfo.imageType = VK_IMAGE_TYPE_1D; break;
	case TextureDesc::Type::TEXTURE2D:	 imageInfo.imageType = VK_IMAGE_TYPE_2D; break;
	case TextureDesc::Type::TEXTURE3D:	 imageInfo.imageType = VK_IMAGE_TYPE_3D; break;
	case TextureDesc::Type::TEXTURECUBE: imageInfo.imageType = VK_IMAGE_TYPE_2D; break;
	}

	// TODO: is this check appropriate? Are more checks needed?
	if (desc.type == TextureDesc::Type::TEXTURECUBE) {
		imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		Check(desc.arrayLayers == 6, "Cubemaps must have 6 layers! Array textures aren't supported yet.");
	}

	if (desc.memory == Memory::DEFAULT) {
		VkCheck(vkCreateImage(device.logicalDevice, &imageInfo, nullptr, &texture.image), "Failed to create image");

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device.logicalDevice, texture.image, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memRequirements.size,
			.memoryTypeIndex = device.GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
		};

		VkCheck(vkAllocateMemory(device.logicalDevice, &allocInfo, nullptr, &texture.deviceMemory), "Failed to allocate image memory");
		VkCheck(vkBindImageMemory(device.logicalDevice, texture.image, texture.deviceMemory, 0), "Failed to bind VkDeviceMemory to VkImage");
	}
	else {
		Check(desc.memory == Memory::DEFAULT, "Only default usage has been implemented.");
	}

	// TODO: this part has not really been tested extensively - specifically when it comes to generating mips for texture arrays.
	if (desc.initialData.data() != nullptr) {
		Buffer stagingBuffer = Buffer::Create(device, {
			.debugName = "Texture staging buffer",
			.usage = Usage::TRANSFER_SRC,
			.memory = Memory::UPLOAD,
			.initialData = desc.initialData
		});

		std::vector<VkBufferImageCopy> bufferCopyRegions;
		u32 copyOffset = 0;
		for (u32 layer = 0; layer < desc.arrayLayers; layer++) {
			u32 width  = imageInfo.extent.width;
			u32 height = imageInfo.extent.height;
			u32 depth  = imageInfo.extent.depth;

			for (u32 mip = 0; mip < (desc.generateMipLevels ? 1 : texture.mipLevels); mip++) {
				const u32 num_texels_x = glm::max(1u, width);
				const u32 num_texels_y = glm::max(1u, height);
				const u32 layer_pitch = num_texels_x * num_texels_y * FormatStride(desc.format);

				bufferCopyRegions.push_back({
					.bufferOffset = copyOffset,
					.bufferRowLength = 0,
					.bufferImageHeight = 0,
					.imageSubresource = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel = mip,
						.baseArrayLayer = layer,
						.layerCount = 1
					},
					.imageOffset = {},
					.imageExtent = { width, height, depth}
				});

				copyOffset += layer_pitch * depth;

				width  = glm::max(1u, width  / 2);
				height = glm::max(1u, height / 2);
				depth  = glm::max(1u, depth  / 2);
			}
		}

		VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkImageSubresourceRange subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = VK_REMAINING_MIP_LEVELS,
			.baseArrayLayer = 0,
			.layerCount = VK_REMAINING_ARRAY_LAYERS
		};

		ImageBarrier(copyCmd, texture.image, subresourceRange,
			VK_PIPELINE_STAGE_NONE,		VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_NONE,				VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,	VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (u32)bufferCopyRegions.size(), bufferCopyRegions.data());
		
		// TODO: we currently dont handle generating mips for texture arrays
		if (!desc.generateMipLevels) {
			ImageBarrier(copyCmd, texture.image, subresourceRange,
				VK_PIPELINE_STAGE_TRANSFER_BIT,			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,			ParseAccessFlags(desc.usage),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,	texture.layout);
		}
		else {
			VkFormatProperties formatProperties;
			vkGetPhysicalDeviceFormatProperties(device.physicalDevice, texture.format, &formatProperties);
			Check(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "Texture image does not support linear blitting");

			// TODO: fix this up maybe?
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;

			i32 mipWidth = texture.width;
			i32 mipHeight = texture.height;
			for (u32 i = 1; i < texture.mipLevels; i++) {
				subresourceRange.baseMipLevel = i - 1;

				ImageBarrier(copyCmd, texture.image, subresourceRange,
					VK_PIPELINE_STAGE_TRANSFER_BIT,			VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_TRANSFER_WRITE_BIT,			VK_ACCESS_TRANSFER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,	VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

				VkImageBlit blit = {
					.srcSubresource = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel = i - 1,
						.baseArrayLayer = 0,
						.layerCount = 1
					},
					.srcOffsets = {
						{ 0, 0, 0 },
						{
							.x = std::max(1, i32(texture.width  >> (i-1))),
							.y = std::max(1, i32(texture.height >> (i-1))),
							.z = 1
						}
					},
					.dstSubresource = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel = i,
						.baseArrayLayer = 0,
						.layerCount = 1
					},
					.dstOffsets = {
						{ 0, 0, 0 },
						{
							.x = std::max(1, i32(texture.width  >> i)),
							.y = std::max(1, i32(texture.height >> i)),
							.z = 1
						}
					}
				};

				vkCmdBlitImage(copyCmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

				ImageBarrier(copyCmd, texture.image, subresourceRange,
					VK_PIPELINE_STAGE_TRANSFER_BIT,			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_TRANSFER_READ_BIT,			ParseAccessFlags(desc.usage),
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,	texture.layout);
			}

			subresourceRange.baseMipLevel = texture.mipLevels - 1;
			ImageBarrier(copyCmd, texture.image, subresourceRange,
				VK_PIPELINE_STAGE_TRANSFER_BIT,			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,			ParseAccessFlags(desc.usage),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,	texture.layout);
		}

		device.FlushCommandBuffer(copyCmd, device.graphicsQueue);

		// Clean up staging resources
		stagingBuffer.Destroy(device);
	}

	// Create image views
	if (HasFlag(desc.usage, Usage::SHADER_RESOURCE)) {
		texture.srv = CreateView(device, texture.image, texture.format, desc.type, HasDepth(desc.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
	}
	if (HasFlag(desc.usage, Usage::RENDER_TARGET)) {
		texture.rtv = CreateView(device, texture.image, texture.format, desc.type, VK_IMAGE_ASPECT_COLOR_BIT);
	}
	if (HasFlag(desc.usage, Usage::DEPTH_STENCIL)) {
		texture.dsv = CreateView(device, texture.image, texture.format, desc.type, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
	}

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
		// Max level-of-detail should match mip level count
		.minLod = 0.0f,
		.maxLod = (float)texture.mipLevels,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK
	};

	VkCheck(vkCreateSampler(device.logicalDevice, &samplerInfo, nullptr, &texture.sampler), "Failed to create texture sampler");

	return texture;
}