#pragma once

#include "Common.h"

#include "Device.h"

struct TextureDesc {
	enum class Type {
		TEXTURE2D,
		TEXTURE2DARRAY,
		TEXTURE3D,
		TEXTURECUBE
	} type = Type::TEXTURE2D;

	u32 width;
	u32 height;
	u32 depth       = 1;
	u32 arrayLayers = 1;
	u32 mipLevels   = 1;
	u32 samples     = 1;

	Format format = Format::UNDEFINED;
	Memory memory = Memory::DEFAULT;
	Usage  usage  = Usage::NONE;
	bool generateMipLevels = false;

	struct {	// TODO: Shouldn't leak VkCompareOp to user code, define our own enum later...
		bool compareOpEnable	= false;
		VkCompareOp compareOp	= VK_COMPARE_OP_NEVER;
	} sampler = {};

	span<const u8> initialData;
};

struct Texture {
	VkImage image;
	VkDeviceMemory deviceMemory;
	VkSampler sampler;

	VkFormat format;
	VkImageLayout layout;

	u32 width;
	u32 height;
	u32 mipLevels;
	u32 layerCount;

	VkImageView srv = VK_NULL_HANDLE;
	// Maximum of 8 layers
	VkImageView rtv[8] = {};
	VkImageView dsv[8] = {};

	struct Binding {
		u32 binding;
		VkSampler sampler;
		VkImageView view;
		VkImageLayout layout;
	} GetBinding(u32 binding) const {
		return { binding, sampler, srv, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
	}

	VkRenderingAttachmentInfo GetAttachmentInfo(u32 layer = 0) const {
		return {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = dsv[layer] ? dsv[layer] : rtv[layer],
			.imageLayout = layout,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = srv ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE
		};
	}

	void Destroy(const Device& device);

	static Texture Create(const Device& device, const char* file, TextureDesc&& desc);
	static Texture Create(const Device& device, const TextureDesc&& desc);

	// Creates a cubemap from the given files, which must be provided in the order:
	// +X, -X, +Y, -Y, +Z, -Z.
	static Texture CreateCubemap(Device& device, Format format, Memory memory, Usage usage, span<const char* const> files);
};