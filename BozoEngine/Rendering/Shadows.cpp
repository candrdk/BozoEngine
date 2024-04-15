#include "Shadows.h"

#include "../Core/ResourceManager.h"

CascadedShadowMap::CascadedShadowMap(u32 resolution, Camera* camera, span<const glm::vec2> distances)
    : m_resolution{ resolution }, m_camera{ camera }
{
    InitCascades(distances);

    ResourceManager* rm = ResourceManager::ptr;

    m_shadowMap = rm->CreateTexture({
        .debugName = "Cascaded shadow map",
        .type      = TextureDesc::Type::TEXTURE2DARRAY,
        .width     = m_resolution, .height = m_resolution,
        .numLayers = MaxCascades,
        .format    = Format::D32_SFLOAT,
        .usage     = Usage::DEPTH_STENCIL | Usage::SHADER_RESOURCE,
        .sampler   = { true, CompareOp::Greater }
    });

    // shadowmap will be created in depth_stencil layout, but our render loop expects it to begin in shader_resource layout.
    CommandBuffer& cmd = Device::ptr->GetCommandBuffer();
    cmd.ImageBarrier(m_shadowMap, Usage::DEPTH_STENCIL, Usage::SHADER_RESOURCE, 0, 1, 0, -1);
    Device::ptr->FlushCommandBuffer(cmd);

    m_shadowBindingsLayout = rm->CreateBindGroupLayout({
        .debugName = "Shadow bindgroup layout",
        .bindings  = { {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT } }
    });

    m_shadowBindings = rm->CreateBindGroup({
        .debugName = "Shadowmap bindgroup",
        .layout    = m_shadowBindingsLayout,
        .textures  = { {0, m_shadowMap } }
    });

    m_cascadeBindingsLayout = rm->CreateBindGroupLayout({
        .debugName = "Cascade bindgroup layout",
        .bindings  = { {.type = Binding::Type::DYNAMIC } }
    });

    for (u32 i = 0; i < Device::MaxFramesInFlight; i++) {
        m_cascadeUBO[i] = rm->CreateBuffer({
            .debugName = "CSM cascade viewProj matrix",
            .byteSize  = glm::max(sizeof(glm::mat4), 256ull) * MaxCascades,
            .usage     = Usage::UNIFORM_BUFFER,
            .memory    = Memory::Upload
        });

        m_cascadeBindings[i] = rm->CreateBindGroup({
            .debugName = "Cascade bindgroup",
            .layout    = m_cascadeBindingsLayout,
            .buffers   = { {.binding = 0, .buffer = m_cascadeUBO[i], .size = sizeof(glm::mat4) } }
        });
    }

    std::vector<u32> vertShader = ReadShaderSpv("shaders/shadowMap.vert.spv");

    m_pipeline = rm->CreatePipeline({
        .debugName = "Cascaded shadow map render pipeline",
        .shaderDescs = { {.spirv = vertShader, .stage = ShaderStage::VERTEX } },
        .bindgroupLayouts = { m_cascadeBindingsLayout },
        .graphicsState = {
            .depthStencilState = {.depthStencilFormat = Format::D32_SFLOAT },
            .rasterizationState = {
                .depthClampEnable = true,
                .depthBiasEnable = true,
                .depthBiasConstantFactor = -2.0f,   // TODO: play around with these values
                .depthBiasClamp = -1.0f / 128.0f,
                .depthBiasSlopeFactor = -3.0f,

                .cullMode = CullMode::Back
            },
            .vertexInputState = {
                .vertexStride = sizeof(GLTFModel::Vertex),
                .attributes = { {.format = Format::RGB32_SFLOAT } }
            }
        }
    });
}

CascadedShadowMap::~CascadedShadowMap() {
    ResourceManager* rm = ResourceManager::ptr;

    rm->DestroyPipeline(m_pipeline);

    for (Handle<Buffer> buffer : m_cascadeUBO)
        rm->DestroyBuffer(buffer);

    rm->DestroyBindGroupLayout(m_cascadeBindingsLayout);
    rm->DestroyBindGroupLayout(m_shadowBindingsLayout);
    rm->DestroyTexture(m_shadowMap);
}

void CascadedShadowMap::UpdateCascadeUBO(glm::vec3 lightDir) {
    // Calculate light matrix from light direction. (This breaks when x,z are zero)
    const glm::vec3 z = -glm::normalize(lightDir);
    const glm::vec3 x = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), z));
    const glm::vec3 y = glm::cross(x, z);
    const glm::mat4 light = glm::mat3(x, y, z);

    // Camera space to light space matrix
    const glm::mat4 L = glm::inverse(light) * glm::inverse(m_camera->view);

    for (int k = 0; k < MaxCascades; k++) {
        // Transform cascade frustum points from camera view space to light space
        const glm::vec4 Lv[8] = {
            L * glm::vec4(m_cascades[k].v0, 1.0f),
            L * glm::vec4(m_cascades[k].v1, 1.0f),
            L * glm::vec4(m_cascades[k].v2, 1.0f),
            L * glm::vec4(m_cascades[k].v3, 1.0f),

            L * glm::vec4(m_cascades[k].v4, 1.0f),
            L * glm::vec4(m_cascades[k].v5, 1.0f),
            L * glm::vec4(m_cascades[k].v6, 1.0f),
            L * glm::vec4(m_cascades[k].v7, 1.0f)
        };

        // Find the light-space bounding box of cascade frustum
        m_cascades[k].xmin = Lv[0].x;
        m_cascades[k].ymin = Lv[0].y;
        m_cascades[k].zmin = Lv[0].z;
        m_cascades[k].xmax = Lv[0].x;
        m_cascades[k].ymax = Lv[0].y;
        m_cascades[k].zmax = Lv[0].z;
        for (const glm::vec4 Lvi : Lv) {
            m_cascades[k].xmin = glm::min(m_cascades[k].xmin, Lvi.x);
            m_cascades[k].ymin = glm::min(m_cascades[k].ymin, Lvi.y);
            m_cascades[k].zmin = glm::min(m_cascades[k].zmin, Lvi.z);

            m_cascades[k].xmax = glm::max(m_cascades[k].xmax, Lvi.x);
            m_cascades[k].ymax = glm::max(m_cascades[k].ymax, Lvi.y);
            m_cascades[k].zmax = glm::max(m_cascades[k].zmax, Lvi.z);
        }

        // Calculate the physical size of shadow map texels
        const float T = m_cascades[k].d / m_resolution;

        // Shadow edges are stable if the viewport coordinates of each vertex belonging to an object rendered into the shadow map
        // have *constant fractional parts*. (Triangle are rasterized identically if moved by an integral number of texels).
        // Because the distance between adjacent texels in viewport space corresponds to the physical distance T, changing the
        // camera's x/y position by a multiple of T preserves the fractional positions of the vertices. To achieve shadow stability,
        // we thus require that the light-space x and y coordinates of the camera position always are integral multiples of T.
        //	
        //	Note: For this calculation to be completely effective, T must be exactly representable as a floating point number.
        //        We thus have to make sure that n is always a power of 2. This is also why take the ceiling of d.

        // Calculate light-space coordinates of the camera position we will be rendering the shadow map cascade from.
        const glm::vec3 s = glm::vec3(glm::floor((m_cascades[k].xmax + m_cascades[k].xmin) / (T * 2.0f)) * T,
                                      glm::floor((m_cascades[k].ymax + m_cascades[k].ymin) / (T * 2.0f)) * T,
                                      m_cascades[k].zmin);

        // The cascade camera space to world space matrix can be found by:
        // M    = [ light[0] | light[1] | light[2] | light * s ]
        // As we only need the inverse (world to cascade camera space), we instead calculate:
        // M^-1 = [ light^T[0] | light^T[1] | light^T[2] | -s ]
        // This assumes the upper 3x3 matrix of light is orthogonal and that the translation component is zero.
        const glm::mat4 lightT = glm::transpose(light);
        m_cascades[k].world_to_cascade = glm::mat4(lightT[0], lightT[1], lightT[2], glm::vec4(-s, 1.0f));

        // Calculate the cascade projection matrix
        const float d  = m_cascades[k].d;
        const float zd = m_cascades[k].zmax - m_cascades[k].zmin;
        m_cascades[k].cascade_to_proj = glm::mat4(
            2.0f / d, 0.0f,     0.0f,      0.0f,
            0.0f,     2.0f / d, 0.0f,      0.0f,
            0.0f,     0.0f,     1.0f / zd, 0.0f,
            0.0f,     0.0f,     0.0f,      1.0f);

        // Calculate the view projection matrix of the cascade: P_cascade * (M_cascade)^-1 
        // Full MVP matrix is then cascade.viewProj * M_object
        m_cascades[k].world_to_proj = m_cascades[k].cascade_to_proj * m_cascades[k].world_to_cascade;

        // Update cascade ubo
        // TODO: 256 is the max possible minUniformBufferOffsetAlignment. Use device.properties.minUniformBufferOffsetAlignment instead.
        ResourceManager::ptr->WriteBuffer(m_cascadeUBO[Device::ptr->FrameIdx()], &m_cascades[k].world_to_proj, sizeof(glm::mat4), k * 256);
    }

    // Calculate world to shadow map texture coordinates texture.
    const float d0  = m_cascades[0].d;
    const float zd0 = m_cascades[0].zmax - m_cascades[0].zmin;
    const glm::mat4 shadowProj = glm::mat4(
        1.0f / d0, 0.0f,      0.0f,       0.0f,
        0.0f,      1.0f / d0, 0.0f,       0.0f,
        0.0f,      0.0f,      1.0f / zd0, 0.0f,
        0.5f,      0.5f,      0.0f,       1.0f
    );

    m_shadowData.shadowMat = shadowProj * m_cascades[0].world_to_cascade;

    // We only calculate the shadow matrix for cascade 0.
    // To convert the texture coordinates between cascades, we just use some scales and offsets.
    for (int k = 1; k < MaxCascades; k++) {
        const float dk  = m_cascades[k].d;
        const float zdk = m_cascades[k].zmax - m_cascades[k].zmin;

        const glm::vec3 s0 = -m_cascades[0].world_to_cascade[3];
        const glm::vec3 sk = -m_cascades[k].world_to_cascade[3];

        m_shadowData.cascadeScales[k - 1] = glm::vec4(d0 / dk, d0 / dk, zd0 / zdk, 0.0f);

        m_shadowData.cascadeOffsets[k - 1].x = (s0.x - sk.x) / dk - d0 / (2.0f * dk) + 0.5f;
        m_shadowData.cascadeOffsets[k - 1].y = (s0.y - sk.y) / dk - d0 / (2.0f * dk) + 0.5f;
        m_shadowData.cascadeOffsets[k - 1].z = (s0.z - sk.z) / zdk;
    }
}

void CascadedShadowMap::Render(CommandBuffer& cmd, span<const GLTFModel* const> models) {
    cmd.ImageBarrier(m_shadowMap, Usage::SHADER_RESOURCE, Usage::DEPTH_STENCIL, 0, 1, 0, -1);

    cmd.SetViewport((float)m_resolution, (float)m_resolution);
    cmd.SetScissor({ .offset = {0, 0}, .extent = {m_resolution, m_resolution} });

    cmd.SetPipeline(m_pipeline);

    for (u32 cascade = 0; cascade < MaxCascades; cascade++) {
        u32 dynamicOffset = cascade * 256; // TODO: again, this should be taken from device
        cmd.SetBindGroup(m_cascadeBindings[Device::ptr->FrameIdx()], 0, {dynamicOffset});

        cmd.BeginRendering(m_shadowMap, cascade, m_resolution, m_resolution);

        for (const GLTFModel* model : models)
            model->Draw(cmd, true);

        cmd.EndRendering();
    }

    cmd.ImageBarrier(m_shadowMap, Usage::DEPTH_STENCIL, Usage::SHADER_RESOURCE, 0, 1, 0, -1);
}

void CascadedShadowMap::InitCascades(span<const glm::vec2> distances) {
    Check(distances.size() == MaxCascades, "All %u cascade distances must be specified", MaxCascades);

    // Offsets for shadow samples.
    float d = 3.0f / (16.0f * m_resolution);
    m_shadowData.shadowOffsets[0] = glm::vec4(glm::vec2(-d, -3 * d), glm::vec2(3 * d, -d));
    m_shadowData.shadowOffsets[1] = glm::vec4(glm::vec2(d, 3 * d), glm::vec2(-3 * d, d));

    const float s = m_camera->aspect;
    const float g = 1.0f / glm::tan(glm::radians(m_camera->fov) * 0.5f);

    for (int k = 0; k < MaxCascades; k++) {
        const float a = distances[k].x;
        const float b = distances[k].y;

        // Save cascade distances; they are used in the shader for cascade transitions.
        m_shadowData.a[k] = a;
        m_shadowData.b[k] = b;

        // Calculate the eight view-space frustum coordinates of the cascade
        m_cascades[k] = {
            .v0 = glm::vec3(a * s / g, -a / g, a),
            .v1 = glm::vec3(a * s / g,  a / g, a),
            .v2 = glm::vec3(-a * s / g,  a / g, a),
            .v3 = glm::vec3(-a * s / g, -a / g, a),

            .v4 = glm::vec3(b * s / g, -b / g, b),
            .v5 = glm::vec3(b * s / g,  b / g, b),
            .v6 = glm::vec3(-b * s / g,  b / g, b),
            .v7 = glm::vec3(-b * s / g, -b / g, b),
        };

        // Calculate shadow map size (diameter of the cascade)
        m_cascades[k].d = glm::ceil(glm::max(
            glm::length(m_cascades[k].v0 - m_cascades[k].v6),
            glm::length(m_cascades[k].v4 - m_cascades[k].v6)));
    }
}