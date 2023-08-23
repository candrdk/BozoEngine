#pragma once

#include "Common.h"
#include "Device.h"
#include "BindGroup.h"

// TODO: move these overloads + some of the enums out of this file
// and start using them elsewhere - i.e. use Format enum in pipeline desc

// Scoped enum concept (enum class, class {enum} and struct{enum})
template <typename T>
concept scoped_enum = std::is_enum_v<T>
&& not std::is_convertible_v<T, std::underlying_type_t<T>>;

// Define bitwise operators for scoped enums: &, |, ^, ~, &=, |=
template <scoped_enum E, typename underlying = std::underlying_type<E>::type>
constexpr E operator&(E lhs, E rhs) { return static_cast<E>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs)); }

template <scoped_enum E, typename underlying = std::underlying_type<E>::type>
constexpr E operator|(E lhs, E rhs) { return static_cast<E>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs)); }

template <scoped_enum E, typename underlying = std::underlying_type<E>::type>
constexpr E operator^(E lhs, E rhs) { return static_cast<E>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs)); }

template <scoped_enum E, typename underlying = std::underlying_type<E>::type>
constexpr E operator~(E v) { return static_cast<E>(~static_cast<underlying>(v)); }

template <scoped_enum E, typename underlying = std::underlying_type<E>::type>
constexpr E operator&=(E& lhs, E rhs) { lhs = static_cast<E>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs)); return lhs; }

template <scoped_enum E, typename underlying = std::underlying_type<E>::type>
constexpr E operator|=(E& lhs, E rhs) { lhs = static_cast<E>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs)); return lhs; }

template <scoped_enum E, typename underlying = std::underlying_type<E>::type>
constexpr E operator^=(E& lhs, E rhs) { lhs = static_cast<E>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs)); return lhs; }

template <scoped_enum E>
constexpr bool HasFlag(E lhs, E rhs) { return (lhs & rhs) == rhs; }

enum class Usage {
	DEFAULT,	// CPU no access	GPU read/write
	UPLOAD,	    // CPU write		GPU read
	READBACK	// CPU read			GPU write
};

enum class Format {
	UNDEFINED,
	RGBA8_UNORM,
	RGBA8_SRGB,
	D24_UNORM_S8_UINT
};

enum class BindFlag {
	NONE = 0,
	SHADER_RESOURCE = 1 << 0,
	RENDER_TARGET   = 1 << 1,
	DEPTH_STENCIL   = 1 << 2
};

struct TextureDesc {
	enum class Type : u32 {
		TEXTURE1D,
		TEXTURE2D,
		TEXTURE3D,
		TEXTURECUBE		// TODO: kind of the odd one out in this enum, but fine to keep it here for now?
	} type = Type::TEXTURE2D;

	u32 width;
	u32 height;
	u32 depth       = 1;
	u32 arrayLayers = 1;
	u32 mipLevels   = 1;	// 1 indicates we have to generate them ourselves
	bool generateMipLevels = false;
	u32 samples     = 1;

	Format format = Format::UNDEFINED;
	Usage usage = Usage::DEFAULT;
	BindFlag bindFlags = BindFlag::NONE;

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

	BindGroupDesc::TextureBinding GetBinding(u32 binding) const {
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
	static Texture CreateCubemap(Device& device, Format format, Usage usage, BindFlag bindFlags, span<const char* const> files);
};