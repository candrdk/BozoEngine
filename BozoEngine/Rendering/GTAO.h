#pragma once

#include "../Core/Graphics.h"
#include "../Core/Device.h"

#include <glm/glm.hpp>

class GTAO {
public:
    GTAO(u32 width, u32 height, Handle<Texture> depth, Handle<Texture> normal);
    ~GTAO();

    void Resize(u32 width, u32 height, Handle<Texture> depth, Handle<Texture> normal);
    void Update(const glm::mat4& proj, const glm::mat4& invProj);
    void Render(CommandBuffer& cmd);

    Handle<BindGroupLayout> GetAOBindingsLayout() { return m_aoOutputLayout; }
    Handle<BindGroup>       GetAOBindings()       { return m_aoOutputBindings; }

    Handle<Texture> GetRawTexture()     { return m_aoRaw; }
    Handle<Texture> GetBlurredTexture() { return m_aoBlurred; }

    struct Settings {
        int   DirectionSampleCount = 8;
        int   SliceCount           = 4;
        float WorldRadius          = 0.01f;
        float Power                = 2.0f;
        bool  enabled    = true;
    } settings;

private:
    struct UBO {
        alignas(16) glm::mat4 proj;
        alignas(16) glm::mat4 invProj;
        alignas(16) glm::vec4 params;
        alignas(16) glm::vec2 pixelSize;
    };

    void CreateTextures();
    void DestroyTextures();
    void CreateTextureBindings(Handle<Texture> depth, Handle<Texture> normal);

    u32 m_width, m_height;

    // Stored handles for barrier management
    Handle<Texture> m_depth;
    Handle<Texture> m_normal;

    // AO textures
    Handle<Texture> m_aoRaw;
    Handle<Texture> m_aoBlurred;

    // GTAO main pass
    Handle<Pipeline>        m_gtaoPipeline;
    Handle<BindGroupLayout> m_gtaoUBOLayout;
    Handle<BindGroupLayout> m_gtaoTextureLayout;
    Handle<BindGroup>       m_gtaoUBOBindings[Device::MaxFramesInFlight];
    Handle<BindGroup>       m_gtaoTextureBindings[Device::MaxFramesInFlight];
    Handle<Buffer>          m_ubo[Device::MaxFramesInFlight];

    // Blur pass
    Handle<Pipeline>        m_blurPipeline;
    Handle<BindGroupLayout> m_blurTextureLayout;
    Handle<BindGroup>       m_blurTextureBindings[Device::MaxFramesInFlight];

    // Output for deferred pass
    Handle<BindGroupLayout> m_aoOutputLayout;
    Handle<BindGroup>       m_aoOutputBindings;
};
