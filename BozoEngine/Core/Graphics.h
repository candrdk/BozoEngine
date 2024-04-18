#pragma once

#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

#include <vector>
#include "../span.h"

// REVISIT THESE, LOOK INTO BETTER LOGGING
#define Check(expression, message, ...) do {	\
	if(!(expression)) {							\
		fprintf(stderr, "\nCheck `" #expression "` failed in %s at line %d.\n\t(file: %s)\n", __FUNCTION__, __LINE__, __FILE__); \
		if(message[0] != '\0') {				\
			char _check_str[512];				\
			_snprintf(_check_str, arraysize(_check_str), message, __VA_ARGS__); \
			fprintf(stderr, "Message: '%s'\n", _check_str); \
		}										\
		abort();								\
	} } while(0)

template <typename T, size_t N>
char(&ArraySizeHelper(T(&array)[N]))[N];

#define arraysize(array) (sizeof(ArraySizeHelper(array)))

// This should probably be moved to some util header
inline std::vector<u32> ReadShaderSpv(const char* path) {
    FILE* fp = fopen(path, "rb");
    Check(fp != nullptr, "File: '%s' failed to open", path);

    fseek(fp, 0, SEEK_END);
    long byteLength = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    Check(byteLength > 0, "File: '%s' was empty", path);

    Check((byteLength & 3) == 0, "File: '%s' size was not aligned by 4", path);
    std::vector<u32> buffer(byteLength / 4);

    size_t bytesRead = fread(buffer.data(), 1, byteLength, fp);
    Check(bytesRead == byteLength, "Failed to read all contents of '%s'", path);
    fclose(fp);

    return buffer;
}

constexpr bool HasFlag(u32 value, u32 flag) {
    return (value & flag) == flag;
}

enum class Memory {
    Default,        // Any resource frequently written and read on GPU, i.e. render targets, attachments, etc.
    Upload,         // Staging buffers that you want to map and fill from CPU code, then use as a source of transfer to some GPU location.
    Readback        // Buffers for data written by or transferred from the GPU that you want to read back on the CPU, e.g. results of some computations.
};

enum class Format {
	UNDEFINED,
	RGBA8_UNORM,
	RGBA8_SRGB,
    BGRA8_SRGB,
	D24_UNORM_S8_UINT,
	D32_SFLOAT,
    RG32_SFLOAT,
    RGB32_SFLOAT,
    RGBA32_SFLOAT
};

enum Usage : u32 {
    USAGE_NONE		    = 0,
	SHADER_RESOURCE		= 1 << 0,
	TRANSFER_SRC		= 1 << 1,
	TRANSFER_DST		= 1 << 2,

	RENDER_TARGET		= 1 << 3,
	DEPTH_STENCIL		= 1 << 4,

	VERTEX_BUFFER		= 1 << 5,
	INDEX_BUFFER		= 1 << 6,
	UNIFORM_BUFFER		= 1 << 7
};

enum ShaderStage : u32 {
    STAGE_NONE = 0,
    VERTEX     = 1 << 0,
    FRAGMENT   = 1 << 1
};

enum class CompareOp {
    Never,
    Always,
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual
};

enum class StencilOp {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap
};

enum class CullMode {
    None,
    Front,
    Back
};

enum class FrontFace {
    CounterClockwise,
    Clockwise
};

enum class PolygonMode {
    Fill, 
    Line,
    Point
};

enum class IndexType {
    UINT16,
    UINT32
};

// Forward declare Pool so Handle can befriend it
template <typename T, typename H> 
class Pool;

// TODO: Should handles carry some resource state bits? How should they be set?
//       Handles can refer to allocated, but not initialized data - or the init
//       might have failed. 2 bits could be used for memory safety checks.
//
// 1. Move resource initialization to the VulkanResource constructor.
//    Take a mutable reference to the corresponding resource handle,
//    and update it accordingly.
//
// 2. Make an Initializable concept and constrain the underlying pool
//    data type. Pool then calls T.init(std::forward<Args...>) instead
//    of using placement new. Init functions should then return a bool
//    indicating whether they were successful or not.
//
// 3. Keep resource creation logic in the ResourceManager. Store resource
//    state in the VulkanResource object instead of the handle. User code
//    would then have to call rm->GetResourceState(handle); to check state.
//
template <typename U>
class Handle {
    enum State {
        Invalid   = 0,  // Resource is invalid, i.e. no underlying allocation
        Allocated = 1,  // Resource is allocated but not initialized
        Failed    = 2,  // Resource is allocated but initialization failed
        Valid     = 3   // Resource is allocated and initialized
    };

    u16 index;
    u16 generation;

    template <typename T, typename H>
    friend class Pool;
};

static_assert(sizeof(Handle<void>) == sizeof(u32));

// empty types for resource handle type safety
struct Texture;
struct BindGroup;
struct BindGroupLayout;
struct Buffer;
struct Shader;
struct Pipeline;


struct BufferDesc {
    const char* debugName = "";

    u64 byteSize  = 0;
    u32 usage     = Usage::USAGE_NONE;
    Memory memory = Memory::Default;
};

struct TextureRange {
    u32 width        = 0;
    u32 height       = 0;

    u32 layer        = 0;
    u32 numLayers    = 1;
    u32 mipLevel     = 0;
    u32 numMipLevels = 1;
};

struct TextureDesc {
    const char* debugName = "";

    enum class Type {
		TEXTURE2D,
		TEXTURE2DARRAY,
		TEXTURE3D,
		TEXTURECUBE
	} type = Type::TEXTURE2D;

    u32 width;
    u32 height;
	u32 numLayers     = 1;
	u32 numMipLevels  = 1;
	u32 samples       = 1;

	Format format     = Format::UNDEFINED;
	Memory memory     = Memory::Default;
	u32    usage      = Usage::USAGE_NONE;

	bool generateMips = false;

    struct SamplerDesc {
        bool compareOpEnable = false;
        CompareOp compareOp  = CompareOp::Never;
    } sampler;
};

struct Binding {
    enum class Type {
        TEXTURE,
        BUFFER,
        DYNAMIC
    };

    Type type;
    u32 stages = ShaderStage::VERTEX | ShaderStage::FRAGMENT;
    u32 count  = 1;
};

struct BindGroupLayoutDesc {
    const char*         debugName = "";
    span<const Binding> bindings;
};

struct BufferBinding {
    u32 binding;
    Handle<Buffer> buffer;
    u64 offset;
    u64 size;
};

struct TextureBinding {
    u32 binding;
    Handle<Texture> texture;
};

struct BindGroupDesc {
    const char*                debugName = "";
    Handle<BindGroupLayout>    layout    = {};
    span<const TextureBinding> textures  = {};
    span<const BufferBinding>  buffers   = {};
};

// TODO: reconsider the blend mode interface
// Would probably be nicer to simply have:
// { .mask = 0xF, .blendMode = Blend::Alpha }
// Instead of exposing op and factor directly,
// the backend would just handle the different
// blend modes: NONE, ALPHA, PREMULTIPLY, etc.
struct Blend {
    static constexpr Blend NONE() {
        return { .blendEnable = false };
    }

    static constexpr Blend ALPHA(u8 writeMask) {
        return {
            .blendEnable = true,
            .colorWriteMask = writeMask,
            .colorOp = Op::ADD,
            .srcColorFactor = Factor::SRC_ALPHA,
            .dstColorFactor = Factor::ONE_MINUS_SRC_ALPHA,
            .alphaOp = Op::ADD,
            .srcAlphaFactor = Factor::SRC_ALPHA,
            .dstAlphaFactor = Factor::ONE_MINUS_SRC_ALPHA
        };
    }

    static constexpr Blend ADDITIVE(u8 writeMask) {
        return {
            .blendEnable = true,
            .colorWriteMask = writeMask,
            .colorOp = Op::ADD,
            .srcColorFactor = Factor::SRC_ALPHA,
            .dstColorFactor = Factor::ONE,
            .alphaOp = Op::ADD,
            .srcAlphaFactor = Factor::SRC_ALPHA,
            .dstAlphaFactor = Factor::ONE
        };
    }

    static constexpr Blend PREMULTIPLY(u8 writeMask) {
        return {
            .blendEnable = true,
            .colorWriteMask = writeMask,
            .colorOp = Op::ADD,
            .srcColorFactor = Factor::ONE,
            .dstColorFactor = Factor::ONE_MINUS_SRC_ALPHA,
            .alphaOp = Op::ADD,
            .srcAlphaFactor = Factor::ONE,
            .dstAlphaFactor = Factor::ONE_MINUS_SRC_ALPHA
        };
    }

    static constexpr Blend MULTIPLY(u8 writeMask) {
        return {
            .blendEnable = true,
            .colorWriteMask = writeMask,
            .colorOp = Op::ADD,
            .srcColorFactor = Factor::DST_COLOR,
            .dstColorFactor = Factor::ONE_MINUS_SRC_ALPHA,
            .alphaOp = Op::ADD,
            .srcAlphaFactor = Factor::DST_ALPHA,
            .dstAlphaFactor = Factor::ONE_MINUS_SRC_ALPHA
        };
    }

    enum class Op {
        ADD,
        SUBTRACT,
        MIN,
        MAX
    };

    enum class Factor {
        ZERO,
        ONE,
        SRC_COLOR,
        ONE_MINUS_SRC_COLOR,
        DST_COLOR,
        ONE_MINUS_DST_COLOR,
        SRC_ALPHA,
        ONE_MINUS_SRC_ALPHA,
        DST_ALPHA,
        ONE_MINUS_DST_ALPHA
    };

    bool   blendEnable = false;
    u8     colorWriteMask;

    Op     colorOp;
    Factor srcColorFactor;
    Factor dstColorFactor;

    Op     alphaOp;
    Factor srcAlphaFactor;
    Factor dstAlphaFactor;
};

struct GraphicsState {
    span<const Format> colorAttachments = {};
    span<const Blend>  blendStates      = {};

    struct DepthStencilState {
        struct StencilState {
            StencilOp failOp      = StencilOp::Keep;
            StencilOp passOp      = StencilOp::Keep;
            StencilOp depthFailOp = StencilOp::Keep;
            CompareOp compareOp   = CompareOp::Never;
            u32       compareMask = 0xFFFFFFFF;
            u32       writeMask   = 0xFFFFFFFF;
            u32       reference   = 0xFFFFFFFF;
        };

        Format depthStencilFormat = Format::UNDEFINED;

        bool depthTestEnable      = true;
        bool depthWriteEnable     = true;
        CompareOp depthCompareOp  = CompareOp::GreaterEqual;

        bool stencilTestEnable    = false;
        StencilState frontStencilState = {};
        StencilState backStencilState  = {};
    } depthStencilState = {};

    struct RasterizationState {
        bool depthClampEnable = false;
        bool depthBiasEnable  = false;
        float depthBiasConstantFactor;
        float depthBiasClamp;
        float depthBiasSlopeFactor;

        CullMode cullMode     = CullMode::Back;
        FrontFace frontFace   = FrontFace::CounterClockwise;
    } rasterizationState = {};

    // NOTE: Only support 1 vertex input for now. Consider supporting multiple if needed later.
    struct VertexInputState {
        struct Attribute {
            u32 offset;
            Format format;
        };

        u32 vertexStride = 0;
        std::vector<Attribute> attributes = {};
    } vertexInputState = {};

    u32 sampleCount = 1;
};

struct ShaderDesc { 
    span<const u32> spirv = {};
    ShaderStage stage     = ShaderStage::STAGE_NONE;
    const char* entry     = "main"; 
};

struct PipelineDesc {
    const char* debugName = "";

    span<const ShaderDesc> shaderDescs;
    span<const Handle<BindGroupLayout>> bindgroupLayouts;

    GraphicsState graphicsState;
};

// TODO: idk if i want Extent2D, Rect2D, etc structs but here they are for now.
struct Offset2D { i32 x, y; };
struct Extent2D { u32 width, height; };

struct Rect2D {
    Offset2D offset;
    Extent2D extent;
};