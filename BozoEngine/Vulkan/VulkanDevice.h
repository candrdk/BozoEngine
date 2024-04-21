#pragma once

#include "../Core/Device.h"
#include "../Core/Graphics.h"

#include <volk/volk.h>

// We are loading vulkan functions through volk
#define VMA_STATIC_FULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

class VulkanCommandBuffer final : public CommandBuffer {
public:
    VulkanCommandBuffer(VkCommandBuffer cmd, u32 index) : m_cmd{cmd} { m_index = index; }

    void BeginRendering(Handle<Texture> depth, u32 layer, u32 width, u32 height);
    void BeginRendering(Extent2D extent, const span<const Handle<Texture>>&& attachments = {}, Handle<Texture> depth = {});
    void BeginRenderingSwapchain(Handle<Texture> depth = {});
    void EndRendering();

    void ImageBarrier(Handle<Texture> texture, Usage srcUsage, Usage dstUsage, u32 baseMip = 0, u32 mipCount = 1, u32 baseLayer = 0, u32 layerCount = 1);

    void SetPipeline(Handle<Pipeline> handle);
    void SetBindGroup(Handle<BindGroup> handle, u32 index, span<const u32> dynamicOffsets = {});
    void PushConstants(void* data, u32 offset, u32 size, u32 stages);

    void SetVertexBuffer(Handle<Buffer> handle, u64 offset);
    void SetIndexBuffer(Handle<Buffer> handle, u64 offset, IndexType type);

    void SetScissor(const Rect2D& scissor);
    void SetViewport(float width, float height);

    void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance);
    void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, u32 vertexOffset, u32 firstInstance);

private:
    friend class VulkanDevice;

    VkCommandBuffer m_cmd          = VK_NULL_HANDLE;
    Handle<Pipeline> boundPipeline = {};
};

class VulkanDevice final : public Device {
public:
    VulkanDevice(Window* window);
    ~VulkanDevice();

    static VulkanDevice* impl() { return (VulkanDevice*)ptr; }

    void WaitIdle();

    CommandBuffer& GetCommandBuffer();
    CommandBuffer& GetFrameCommandBuffer();
    void FlushCommandBuffer(CommandBuffer& commandBuffer);

    bool BeginFrame();
    void EndFrame();
    u32 FrameIdx();

    Format GetSwapchainFormat();
    Extent2D GetSwapchainExtent();

    VkCommandBuffer GetCommandBufferVK();
    void FlushCommandBufferVK(VkCommandBuffer cmd);
    VkRenderingAttachmentInfo* GetSwapchainAttachmentInfo();
    
private:
    // Creates a new swapchain
    void CreateSwapchain(bool VSync);

    // Blocks until window resizing finishes. Old swapchain can safely
    // be destroyed and recreated when this function returns.
    void RecreateSwapchain();

    // Destroys the current swapchain
    void DestroySwapchain();

public:
    VkDevice                    vkDevice         = VK_NULL_HANDLE;
    VmaAllocator                vmaAllocator     = VK_NULL_HANDLE;
    VkDescriptorPool            descriptorPool   = VK_NULL_HANDLE;

    VkPhysicalDeviceFeatures	features	     = {};
    VkPhysicalDeviceProperties	properties		 = {};

private:
    // TODO: look into adding a frame allocator, allowing user
    // code to upload per-frame data. This would just be a
    // persistently mapped buffer that gets bump allocated.
    struct Frame {
        VkSemaphore      imageAvailable = VK_NULL_HANDLE;
        VkSemaphore      renderFinished = VK_NULL_HANDLE;
        VkFence          inFlight       = VK_NULL_HANDLE;

        // Command buffers are transient and one time submit.
        // commandPool is reset at the beginning of each frame.
        VkCommandPool    commandPool    = VK_NULL_HANDLE;

        // Vector of commandbuffers allocated from this frames pool.
        // Resets at the beginning of each frame along with the pool.
        std::vector<VulkanCommandBuffer> commandBuffers = {};

        // TODO: Figure out if this is even a good idea...
        //
        // Descriptor pool for temporary bindgroups. Reset at
        // the beginning of each frame. vkResetDescriptorPool.
        // 
        // Instead of double buffering bind groups in user code
        // (i.e. cascaded shadow maps), a temporary bindgroup
        // can be allocated for the frame. The returned handle
        // will only be valid during this frame.
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    };

    struct Queue {
        u32 index;
        VkQueue queue;
    };

    struct Swapchain {
        void*          window     = nullptr;
        VkExtent2D     extent     = {};
        VkFormat       format     = VK_FORMAT_UNDEFINED;
        VkSwapchainKHR swapchain  = VK_NULL_HANDLE;
        bool           VSync      = true;

        u32            imageIndex = 0;

        std::vector<VkImage>                   images          = {};
        std::vector<VkImageView>               imageViews      = {};
        std::vector<VkRenderingAttachmentInfo> attachmentInfos = {};
    } m_swapchain;

    Frame m_frames[MaxFramesInFlight] = {};
    Frame& frame() { return m_frames[m_frameIndex]; } 

    Queue m_graphics = {};
    Queue m_compute  = {};
    Queue m_transfer = {};

    VkInstance       m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface  = VK_NULL_HANDLE;
    VkPhysicalDevice m_gpu      = VK_NULL_HANDLE;

#ifdef _DEBUG
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
#endif
};