#pragma once

#include "Graphics.h"

// TODO: Add alloc/dealloc and init/deinit functions allowing usercode
// to manually allocate/deallocate and initialize/deinitialize resources 
// See: https://github.com/floooh/sokol/blob/master/sokol_gfx.h#L1128

// TODO: Consider adding a DestroyBindGroup method? Not strictly necessary,
//       but would prob be a good idea for interface symmetry. Alternatively,
//       rename CreateBindGroup to avoid confusion.

class ResourceManager {
public:
    static ResourceManager* ptr;

    virtual ~ResourceManager() {};

    virtual Handle<Buffer>          CreateBuffer(const BufferDesc&& desc)                   = 0;
    virtual Handle<Texture>         CreateTexture(const TextureDesc&& desc)                 = 0;
    virtual Handle<BindGroup>       CreateBindGroup(const BindGroupDesc&& desc)             = 0;
    virtual Handle<BindGroupLayout> CreateBindGroupLayout(const BindGroupLayoutDesc&& desc) = 0;
    virtual Handle<Pipeline>        CreatePipeline(const PipelineDesc&& desc)               = 0;

    // Create texture with initial data
    virtual Handle<Texture> CreateTexture(const void* data, const TextureDesc&& desc)       = 0;
    virtual void GenerateMipmaps(Handle<Texture> handle)                                    = 0;
    
    virtual void UpdateBindGroupTextures(Handle<BindGroup> bindgroup, span<const TextureBinding> textures) = 0;
    virtual void UpdateBindGroupBuffers(Handle<BindGroup> bindgroup, span<const BufferBinding> buffers)    = 0;
    
    virtual void DestroyBuffer(Handle<Buffer> handle)                   = 0;
    virtual void DestroyTexture(Handle<Texture> handle)                 = 0;
    virtual void DestroyBindGroupLayout(Handle<BindGroupLayout> handle) = 0;
    virtual void DestroyPipeline(Handle<Pipeline> handle)               = 0;

    // Write `size` bytes from `data` to a mapped buffer.
    virtual bool WriteBuffer(Handle<Buffer> handle, const void* data, u32 size, u32 offset = 0) = 0;

    // Upload data to device-local buffer/texture
    virtual bool Upload(Handle<Buffer> handle, const void* data, u32 size)                   = 0;
    virtual bool Upload(Handle<Texture> handle, const void* data, const TextureRange& range) = 0;

    virtual bool IsMapped(Handle<Buffer> handle)    = 0;
    virtual u8* GetMapped(Handle<Buffer> handle)    = 0;

    virtual bool MapBuffer(Handle<Buffer>   handle) = 0;
    virtual void UnmapBuffer(Handle<Buffer> handle) = 0;
};