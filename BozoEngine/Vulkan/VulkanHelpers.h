#pragma once

#include "../Core/Graphics.h"

#include <volk/volk.h>
#include <vulkan/vk_enum_string_helper.h>

#define VkCheck(vkCall, message) do {					\
	VkResult _vkcheck_result = vkCall;					\
	if(_vkcheck_result != VK_SUCCESS) {					\
		fprintf(stderr, "\nVkCheck failed with `%s` in %s at line %d.\n\t(file: %s)\n", string_VkResult(_vkcheck_result), __FUNCTION__, __LINE__, __FILE__); \
		fprintf(stderr, "Message: '%s'\n", message);	\
		abort();										\
	} } while(0)

#define VkAssert(vkCall) Check((vkCall) == VK_SUCCESS, "")

inline constexpr VkBlendOp ConvertBlendOp(Blend::Op op) {
	switch (op) {
		case Blend::Op::ADD:	  return VK_BLEND_OP_ADD;
		case Blend::Op::SUBTRACT: return VK_BLEND_OP_SUBTRACT;
		case Blend::Op::MIN:	  return VK_BLEND_OP_MIN;
		case Blend::Op::MAX:	  return VK_BLEND_OP_MAX;

		default:
			Check(false, "Unknown blend op: %u", (u32)op);
	}
}

inline constexpr VkBlendFactor ConvertBlendFactor(Blend::Factor factor) {
	switch (factor) {
		case Blend::Factor::ZERO:				 return VK_BLEND_FACTOR_ZERO;
		case Blend::Factor::ONE:				 return VK_BLEND_FACTOR_ONE;
		case Blend::Factor::SRC_COLOR:			 return VK_BLEND_FACTOR_SRC_COLOR;
		case Blend::Factor::ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case Blend::Factor::DST_COLOR:			 return VK_BLEND_FACTOR_DST_COLOR;
		case Blend::Factor::ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case Blend::Factor::SRC_ALPHA:			 return VK_BLEND_FACTOR_SRC_ALPHA;
		case Blend::Factor::ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case Blend::Factor::DST_ALPHA:			 return VK_BLEND_FACTOR_DST_ALPHA;
		case Blend::Factor::ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;

		default:
			Check(false, "Unknown blend factor: %u", (u32)factor);
	}
}

inline constexpr VkIndexType ConvertIndexType(IndexType type) {
	switch (type) {
	case IndexType::UINT16: return VK_INDEX_TYPE_UINT16;
	case IndexType::UINT32: return VK_INDEX_TYPE_UINT32;

	default:
		Check(false, "Unknown index type %u", (u32)type);
	}
}

inline constexpr VkFormat ConvertFormat(Format format) {
	switch (format) {
	case Format::UNDEFINED:			return VK_FORMAT_UNDEFINED;
	case Format::RGBA8_UNORM:		return VK_FORMAT_R8G8B8A8_UNORM;
	case Format::RGBA8_SRGB:		return VK_FORMAT_R8G8B8A8_SRGB;
	case Format::BGRA8_SRGB:		return VK_FORMAT_B8G8R8A8_SRGB;
	case Format::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
	case Format::D32_SFLOAT:		return VK_FORMAT_D32_SFLOAT;
	case Format::RG32_SFLOAT:		return VK_FORMAT_R32G32_SFLOAT;
	case Format::RGB32_SFLOAT:		return VK_FORMAT_R32G32B32_SFLOAT;
	case Format::RGBA32_SFLOAT:		return VK_FORMAT_R32G32B32A32_SFLOAT;

	default: 
		Check(false, "Unknown format %u", (u32)format);
	}
}

inline constexpr Format ConvertFormatVK(VkFormat format) {
	switch (format) {
	case VK_FORMAT_UNDEFINED:			return Format::UNDEFINED;
	case VK_FORMAT_R8G8B8A8_UNORM:		return Format::RGBA8_UNORM;
	case VK_FORMAT_R8G8B8A8_SRGB:		return Format::RGBA8_SRGB;
	case VK_FORMAT_B8G8R8A8_SRGB:		return Format::BGRA8_SRGB;
	case VK_FORMAT_D24_UNORM_S8_UINT:	return Format::D24_UNORM_S8_UINT;
	case VK_FORMAT_D32_SFLOAT:			return Format::D32_SFLOAT;
	case VK_FORMAT_R32G32_SFLOAT:		return Format::RG32_SFLOAT;
	case VK_FORMAT_R32G32B32_SFLOAT:	return Format::RGB32_SFLOAT;
	case VK_FORMAT_R32G32B32A32_SFLOAT:	return Format::RGBA32_SFLOAT;

	default:
		Check(false, "Unknown format %u", (u32)format);
	}
}

inline constexpr VkCompareOp ConvertCompareOp(CompareOp compareOp) {
    switch (compareOp) {
        case CompareOp::Never:         return VK_COMPARE_OP_NEVER;
        case CompareOp::Always:        return VK_COMPARE_OP_ALWAYS;
        case CompareOp::Equal:         return VK_COMPARE_OP_EQUAL;
        case CompareOp::NotEqual:      return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::Less:          return VK_COMPARE_OP_LESS;
        case CompareOp::Greater:       return VK_COMPARE_OP_GREATER;
        case CompareOp::LessEqual:     return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::GreaterEqual:  return VK_COMPARE_OP_GREATER_OR_EQUAL;

        default: 
            Check(false, "Unknown compare op %u", compareOp);
    };
}

inline constexpr VkStencilOp ConvertStencilOp(StencilOp stencilOp) {
    switch (stencilOp) {
        case StencilOp::Keep:           return VK_STENCIL_OP_KEEP;
        case StencilOp::Zero:           return VK_STENCIL_OP_ZERO;
        case StencilOp::Replace:        return VK_STENCIL_OP_REPLACE;
        case StencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case StencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case StencilOp::Invert:         return VK_STENCIL_OP_INVERT;
        case StencilOp::IncrementWrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case StencilOp::DecrementWrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;

        default: 
            Check(false, "Unknown stencil op %u", stencilOp);
    };
}

inline constexpr VkCullModeFlags ConvertCullMode(CullMode cullMode) {
    switch (cullMode) {
        case CullMode::None:  return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;

        default: 
            Check(false, "Unknown cull mode %u", cullMode);
    }
}

inline constexpr VkFrontFace ConvertFrontFace(FrontFace frontFace) {
    switch (frontFace) {
        case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case FrontFace::Clockwise:        return VK_FRONT_FACE_CLOCKWISE;

        default:
            Check(false, "Unknown front face %u", frontFace);
    }
}

inline constexpr VkDescriptorType ConvertDescriptorType(Binding::Type type) {
    switch(type) {
        case Binding::Type::TEXTURE: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case Binding::Type::BUFFER:  return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case Binding::Type::DYNAMIC: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

        default:
            Check(false, "Unknown descriptor type %u", type);
    }
}

inline constexpr VkBufferUsageFlags ParseUsageFlags(u32 value) {
	VkBufferUsageFlags usage = 0;

	if (HasFlag(value, Usage::TRANSFER_SRC))	usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	if (HasFlag(value, Usage::TRANSFER_DST))	usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (HasFlag(value, Usage::VERTEX_BUFFER))	usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (HasFlag(value, Usage::INDEX_BUFFER))	usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (HasFlag(value, Usage::UNIFORM_BUFFER))	usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	return usage;
}

inline constexpr VkAccessFlags2 ParseAccessFlags(u32 value) {
    VkAccessFlags2 flags = 0;

    if (HasFlag(value, Usage::SHADER_RESOURCE)) flags |= VK_ACCESS_2_SHADER_READ_BIT;
	if (HasFlag(value, Usage::RENDER_TARGET))	flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	if (HasFlag(value, Usage::DEPTH_STENCIL))	flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    return flags;
}

inline constexpr VkShaderStageFlags ParseShaderStageFlags(u32 value) {
    VkShaderStageFlags stages = 0;

    if (HasFlag(value, ShaderStage::VERTEX))     stages |= VK_SHADER_STAGE_VERTEX_BIT;
	if (HasFlag(value, ShaderStage::FRAGMENT))   stages |= VK_SHADER_STAGE_FRAGMENT_BIT;

    return stages;
}

inline constexpr VkImageLayout ParseImageLayout(u32 value) {
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

inline constexpr VkImageUsageFlags ParseImageUsage(u32 value) {
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

inline constexpr VkImageType ParseImageType(TextureDesc::Type type) {
    switch (type) {
    case TextureDesc::Type::TEXTURE2D:
    case TextureDesc::Type::TEXTURE2DARRAY:
    case TextureDesc::Type::TEXTURECUBE:
        return VK_IMAGE_TYPE_2D;

    case TextureDesc::Type::TEXTURE3D:
        return VK_IMAGE_TYPE_3D;
    
    default:
        Check(false, "Unknown image type %u", type);
    }
}

inline constexpr u32 FormatStride(VkFormat format) {
    switch (format)
    {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
        return 4;

	case VK_FORMAT_R32G32_SFLOAT:
		return 8;

	case VK_FORMAT_R32G32B32_SFLOAT:
		return 12;

	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return 16;
    
    default:
        Check(false, "No known stride for format %i", format);
    }
}

inline constexpr bool HasDepth(Format format) {
	switch (format) {
	case Format::D24_UNORM_S8_UINT:
	case Format::D32_SFLOAT:
		return true;
	default:
		return false;
	}
}

inline constexpr bool HasStencil(Format format) {
	switch (format) {
	case Format::D24_UNORM_S8_UINT:
		return true;
	default:
		return false;
	}
}

inline constexpr VkImageAspectFlags GetImageAspect(Format format) {
	VkImageAspectFlags aspect = 0;

	if (HasDepth(format))   aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (HasStencil(format)) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
	if (aspect == 0)		aspect |= VK_IMAGE_ASPECT_COLOR_BIT;

	return aspect;
}

inline constexpr VkImageMemoryBarrier2 GetVkImageBarrier(VkImage image, VkImageAspectFlags aspect, Usage srcUsage, Usage dstUsage, u32 baseMip, u32 mipCount, u32 baseLayer, u32 layerCount) {
	VkImageSubresourceRange subresourceRange{
		.aspectMask     = aspect,
		.baseMipLevel   = baseMip,
		.levelCount     = mipCount,
		.baseArrayLayer = baseLayer,
		.layerCount     = layerCount
	};

	VkImageMemoryBarrier2 barrier = { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, 
		.image = image,
		.subresourceRange = subresourceRange
	};

	if (HasFlag(srcUsage, Usage::RENDER_TARGET)) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
	}
	else if (HasFlag(srcUsage, Usage::DEPTH_STENCIL)) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
	}
	else if (HasFlag(srcUsage, Usage::SHADER_RESOURCE)) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
	}
	else if (HasFlag(srcUsage, Usage::TRANSFER_DST)) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}
	else {
		Check(false, "Unsupported srcUsage for image transition: %u", (u32)srcUsage);
	}

	if (HasFlag(dstUsage, Usage::RENDER_TARGET)) {
		barrier.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
	}
	else if (HasFlag(dstUsage, Usage::DEPTH_STENCIL)) {
		barrier.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
	}
	else if (HasFlag(dstUsage, Usage::SHADER_RESOURCE)) {
		barrier.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
	}
	else if (HasFlag(dstUsage, Usage::TRANSFER_DST)) {
		barrier.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}
	else {
		Check(false, "Unsupported dstUsage for image transition: %u", (u32)dstUsage);
	}
	
	return barrier;
}

inline void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange subresourceRange,
	VkPipelineStageFlags2	srcStage,	VkPipelineStageFlags2	dstStage, 
	VkAccessFlags2			srcAccess,	VkAccessFlags2			dstAccess,
	VkImageLayout			srcLayout,	VkImageLayout			dstLayout) 
{
	VkImageMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = srcStage,
		.srcAccessMask = srcAccess,
		.dstStageMask = dstStage,
		.dstAccessMask = dstAccess,
		.oldLayout = srcLayout,
		.newLayout = dstLayout,
		.image = image,
		.subresourceRange = subresourceRange
	};

	VkDependencyInfo dependencyInfo = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier
	};

	vkCmdPipelineBarrier2(cmd, &dependencyInfo);
};

// Cursed type to enum reflection here plz dont look
template <typename T> struct TypeToEnum				{ static constexpr VkObjectType objectType = VK_OBJECT_TYPE_UNKNOWN;               };

template<> struct TypeToEnum<VkInstance>            { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_INSTANCE;              };
template<> struct TypeToEnum<VkPhysicalDevice>      { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_PHYSICAL_DEVICE;       };
template<> struct TypeToEnum<VkDevice>              { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_DEVICE;                };
template<> struct TypeToEnum<VkQueue>               { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_QUEUE;                 };
template<> struct TypeToEnum<VkSemaphore>           { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_SEMAPHORE;             };
template<> struct TypeToEnum<VkCommandBuffer>       { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_COMMAND_BUFFER;        };
template<> struct TypeToEnum<VkFence>               { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_FENCE;                 };
template<> struct TypeToEnum<VkDeviceMemory>        { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;         };
template<> struct TypeToEnum<VkBuffer>              { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_BUFFER;                };
template<> struct TypeToEnum<VkImage>               { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_IMAGE;                 };
template<> struct TypeToEnum<VkEvent>               { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_EVENT;                 };
template<> struct TypeToEnum<VkQueryPool>           { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_QUERY_POOL;            };
template<> struct TypeToEnum<VkBufferView>          { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_BUFFER_VIEW;           };
template<> struct TypeToEnum<VkImageView>           { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_IMAGE_VIEW;            };
template<> struct TypeToEnum<VkShaderModule>        { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_SHADER_MODULE;         };
template<> struct TypeToEnum<VkPipelineCache>       { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_PIPELINE_CACHE;        };
template<> struct TypeToEnum<VkPipelineLayout>      { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;       };
template<> struct TypeToEnum<VkRenderPass>          { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_RENDER_PASS;           };
template<> struct TypeToEnum<VkPipeline>            { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_PIPELINE;              };
template<> struct TypeToEnum<VkDescriptorSetLayout> { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT; };
template<> struct TypeToEnum<VkSampler>             { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_SAMPLER;               };
template<> struct TypeToEnum<VkDescriptorPool>      { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_DESCRIPTOR_POOL;       };
template<> struct TypeToEnum<VkDescriptorSet>       { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET;        };
template<> struct TypeToEnum<VkFramebuffer>         { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_FRAMEBUFFER;           };
template<> struct TypeToEnum<VkCommandPool>         { static constexpr VkObjectType objectType = VK_OBJECT_TYPE_COMMAND_POOL;          };

template <typename T>
inline VkResult VkNameObject(T object, const char* name) {
    VkDebugUtilsObjectNameInfoEXT objectDebugNameInfo = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = TypeToEnum<T>::objectType,
		.objectHandle = (u64)object,
		.pObjectName = name
	};

	return vkSetDebugUtilsObjectNameEXT(VulkanDevice::impl()->vkDevice, &objectDebugNameInfo);
}