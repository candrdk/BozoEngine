#pragma once

#include "Common.h"
#include "Device.h"

struct TextureDesc {
	enum class Type {
		TEXTURE1D,
		TEXTURE2D,
		TEXTURE3D,
		TEXTURECUBE		// TODO: kind of the odd one out in this enum, but fine to keep it here for now?
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

	VkImageView rtv = VK_NULL_HANDLE;
	VkImageView srv = VK_NULL_HANDLE;
	VkImageView dsv = VK_NULL_HANDLE;

	struct Binding {
		u32 binding;
		VkSampler sampler;
		VkImageView view;
		VkImageLayout layout;
	} GetBinding(u32 binding) const {
		return { binding, sampler, srv, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
	}

	VkRenderingAttachmentInfo GetAttachmentInfo() const {
		return {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = dsv ? dsv : rtv,
			.imageLayout = layout,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = srv ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE
		};
	}

	void Destroy(const Device& device);

	static Texture Create(Device& device, const char* file, TextureDesc&& desc);
	static Texture Create(Device& device, const TextureDesc&& desc);

	// Creates a cubemap from the given files, which must be provided in the order:
	// +X, -X, +Y, -Y, +Z, -Z.
	static Texture CreateCubemap(Device& device, Format format, Memory memory, Usage usage, span<const char* const> files);
};