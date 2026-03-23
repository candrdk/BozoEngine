#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanResourceManager.h"

#include "Rendering/Camera.h"
#include "Rendering/UIOverlay.h"
#include "Rendering/GLTFModel.h"
#include "Rendering/Shadows.h"
#include "Rendering/GTAO.h"

// TODO: Remove this include. Only used for loading skybox textures.
#include <stb/stb_image.h>

constexpr u32 WIDTH = 1600;
constexpr u32 HEIGHT = 900;

// TODO: Stuff still missing from the old Bozo Engine:
//  - Add hot-reloading of shaders

// NOTE: Until a proper input system has been implemented, camera is just stored as a global here.
Camera* camera;
GTAO* g_gtao;

// NOTE: These callbacks should be handled by some input system. For now we just store previous mouse positions as globals
double lastXpos = WIDTH / 2.0f;
double lastYpos = HEIGHT / 2.0f;
static void FramebufferSizeCallback(GLFWwindow* window, int width, int height) { 
    Device::ptr->windowResized = true;
}

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        glfwSetInputMode(window, GLFW_CURSOR, action == GLFW_PRESS ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwGetCursorPos(window, &lastXpos, &lastYpos);
    }
}

static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
        return;

    double xoffset = (xpos - lastXpos);
    double yoffset = (lastYpos - ypos);

    lastXpos = xpos;
    lastYpos = ypos;

    camera->ProcessMouseMovement(xoffset, yoffset);
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;

    case GLFW_KEY_SPACE:
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_W:
    case GLFW_KEY_A:
    case GLFW_KEY_S:
    case GLFW_KEY_D:
        camera->ProcessKeyboard(key, action);
        break;
    }
}

// NOTE: Temporary lighting structs. We just store a few of these as globals for now.
struct DirectionalLight {
    alignas(16) glm::vec3 direction;
    alignas(16) glm::vec3 ambient;
    alignas(16) glm::vec3 diffuse;
};

struct PointLight {
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 ambient;
    alignas(16) glm::vec3 diffuse;
};

bool bAnimateLight = false;
DirectionalLight dirLight = {
    .direction = glm::vec3(1.0f, -1.0f, -0.2f),
    .ambient   = glm::vec3(0.05f, 0.05f, 0.05f),
    .diffuse   = glm::vec3(2.0f,  1.6f,  1.4f)
};

// Parallax settings - can be modfied in the GUI.
u32 parallaxMode = 4;
u32 parallaxSteps = 8;
float parallaxScale = 0.05f;

// NOTE: Temporary deferred ubo struct, will change once proper lights are implemented
constexpr u32 MAX_POINT_LIGHTS = 20;
struct DeferredUBO {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 invProj;
    alignas(16) glm::vec4 camPos;

    alignas(16) CascadedShadowMap::ShadowDataUBO shadowData;

    int pointLightCount;
    DirectionalLight dirLight;
    PointLight pointLights[MAX_POINT_LIGHTS];
};

// Struct to hold all GBuffer rendering resources.
struct GBuffer {
    Extent2D extent = {};

    Handle<Texture> albedo = {};
    Handle<Texture> normal = {};
    Handle<Texture> metallicRoughness = {};
    Handle<Texture> depth = {};

    Handle<BindGroupLayout> globalsLayout = {};
    Handle<BindGroupLayout> materialLayout = {};

    Handle<Pipeline> offscreen = {};
    Handle<Pipeline> deferred = {};

    Handle<Buffer> deferredUBO[Device::MaxFramesInFlight] = {};
    Handle<BindGroup> deferredBindings[Device::MaxFramesInFlight] = {};
    Handle<BindGroup> offscreenBindings[Device::MaxFramesInFlight] = {};

    struct {
        u32 renderMode = 0;
        u32 colorCascades = 0;
        u32 enablePCF = 1;
        u32 enableGTAO = 1;
    } settings;
} gbuffer = {};

// Create/destroy the GBuffer
void CreateGBuffer(CascadedShadowMap* shadowMap, GTAO* gtao);
void DestroyGBuffer();
static void CreateGBufferPipelines(CascadedShadowMap* shadowMap, GTAO* gtao);
static void DestroyGBufferPipelines();

// Update the GBuffer on window resize
void ResizeGBuffer();

// Update the GBuffer UBO. This must be called *after*  successful Device.BeginFrame() call to avoid a race condition
void UpdateGBufferUBO(CascadedShadowMap* shadowMap);

struct Skybox {
    Handle<Texture> texture;
    Handle<BindGroup> bindgroup;
    Handle<BindGroupLayout> layout;
    Handle<Pipeline> pipeline;

    Handle<Buffer> vertexBuffer;
    Handle<Buffer> indexBuffer;
} skybox;

void CreateSkybox();
void DestroySkybox();

// The user-specified ImGui overlay render callback.
void ImGuiRenderCallback();

// Render models with a given shadowMap, GTAO and UIOverlay.
void Render(Device* device, CascadedShadowMap* shadowMap, GTAO* gtao, UIOverlay* UI, span<const GLTFModel* const> models);


int main(int argc, char* argv[]) {
    // -- Create window and vulkan instance --
    Window* window = new Window("Bozo Engine 0.2", WIDTH, HEIGHT, {
        .FramebufferSize = FramebufferSizeCallback,
        .MouseButton = MouseButtonCallback,
        .CursorPos = CursorPosCallback,
        .Key = KeyCallback
    });

    Device::ptr = new VulkanDevice(window);
    ResourceManager::ptr = new VulkanResourceManager();

    Device* device = Device::ptr;
    ResourceManager* rm = ResourceManager::ptr;


    // -- Create UI overlay, 3D camera, shadow map, skybox and rendering resources -- 
    UIOverlay* UI = new UIOverlay(window, device, device->GetSwapchainFormat(), Format::D24_UNORM_S8_UINT, ImGuiRenderCallback);
    camera = new Camera(glm::vec3(0.0f, 1.5f, 1.0f), 1.0f, 60.0f, (float)WIDTH / HEIGHT, 0.01f, 0.0f, -30.0f);
    CascadedShadowMap* shadowMap = new CascadedShadowMap(1024 * 2, camera, { {0.0f, 3.0f}, {2.5f, 12.0f}, {11.0f, 32.0f}, {30.0f, 128.0f} });
    CreateGBuffer(shadowMap, nullptr);
    GTAO* gtao = new GTAO(gbuffer.extent.width, gbuffer.extent.height, gbuffer.depth, gbuffer.normal);
    g_gtao = gtao;
    CreateGBufferPipelines(shadowMap, gtao);
    CreateSkybox();


    // -- Load 3D models --
    GLTFModel* rocks  = new GLTFModel(device, gbuffer.materialLayout, "Assets/ParallaxTest/rocks.gltf");
    GLTFModel* sponza = new GLTFModel(device, gbuffer.materialLayout, "Assets/Sponza/Sponza.gltf");


    // -- Main loop --
    double lastFrame = 0.0f;
    while (window->ShouldClose() == false) {
        double currentFrame = glfwGetTime();
        float deltaTime = float(currentFrame - lastFrame);
        lastFrame = currentFrame;

        camera->Update(deltaTime);
        UI->Tick(deltaTime);

        // Temporary debug interface to modify parallax params.
        rocks->UpdateMaterialParallax(parallaxMode, parallaxSteps, parallaxScale);

        if (bAnimateLight) {
            float t = float(currentFrame * 0.5);
            dirLight.direction = glm::vec3(glm::cos(t), -1.0f, 0.3f * glm::sin(t));
        }

        Render(device, shadowMap, gtao, UI, { sponza, rocks });

        // If the window was resized, we also have to resize the GBuffer and update the camera aspect
        Extent2D swapchainExtent = device->GetSwapchainExtent();
        if (swapchainExtent.width != gbuffer.extent.width || swapchainExtent.height != gbuffer.extent.height) {
            ResizeGBuffer();
            gtao->Resize(swapchainExtent.width, swapchainExtent.height, gbuffer.depth, gbuffer.normal);
            camera->aspect = (float)swapchainExtent.width / swapchainExtent.height;
        }

        glfwPollEvents();
    }

    device->WaitIdle();

    DestroySkybox();
    DestroyGBuffer();

    delete sponza;
    delete rocks;

    delete gtao;
    delete shadowMap;
    delete camera;
    delete UI;

    delete ResourceManager::ptr;
    delete Device::ptr;

    return 0;
}


void Render(Device* device, CascadedShadowMap* shadowMap, GTAO* gtao, UIOverlay* UI, span<const GLTFModel* const> models) {
    if (!device->BeginFrame()) {
        return;
    }

    UI->Update();
    camera->UpdateUBO();
    shadowMap->UpdateCascadeUBO(dirLight.direction);
    UpdateGBufferUBO(shadowMap);
    gtao->Update(camera->projection, glm::inverse(camera->projection));

    CommandBuffer& cmd = device->GetFrameCommandBuffer();

    shadowMap->Render(cmd, models);

    Extent2D extent = device->GetSwapchainExtent();
    cmd.SetViewport((float)extent.width, (float)extent.height);
    cmd.SetScissor({ .offset = {0, 0}, .extent = extent });

    // -- Offscreen GBuffer pass --
    cmd.SetPipeline(gbuffer.offscreen);
    cmd.SetBindGroup(camera->GetCameraBindings(), 0);

    // Transition gbuffer resources to RT
    cmd.ImageBarrier(gbuffer.albedo, Usage::SHADER_RESOURCE, Usage::RENDER_TARGET);
    cmd.ImageBarrier(gbuffer.normal, Usage::SHADER_RESOURCE, Usage::RENDER_TARGET);
    cmd.ImageBarrier(gbuffer.metallicRoughness, Usage::SHADER_RESOURCE, Usage::RENDER_TARGET);

    cmd.BeginRendering(extent, { gbuffer.albedo, gbuffer.normal, gbuffer.metallicRoughness }, gbuffer.depth);

    for (const GLTFModel* model : models)
        model->Draw(cmd);

    cmd.EndRendering();

    // Transition depth and normal for GTAO to read
    cmd.ImageBarrier(gbuffer.normal, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.depth, Usage::DEPTH_STENCIL, Usage::SHADER_RESOURCE);

    // -- GTAO pass --
    gtao->Render(cmd);

    // Transition remaining gbuffer resources for deferred pass
    cmd.ImageBarrier(gbuffer.albedo, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.metallicRoughness, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);

    // -- Deferred pass --
    cmd.SetPipeline(gbuffer.deferred);
    cmd.SetBindGroup(gbuffer.deferredBindings[device->FrameIdx()], 0);
    cmd.SetBindGroup(gbuffer.offscreenBindings[device->FrameIdx()], 1);
    cmd.SetBindGroup(shadowMap->GetShadowBindings(), 2);
    cmd.SetBindGroup(gtao->GetAOBindings(), 3);
    cmd.PushConstants(&gbuffer.settings, 0, sizeof(gbuffer.settings), ShaderStage::FRAGMENT);

    cmd.BeginRenderingSwapchain();
    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // -- Skybox pass --
    cmd.SetPipeline(skybox.pipeline);
    cmd.SetBindGroup(camera->GetCameraBindings(), 0);
    cmd.SetBindGroup(skybox.bindgroup, 1);
    cmd.SetVertexBuffer(skybox.vertexBuffer, 0);
    cmd.SetIndexBuffer(skybox.indexBuffer, 0, IndexType::UINT32);

    // Transition depth back to attachment optimal
    cmd.ImageBarrier(gbuffer.depth, Usage::SHADER_RESOURCE, Usage::DEPTH_STENCIL);

    cmd.BeginRenderingSwapchain(gbuffer.depth);
    cmd.DrawIndexed(36, 1, 0, 0, 0);
    cmd.EndRendering();

    UI->Render(cmd);

    device->EndFrame();
}

void CreateSkybox() {
    ResourceManager* rm = ResourceManager::ptr;

    u32 vertexBufferByteSize = sizeof(glm::vec3) * 24;
    glm::vec3 vertices[24] = {
        { -0.5, -0.5,  0.5 },
        {  0.5, -0.5,  0.5 },
        { -0.5,  0.5,  0.5 },
        {  0.5,  0.5,  0.5 },
        {  0.5, -0.5,  0.5 },
        { -0.5, -0.5,  0.5 },
        {  0.5, -0.5, -0.5 },
        { -0.5, -0.5, -0.5 },
        {  0.5,  0.5,  0.5 },
        {  0.5, -0.5,  0.5 },
        {  0.5,  0.5, -0.5 },
        {  0.5, -0.5, -0.5 },
        { -0.5,  0.5,  0.5 },
        {  0.5,  0.5,  0.5 },
        { -0.5,  0.5, -0.5 },
        {  0.5,  0.5, -0.5 },
        { -0.5, -0.5,  0.5 },
        { -0.5,  0.5,  0.5 },
        { -0.5, -0.5, -0.5 },
        { -0.5,  0.5, -0.5 },
        { -0.5, -0.5, -0.5 },
        { -0.5,  0.5, -0.5 },
        {  0.5, -0.5, -0.5 },
        {  0.5,  0.5, -0.5 }
    };

    u32 indexBufferByteSize = sizeof(u32) * 36;
    u32 indices[36] = {
        0,  1,  2,  3,  2,  1,  4,  5,  6,
        7,  6,  5,  8,  9,  10, 11, 10, 9,
        12, 13, 14, 15, 14, 13, 16, 17, 18,
        19, 18, 17, 20, 21, 22, 23, 22, 21
    };

    skybox.vertexBuffer = rm->CreateBuffer({
        .debugName = "glTF vertex buffer", // TODO: better debug name
        .byteSize = vertexBufferByteSize,
        .usage = Usage::VERTEX_BUFFER | Usage::TRANSFER_DST
    });

    skybox.indexBuffer = rm->CreateBuffer({
        .debugName = "glTF index buffer", // TODO: better debug name
        .byteSize = indexBufferByteSize,
        .usage = Usage::INDEX_BUFFER | Usage::TRANSFER_DST
    });

    rm->Upload(skybox.vertexBuffer, vertices, vertexBufferByteSize);
    rm->Upload(skybox.indexBuffer, indices, indexBufferByteSize);

    const char* files[] = {
        "Assets/Skybox/right.jpg",
        "Assets/Skybox/left.jpg",
        "Assets/Skybox/top.jpg",
        "Assets/Skybox/bottom.jpg",
        "Assets/Skybox/front.jpg",
        "Assets/Skybox/back.jpg"
    };

    int width, height, channels;
    stbi_info(files[0], &width, &height, &channels);

    u32 layerStride = width * height * 4;
    std::vector<u8> texture(layerStride * 6);

    for (u32 layer = 0; layer < 6; layer++) {
        int w, h, c;
        stbi_uc* data = stbi_load(files[layer], &w, &h, &c, STBI_rgb);
        Check(w == width, "All layers must have same width");
        Check(h == height, "All layers must have same height");
        Check(c == channels, "All layers must have same number of channels");

        if (channels == 3) {
            for (int i = 0; i < width * height; i++) {
                texture[layer * layerStride + 4 * i + 0] = data[3 * i + 0];
                texture[layer * layerStride + 4 * i + 1] = data[3 * i + 1];
                texture[layer * layerStride + 4 * i + 2] = data[3 * i + 2];
                texture[layer * layerStride + 4 * i + 3] = 0xFF;
            }
        }
        else {
            memcpy(texture.data() + layer * layerStride, data, layerStride);
        }

        stbi_image_free(data);
    }

    skybox.texture = rm->CreateTexture(texture.data(), {
        .debugName = "Skybox cubemap texture",
        .type = TextureDesc::Type::TEXTURECUBE,
        .width = (u32)width, .height = (u32)height,
        .numLayers = 6,
        .format = Format::RGBA8_UNORM,
        .usage = Usage::SHADER_RESOURCE
    });

    skybox.layout = rm->CreateBindGroupLayout({
        .debugName = "Skybox bindgroup layout",
        .bindings = {{.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT }}
    });

    skybox.bindgroup = rm->CreateBindGroup({
        .debugName = "Skybox bindgroup",
        .layout = skybox.layout,
        .textures = { {0, skybox.texture } }
    });

    std::vector<u32> vertShader = ReadShaderSpv("Shaders/skybox.vert.spv");
    std::vector<u32> fragShader = ReadShaderSpv("Shaders/skybox.frag.spv");

    skybox.pipeline = rm->CreatePipeline({
        .debugName = "Skybox pipeline",
        .shaderDescs = {
            {.spirv = vertShader, .stage = ShaderStage::VERTEX },
            {.spirv = fragShader, .stage = ShaderStage::FRAGMENT}
        },
        .bindgroupLayouts = { gbuffer.globalsLayout, skybox.layout },
        .graphicsState = {
            .colorAttachments = { Device::ptr->GetSwapchainFormat() },
            .depthStencilState = {.depthStencilFormat = Format::D24_UNORM_S8_UINT },
            .rasterizationState = {.cullMode = CullMode::Front },
            .vertexInputState = {
                .vertexStride = sizeof(glm::vec3),
                .attributes = { {.offset = 0, .format = Format::RGB32_SFLOAT} }
            }
        }
    });
}

void DestroySkybox() {
    ResourceManager* rm = ResourceManager::ptr;

    rm->DestroyPipeline(skybox.pipeline);
    rm->DestroyBindGroupLayout(skybox.layout);
    rm->DestroyTexture(skybox.texture);
    rm->DestroyBuffer(skybox.vertexBuffer);
    rm->DestroyBuffer(skybox.indexBuffer);
}

static void CreateGBufferResources() {
    ResourceManager* rm = ResourceManager::ptr;
    Device* device = Device::ptr;

    // Create GBuffer textures
    gbuffer.albedo = rm->CreateTexture({
        .debugName = "GBuffer Albedo",
        .width = gbuffer.extent.width, .height = gbuffer.extent.height,
        .format = Format::RGBA8_UNORM,
        .usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
    });

    gbuffer.normal = rm->CreateTexture({
        .debugName = "GBuffer Normal",
        .width = gbuffer.extent.width, .height = gbuffer.extent.height,
        .format = Format::RGBA8_UNORM,
        .usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
    });

    gbuffer.metallicRoughness = rm->CreateTexture({
        .debugName = "GBuffer Metallic/Roughness",
        .width = gbuffer.extent.width, .height = gbuffer.extent.height,
        .format = Format::RGBA8_UNORM,
        .usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
    });

    gbuffer.depth = rm->CreateTexture({
        .debugName = "GBuffer Depth",
        .width = gbuffer.extent.width, .height = gbuffer.extent.height,
        .format = Format::D24_UNORM_S8_UINT,
        .usage = Usage::DEPTH_STENCIL | Usage::SHADER_RESOURCE
    });

    // rm will transition them to attachment layout but render loop expects them to start in shader readonly layout.
    CommandBuffer& cmd = device->GetCommandBuffer();

    cmd.ImageBarrier(gbuffer.albedo, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.normal, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.metallicRoughness, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);

    device->FlushCommandBuffer(cmd);
}

static void CreateGBufferBindings() {
    ResourceManager* rm = ResourceManager::ptr;

    // Create bindgroup layouts
    gbuffer.globalsLayout = rm->CreateBindGroupLayout({
        .debugName = "Globals bindgroup layout",
        .bindings = { {.type = Binding::Type::BUFFER, .stages = ShaderStage::VERTEX | ShaderStage::FRAGMENT } }
    });

    gbuffer.materialLayout = rm->CreateBindGroupLayout({
        .debugName = "Material bindgroup layout",
        .bindings = {
            {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT },
            {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT },
            {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT },
            {.type = Binding::Type::TEXTURE, .stages = ShaderStage::FRAGMENT },
        }
    });

    // Create GBuffer bindgroup
    for (Handle<BindGroup>& bindgroup : gbuffer.offscreenBindings) {
        bindgroup = rm->CreateBindGroup({
            .debugName = "GBuffer bindgroup",
            .layout = gbuffer.materialLayout,
            .textures = {
                { 0, gbuffer.albedo },
                { 1, gbuffer.normal },
                { 2, gbuffer.metallicRoughness },
                { 3, gbuffer.depth }
            }
        });
    }

    // Create deferred UBOs + bindgroups
    for (u32 i = 0; i < arraysize(gbuffer.deferredUBO); i++) {
        gbuffer.deferredUBO[i] = rm->CreateBuffer({
            .debugName = "Deferred UBO",
            .byteSize = sizeof(DeferredUBO),
            .usage = Usage::UNIFORM_BUFFER,
            .memory = Memory::Upload
        });

        gbuffer.deferredBindings[i] = rm->CreateBindGroup({
            .debugName = "Deferred UBO bindgroup",
            .layout = gbuffer.globalsLayout,
            .buffers = { { 0, gbuffer.deferredUBO[i], 0, sizeof(DeferredUBO) } }
        });
    }
}

static void CreateGBufferPipelines(CascadedShadowMap* shadowMap, GTAO* gtao) {
    // Create Offscreen pipeline
    std::vector<u32> offscreenVert = ReadShaderSpv("Shaders/offscreen.vert.spv");
    std::vector<u32> offscreenFrag = ReadShaderSpv("Shaders/offscreen.frag.spv");

    gbuffer.offscreen = ResourceManager::ptr->CreatePipeline({
        .debugName = "Offscreen pipeline",
        .shaderDescs = {
            {.spirv = offscreenVert, .stage = ShaderStage::VERTEX},
            {.spirv = offscreenFrag, .stage = ShaderStage::FRAGMENT}
        },
        .bindgroupLayouts = { gbuffer.globalsLayout, gbuffer.materialLayout },
        .graphicsState = {
            .colorAttachments = { Format::RGBA8_UNORM, Format::RGBA8_UNORM, Format::RGBA8_UNORM },
            .depthStencilState = {.depthStencilFormat = Format::D24_UNORM_S8_UINT },
            .vertexInputState = GLTFModel::Vertex::InputState
        }
    });

    // Create Deferred pipeline
    std::vector<u32> deferredVert = ReadShaderSpv("Shaders/deferred.vert.spv");
    std::vector<u32> deferredFrag = ReadShaderSpv("Shaders/deferred.frag.spv");

    gbuffer.deferred = ResourceManager::ptr->CreatePipeline({
        .debugName = "Deferred pipeline",
        .shaderDescs = {
            {.spirv = deferredVert, .stage = ShaderStage::VERTEX},
            {.spirv = deferredFrag, .stage = ShaderStage::FRAGMENT}
        },
        .bindgroupLayouts = { gbuffer.globalsLayout, gbuffer.materialLayout, shadowMap->GetShadowBindingsLayout(), gtao->GetAOBindingsLayout() },
        .graphicsState = {
            .colorAttachments = { Format::BGRA8_SRGB },
            .depthStencilState = {.depthStencilFormat = Format::D24_UNORM_S8_UINT },
            .rasterizationState = {.cullMode = CullMode::Front }
        }
    });
}

static void CreateGBuffer(CascadedShadowMap* shadowMap, GTAO* gtao) {
    gbuffer.extent = Device::ptr->GetSwapchainExtent();

    CreateGBufferResources();
    CreateGBufferBindings();

    // Pipelines are created separately after GTAO is initialized,
    // since the deferred pipeline needs GTAO's bind group layout.
    if (gtao)
        CreateGBufferPipelines(shadowMap, gtao);
}

static void DestroyGBufferPipelines() {
    ResourceManager::ptr->DestroyPipeline(gbuffer.offscreen);
    ResourceManager::ptr->DestroyPipeline(gbuffer.deferred);
}

static void DestroyGBufferResources() {
    ResourceManager::ptr->DestroyTexture(gbuffer.albedo);
    ResourceManager::ptr->DestroyTexture(gbuffer.normal);
    ResourceManager::ptr->DestroyTexture(gbuffer.metallicRoughness);
    ResourceManager::ptr->DestroyTexture(gbuffer.depth);
}

static void DestroyGBufferBindings() {
    for (Handle<Buffer> buffer : gbuffer.deferredUBO)
        ResourceManager::ptr->DestroyBuffer(buffer);

    ResourceManager::ptr->DestroyBindGroupLayout(gbuffer.materialLayout);
    ResourceManager::ptr->DestroyBindGroupLayout(gbuffer.globalsLayout);
}

static void DestroyGBuffer() {
    DestroyGBufferPipelines();
    DestroyGBufferResources();
    DestroyGBufferBindings();
}

void ResizeGBuffer() {
    gbuffer.extent = Device::ptr->GetSwapchainExtent();

    DestroyGBufferResources();
    CreateGBufferResources();

    // Update GBuffer bindgroup
    for (Handle<BindGroup> bindgroup : gbuffer.offscreenBindings) {
        ResourceManager::ptr->UpdateBindGroupTextures(bindgroup, {
            { 0, gbuffer.albedo },
            { 1, gbuffer.normal },
            { 2, gbuffer.metallicRoughness },
            { 3, gbuffer.depth }
        });
    }
}

void UpdateGBufferUBO(CascadedShadowMap* shadowMap) {
    DeferredUBO ubo = {
        .view = camera->view,
        .invProj = glm::inverse(camera->projection),
        .camPos = glm::vec4(camera->position, 1.0f),
        .shadowData = shadowMap->m_shadowData,
        .pointLightCount = MAX_POINT_LIGHTS,
        .dirLight = dirLight,
        .pointLights = {}
    };

    for (i32 i = 0; i < MAX_POINT_LIGHTS; i++)
        ubo.pointLights[i] = {
            .position = glm::vec3(2.5f * (i - 10), (i/10) * 4.0f + 1.0f, ((i % 3) - 1) * 2.5f),
            .ambient = glm::vec3(1.2f, 1.0f, 1.0f),
            .diffuse = glm::vec3(1.4f, 1.0f, 1.0f)
        };

    ResourceManager::ptr->WriteBuffer(gbuffer.deferredUBO[Device::ptr->FrameIdx()], &ubo, sizeof(ubo), 0);
}

void ImGuiRenderCallback() {
    ImGui::Begin("Bozo Engine", 0, 0);
    ImGui::SliderFloat3("dir", &dirLight.direction[0], -1.0f, 1.0f);

    ImGui::SeparatorText("Directional Light settings");
    ImGui::Checkbox("Animate light", &bAnimateLight);
    ImGui::ColorEdit3("Ambient", &dirLight.ambient.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit3("Diffuse", &dirLight.diffuse.x, ImGuiColorEditFlags_Float);

    if (ImGui::CollapsingHeader("Shadow settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Visualize cascades", (bool*)&gbuffer.settings.colorCascades);
        ImGui::Checkbox("Enable PCF", (bool*)&gbuffer.settings.enablePCF);
    }

    if (ImGui::CollapsingHeader("Render Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginTable("split", 2);

        ImGui::TableNextColumn(); if (ImGui::RadioButton("Deferred", gbuffer.settings.renderMode == 0)) { gbuffer.settings.renderMode = 0; }
        ImGui::TableNextColumn();
        ImGui::TableNextColumn(); if (ImGui::RadioButton("Albedo", gbuffer.settings.renderMode == 1)) { gbuffer.settings.renderMode = 1; }
        ImGui::TableNextColumn(); if (ImGui::RadioButton("Normal", gbuffer.settings.renderMode == 2)) { gbuffer.settings.renderMode = 2; }
        ImGui::TableNextColumn(); if (ImGui::RadioButton("Metallic/Roughness", gbuffer.settings.renderMode == 3)) { gbuffer.settings.renderMode = 3; }
        ImGui::TableNextColumn(); if (ImGui::RadioButton("Depth", gbuffer.settings.renderMode == 4)) { gbuffer.settings.renderMode = 4; }
        ImGui::TableNextColumn(); if (ImGui::RadioButton("AO", gbuffer.settings.renderMode == 5)) { gbuffer.settings.renderMode = 5; }

        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("GTAO Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable GTAO##gtao", (bool*)&gbuffer.settings.enableGTAO);
        ImGui::SliderInt("DirectionSampleCount##gtao", &g_gtao->settings.DirectionSampleCount, 4, 64);
        ImGui::SliderInt("SliceCount##gtao", &g_gtao->settings.SliceCount, 4, 64);
        ImGui::SliderFloat("Power##gtao", &g_gtao->settings.Power, 0.01f, 5.0f);
        ImGui::SliderFloat("WorldRadius##gtao", &g_gtao->settings.WorldRadius, 0.001f, 5.0f);
    }

    if (ImGui::CollapsingHeader("Parallax Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::RadioButton("Disable", parallaxMode == 0)) { parallaxMode = 0; }
        if (ImGui::RadioButton("Simple Parallax Mapping", parallaxMode == 1)) { parallaxMode = 1; }
        if (ImGui::RadioButton("FGED Parallax Mapping", parallaxMode == 2)) { parallaxMode = 2; }
        if (ImGui::RadioButton("Steep Parallax Mapping", parallaxMode == 3)) { parallaxMode = 3; }
        if (ImGui::RadioButton("Parallax Occlusion Mapping", parallaxMode == 4)) { parallaxMode = 4; }

        if (parallaxMode) {
            ImGui::SliderFloat("Scale", &parallaxScale, 0.001f, 0.1f);
            if (parallaxMode > 1) {
                const u32 minSteps = 8;
                const u32 maxSteps = 64;
                ImGui::SliderScalar("Steps", ImGuiDataType_U32, &parallaxSteps, &minSteps, &maxSteps);
            }
        }
    }

    ImGui::End();
}
