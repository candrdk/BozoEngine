#pragma once

#include "../Core/ResourceManager.h"
#include "../Core/Graphics.h"
#include "../Core/Pool.h"

#include "VulkanDevice.h"

#include <volk/volk.h>

// We are loading vulkan functions through volk
#define VMA_STATIC_FULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

struct VulkanBuffer {
	VkBuffer		buffer     = VK_NULL_HANDLE;
	VmaAllocation	allocation = VK_NULL_HANDLE;
	u8*			    mapped     = nullptr;
	VkDeviceSize	size       = 0;
    Memory          type       = Memory::Default;
};

struct VulkanTexture {
    VkImage image            = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkSampler sampler        = VK_NULL_HANDLE;
    VkFormat format          = VK_FORMAT_UNDEFINED;
    VkImageLayout layout     = VK_IMAGE_LAYOUT_UNDEFINED;   // TODO: this field is not synced with image barriers - should it be? If not, maybe rename
    VkImageType type         = VK_IMAGE_TYPE_MAX_ENUM;

    u32 usage                = Usage::USAGE_NONE;

    u32 width                = 0;
    u32 height               = 0;
    u32 numLayers            = 1;
    u32 numMipLevels         = 1;
	u32 samples              = 1;

    VkImageView srv          = VK_NULL_HANDLE;
	VkImageView rtv[8]       = {};
	VkImageView dsv[8]       = {};

    VkRenderingAttachmentInfo GetAttachmentInfo(u32 layer = 0) const {
        return {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = dsv[layer] ? dsv[layer] : rtv[layer],
            .imageLayout = layout,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = srv ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE
        };
    }
};

struct VulkanBindGroupLayout {
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    u32 bindingCount                = 0;
    Binding bindings[8]             = {};
};

struct VulkanBindGroup {
    VkDescriptorSet         set    = VK_NULL_HANDLE;
    Handle<BindGroupLayout> layout = {};
};

struct VulkanPipeline {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline     = VK_NULL_HANDLE;
};

class VulkanResourceManager final : public ResourceManager {
public:
    VulkanResourceManager() {
        m_device = VulkanDevice::impl();
    };

    ~VulkanResourceManager() {
        Check(m_buffers.size()          == 0, "Pool not empty! Still contains %u items!", m_buffers.size());
        Check(m_textures.size()         == 0, "Pool not empty! Still contains %u items!", m_textures.size());
        Check(m_bindgroupLayouts.size() == 0, "Pool not empty! Still contains %u items!", m_bindgroupLayouts.size());
        Check(m_pipelines.size()        == 0, "Pool not empty! Still contains %u items!", m_pipelines.size());
    }

    static VulkanResourceManager* impl() { return (VulkanResourceManager*)ptr; }

    Handle<Buffer>          CreateBuffer(const BufferDesc&& desc);
    Handle<Texture>         CreateTexture(const TextureDesc&& desc);
    Handle<BindGroup>       CreateBindGroup(const BindGroupDesc&& desc);
    Handle<BindGroupLayout> CreateBindGroupLayout(const BindGroupLayoutDesc&& desc);
    Handle<Pipeline>        CreatePipeline(const PipelineDesc&& desc);

    // Create texture with initial data
    Handle<Texture> CreateTexture(const void* data, const TextureDesc&& desc);
    void GenerateMipmaps(Handle<Texture> handle);

    // TODO: Race condition: can't update bindgroups while they're in use by gpu.
    //       Should this be user code responsibility? How to synchronize this?
    void UpdateBindGroupTextures(Handle<BindGroup> bindgroup, span<const TextureBinding> textures);
    void UpdateBindGroupBuffers(Handle<BindGroup> bindgroup, span<const BufferBinding> buffers);

    void DestroyBuffer(Handle<Buffer> handle);
    void DestroyTexture(Handle<Texture> handle);
    void DestroyBindGroupLayout(Handle<BindGroupLayout> handle);
    void DestroyPipeline(Handle<Pipeline> handle);

    bool WriteBuffer(Handle<Buffer> handle, const void* data, u32 size, u32 offset = 0);
    bool Upload(Handle<Buffer> handle, const void* data, u32 size);

    // TODO: vkCmdCopy should be pushed to a transfer queue. Backend should handle the semaphore
    // for texture uploads internally. One semaphore per frame.
    //
    // Also add an UploadTexture method that takes: Handle<Texture>, Handle<Buffer>, byteOffset.
    // Would allow user to alloc GPU-visible temp memory, load texture there directly, then call
    // the upload command. Eventually add span versions of these to allow usercode to load N
    // textures into a buffer and upload them to N textures at once. This won\t be needed until
    // we start loading/streaming larger scenes. Current interface is fine for now.
    bool Upload(Handle<Texture> handle, const void* data, const TextureRange& range);

    bool IsMapped(Handle<Buffer> handle);
    u8* GetMapped(Handle<Buffer> handle);

    bool MapBuffer(Handle<Buffer>   handle);
    void UnmapBuffer(Handle<Buffer> handle);
    
    VulkanBuffer*          GetBuffer(Handle<Buffer> handle)                   { return m_buffers.get(handle); }
    VulkanTexture*         GetTexture(Handle<Texture> handle)                 { return m_textures.get(handle); }
    VulkanBindGroup*       GetBindGroup(Handle<BindGroup> handle)             { return m_bindgroups.get(handle); }
    VulkanBindGroupLayout* GetBindGroupLayout(Handle<BindGroupLayout> handle) { return m_bindgroupLayouts.get(handle); }
    VulkanPipeline*        GetPipeline(Handle<Pipeline> handle)               { return m_pipelines.get(handle); }

private:
    VulkanDevice* m_device;

    Pool<VulkanBuffer,          Buffer>          m_buffers;
    Pool<VulkanTexture,         Texture>         m_textures;
    Pool<VulkanBindGroup,       BindGroup>       m_bindgroups;
    Pool<VulkanBindGroupLayout, BindGroupLayout> m_bindgroupLayouts;
    Pool<VulkanPipeline,        Pipeline>        m_pipelines;
};