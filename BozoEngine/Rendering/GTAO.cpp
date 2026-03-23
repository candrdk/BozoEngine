#include "GTAO.h"

#include "../Core/ResourceManager.h"

GTAO::GTAO(u32 width, u32 height, Handle<Texture> depth, Handle<Texture> normal)
    : m_width{ width }, m_height{ height }, m_depth{ depth }, m_normal{ normal }
{
    ResourceManager* rm = ResourceManager::ptr;

    CreateTextures();

    // -- Bind group layouts --
    m_gtaoUBOLayout = rm->CreateBindGroupLayout({
        .debugName = "GTAO UBO layout",
        .bindings = { {.type = Binding::Type::BUFFER, .stages = ShaderStage::FRAGMENT } }
    });

    m_gtaoTextureLayout = rm->CreateBindGroupLayout({
        .debugName = "GTAO texture layout",
        .bindings = {
            {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT },
            {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT },
        }
    });

    m_blurTextureLayout = rm->CreateBindGroupLayout({
        .debugName = "GTAO blur texture layout",
        .bindings = {
            {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT },
            {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT },
        }
    });

    m_aoOutputLayout = rm->CreateBindGroupLayout({
        .debugName = "GTAO output layout",
        .bindings = { {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT } }
    });

    // -- Per-frame UBOs and bind groups --
    for (u32 i = 0; i < Device::MaxFramesInFlight; i++) {
        m_ubo[i] = rm->CreateBuffer({
            .debugName = "GTAO UBO",
            .byteSize = sizeof(UBO),
            .usage = Usage::UNIFORM_BUFFER,
            .memory = Memory::Upload
        });

        m_gtaoUBOBindings[i] = rm->CreateBindGroup({
            .debugName = "GTAO UBO bindgroup",
            .layout = m_gtaoUBOLayout,
            .buffers = { {.binding = 0, .buffer = m_ubo[i], .offset = 0, .size = sizeof(UBO) } }
        });
    }

    // -- Texture bind groups (per-frame to avoid updating in-flight descriptors) --
    CreateTextureBindings(depth, normal);

    m_aoOutputBindings = rm->CreateBindGroup({
        .debugName = "GTAO output bindgroup",
        .layout = m_aoOutputLayout,
        .textures = { {0, m_aoBlurred} }
    });

    // -- Pipelines --
    std::vector<u32> fullscreenVert = ReadShaderSpv("Shaders/deferred.vert.spv");
    std::vector<u32> gtaoFrag      = ReadShaderSpv("Shaders/gtao.frag.spv");
    std::vector<u32> blurFrag      = ReadShaderSpv("Shaders/gtao_blur.frag.spv");

    m_gtaoPipeline = rm->CreatePipeline({
        .debugName = "GTAO pipeline",
        .shaderDescs = {
            {.spirv = fullscreenVert, .stage = ShaderStage::VERTEX},
            {.spirv = gtaoFrag,       .stage = ShaderStage::FRAGMENT}
        },
        .bindgroupLayouts = { m_gtaoUBOLayout, m_gtaoTextureLayout },
        .graphicsState = {
            .colorAttachments = { Format::R8_UNORM },
            .depthStencilState = {},
            .rasterizationState = {.cullMode = CullMode::Front }
        }
    });

    m_blurPipeline = rm->CreatePipeline({
        .debugName = "GTAO blur pipeline",
        .shaderDescs = {
            {.spirv = fullscreenVert, .stage = ShaderStage::VERTEX},
            {.spirv = blurFrag,       .stage = ShaderStage::FRAGMENT}
        },
        .bindgroupLayouts = { m_gtaoUBOLayout, m_blurTextureLayout },
        .graphicsState = {
            .colorAttachments = { Format::R8_UNORM },
            .depthStencilState = {},
            .rasterizationState = {.cullMode = CullMode::Front }
        }
    });
}

GTAO::~GTAO() {
    ResourceManager* rm = ResourceManager::ptr;

    rm->DestroyPipeline(m_gtaoPipeline);
    rm->DestroyPipeline(m_blurPipeline);

    DestroyTextures();

    for (Handle<Buffer> buffer : m_ubo)
        rm->DestroyBuffer(buffer);

    rm->DestroyBindGroupLayout(m_gtaoUBOLayout);
    rm->DestroyBindGroupLayout(m_gtaoTextureLayout);
    rm->DestroyBindGroupLayout(m_blurTextureLayout);
    rm->DestroyBindGroupLayout(m_aoOutputLayout);
}

void GTAO::CreateTextures() {
    ResourceManager* rm = ResourceManager::ptr;

    m_aoRaw = rm->CreateTexture({
        .debugName = "GTAO raw",
        .width = m_width, .height = m_height,
        .format = Format::R8_UNORM,
        .usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
    });

    m_aoBlurred = rm->CreateTexture({
        .debugName = "GTAO blurred",
        .width = m_width, .height = m_height,
        .format = Format::R8_UNORM,
        .usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
    });

    // Transition to shader resource (render loop expects this initial state)
    CommandBuffer& cmd = Device::ptr->GetCommandBuffer();
    cmd.ImageBarrier(m_aoRaw,     Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(m_aoBlurred, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    Device::ptr->FlushCommandBuffer(cmd);
}

void GTAO::DestroyTextures() {
    ResourceManager* rm = ResourceManager::ptr;
    rm->DestroyTexture(m_aoRaw);
    rm->DestroyTexture(m_aoBlurred);
}

void GTAO::CreateTextureBindings(Handle<Texture> depth, Handle<Texture> normal) {
    ResourceManager* rm = ResourceManager::ptr;

    for (u32 i = 0; i < Device::MaxFramesInFlight; i++) {
        m_gtaoTextureBindings[i] = rm->CreateBindGroup({
            .debugName = "GTAO input textures",
            .layout = m_gtaoTextureLayout,
            .textures = { {0, depth}, {1, normal} }
        });

        m_blurTextureBindings[i] = rm->CreateBindGroup({
            .debugName = "GTAO blur input textures",
            .layout = m_blurTextureLayout,
            .textures = { {0, m_aoRaw}, {1, depth} }
        });
    }
}

void GTAO::Resize(u32 width, u32 height, Handle<Texture> depth, Handle<Texture> normal) {
    m_width = width;
    m_height = height;
    m_depth = depth;
    m_normal = normal;

    ResourceManager* rm = ResourceManager::ptr;

    DestroyTextures();
    CreateTextures();

    // Recreate per-frame texture bind groups with new handles
    for (u32 i = 0; i < Device::MaxFramesInFlight; i++) {
        rm->UpdateBindGroupTextures(m_gtaoTextureBindings[i], { {0, depth}, {1, normal} });
        rm->UpdateBindGroupTextures(m_blurTextureBindings[i], { {0, m_aoRaw}, {1, depth} });
    }
    rm->UpdateBindGroupTextures(m_aoOutputBindings, { {0, m_aoBlurred} });
}

void GTAO::Update(const glm::mat4& proj, const glm::mat4& invProj) {
    UBO ubo = {
        .proj      = proj,
        .invProj   = invProj,
        .params    = glm::vec4(settings.DirectionSampleCount, settings.SliceCount, settings.WorldRadius, settings.Power),
        .pixelSize = glm::vec2(1.0f / 1600, 1.0f / 900) // TODO: Actually scale w/ window size
    };

    ResourceManager::ptr->WriteBuffer(m_ubo[Device::ptr->FrameIdx()], &ubo, sizeof(ubo), 0);
}

void GTAO::Render(CommandBuffer& cmd) {
    Extent2D extent = { m_width, m_height };
    u32 frame = Device::ptr->FrameIdx();

    // -- GTAO main pass --
    cmd.ImageBarrier(m_aoRaw, Usage::SHADER_RESOURCE, Usage::RENDER_TARGET);

    cmd.SetPipeline(m_gtaoPipeline);
    cmd.SetBindGroup(m_gtaoUBOBindings[frame], 0);
    cmd.SetBindGroup(m_gtaoTextureBindings[frame], 1);

    cmd.BeginRendering(extent, { m_aoRaw });
    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // -- Blur pass --
    cmd.ImageBarrier(m_aoRaw,     Usage::RENDER_TARGET,   Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(m_aoBlurred, Usage::SHADER_RESOURCE, Usage::RENDER_TARGET);

    cmd.SetPipeline(m_blurPipeline);
    cmd.SetBindGroup(m_gtaoUBOBindings[frame], 0);
    cmd.SetBindGroup(m_blurTextureBindings[frame], 1);

    cmd.BeginRendering(extent, { m_aoBlurred });
    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    cmd.ImageBarrier(m_aoBlurred, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
}
