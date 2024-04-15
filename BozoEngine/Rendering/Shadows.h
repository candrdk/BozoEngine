#pragma once

#include "../Core/Graphics.h"
#include "../Core/Device.h"

#include "Camera.h"
#include "GLTFModel.h"

#include <glm/glm.hpp>

class CascadedShadowMap {
public:
    static constexpr u32 MaxCascades = 4;
    
    CascadedShadowMap(u32 resolution, Camera* camera, span<const glm::vec2> distances);
    ~CascadedShadowMap();

    void UpdateCascadeUBO(glm::vec3 lightDir);
    void Render(CommandBuffer& cmd, span<const GLTFModel* const> models);

    Handle<BindGroupLayout> GetShadowBindingsLayout() { return m_shadowBindingsLayout; }
    Handle<BindGroup>       GetShadowBindings()       { return m_shadowBindings; }

    struct ShadowDataUBO {
        alignas(16) glm::vec4 a;
        alignas(16) glm::vec4 b;
        alignas(16) glm::mat4 shadowMat;
        alignas(16) glm::vec4 cascadeScales[MaxCascades - 1];
        alignas(16) glm::vec4 cascadeOffsets[MaxCascades - 1];
        alignas(16) glm::vec4 shadowOffsets[2];
    } m_shadowData;

private:
    void InitCascades(span<const glm::vec2> distances);

    struct Cascade {
        glm::vec3 v0, v1, v2, v3;   // near plane of the cascade portion of the camera frustum
        glm::vec3 v4, v5, v6, v7;   // far  plane of the cascade portion of the camera frustum

        float d;                    // diameter of the cascade portion of the camera frustum

        float xmin, ymin, zmin;     // light-space bounding box of the cascade frustum
        float xmax, ymax, zmax;

        glm::mat4 world_to_cascade; // world space to cascade view space
        glm::mat4 cascade_to_proj;  // cascade view space to projection
        glm::mat4 world_to_proj;    // world space to cascade projection
    };

    Cascade m_cascades[MaxCascades];

    Handle<Buffer> m_cascadeUBO[Device::MaxFramesInFlight];
    Handle<BindGroupLayout> m_cascadeBindingsLayout;
    Handle<BindGroup> m_cascadeBindings[Device::MaxFramesInFlight];

    Handle<BindGroupLayout> m_shadowBindingsLayout;
    Handle<BindGroup> m_shadowBindings;

    Handle<Texture>  m_shadowMap;
    Handle<Pipeline> m_pipeline;

    u32 m_resolution;
    Camera* m_camera;
};