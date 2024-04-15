#pragma once

#include "Graphics.h"
#include "Window.h"

struct CommandBuffer {
    virtual void BeginRendering(Handle<Texture> depth, u32 layer, u32 width, u32 height) = 0;
    virtual void BeginRendering(Extent2D extent, const span<const Handle<Texture>>&& attachments = {}, Handle<Texture> depth = {}) = 0;
    virtual void BeginRenderingSwapchain() = 0;
    virtual void EndRendering() = 0;

    // Insert an image barrier. To transition all mip levels, use mipCount = -1. To transition all array layers, use layerCount = -1
    virtual void ImageBarrier(Handle<Texture> texture, Usage srcUsage, Usage dstUsage, u32 baseMip = 0, u32 mipCount = 1, u32 baseLayer = 0, u32 layerCount = 1) = 0;

    virtual void SetPipeline(Handle<Pipeline> handle) = 0;
    virtual void SetBindGroup(Handle<BindGroup> handle, u32 index, span<const u32> dynamicOffsets = {}) = 0;
    virtual void PushConstants(void* data, u32 offset, u32 size, u32 stages) = 0;

    virtual void SetVertexBuffer(Handle<Buffer> handle, u64 offset) = 0;
    virtual void SetIndexBuffer(Handle<Buffer> handle, u64 offset, IndexType type) = 0;

    virtual void SetScissor(const Rect2D& scissor) = 0;
    virtual void SetViewport(float width, float height) = 0;

    virtual void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;
    virtual void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, u32 vertexOffset, u32 firstInstance) = 0;

    u32 m_index;
};

class Device {
public:
    static Device* ptr;

    static constexpr u32 MaxFramesInFlight = 2;

    virtual ~Device() {};

    virtual void WaitIdle() = 0;
    
    virtual CommandBuffer& GetCommandBuffer() = 0;
    virtual CommandBuffer& GetFrameCommandBuffer() = 0;
    virtual void FlushCommandBuffer(CommandBuffer& commandBuffer) = 0;

    virtual bool BeginFrame() = 0;
    virtual void EndFrame()   = 0;
    virtual u32  FrameIdx()   = 0;

    virtual Format GetSwapchainFormat() = 0;
    virtual Extent2D GetSwapchainExtent() = 0;
    
    bool windowResized = false;

protected:
    u32 m_frameIndex   = 0;
    Window* m_window   = nullptr;
};