#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanResourceManager.h"

#include "Rendering/Camera.h"
#include "Rendering/UIOverlay.h"
#include "Rendering/GLTFModel.h"
#include "Rendering/Shadows.h"

constexpr u32 WIDTH = 1600;
constexpr u32 HEIGHT = 900;

// TODO: Stuff still missing from the old Bozo Engine:
//  - Skybox rendering.
//      - Cubemap textures
//  - Handle window resizes properly
//      - Recreate GBuffer resources on resize
//      - Update GBuffer bindgroups
//      - Fix image layout bug on resize

// NOTE: Until a proper input system has been implemented, camera is just stored as a global here.
Camera* camera;

// NOTE: These callbacks should be handled by some input system. For now we just store previous mouse positions as globals
double lastXpos = WIDTH / 2.0f;
double lastYpos = HEIGHT / 2.0f;
void FramebufferSizeCallback(GLFWwindow* window, int width, int height) { 
    Device::ptr->windowResized = true;
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        glfwSetInputMode(window, GLFW_CURSOR, action == GLFW_PRESS ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwGetCursorPos(window, &lastXpos, &lastYpos);
    }
}

void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
        return;

    double xoffset = (xpos - lastXpos);
    double yoffset = (lastYpos - ypos);

    lastXpos = xpos;
    lastYpos = ypos;

    camera->ProcessMouseMovement(xoffset, yoffset);
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
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
    alignas(16) glm::vec3 specular;
};

struct PointLight {
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 ambient;
    alignas(16) glm::vec3 diffuse;
    alignas(16) glm::vec3 specular;
};

bool bAnimateLight = false;
DirectionalLight dirLight = {
    .direction = glm::vec3(1.0f, -1.0f, -0.2f),
    .ambient   = glm::vec3(0.05f, 0.05f, 0.05f),
    .diffuse   = glm::vec3(1.0f,  0.8f,  0.7f),
    .specular  = glm::vec3(0.1f,  0.1f,  0.1f)
};
PointLight pointLightR = {
    .position = glm::vec3(0.0f,  0.25f, 0.25f),
    .ambient  = glm::vec3(0.1f,  0.1f,  0.1f),
    .diffuse  = glm::vec3(1.0f,  0.0f,  0.0f),
    .specular = glm::vec3(0.05f, 0.05f, 0.05f)
};
PointLight pointLightG = {
    .position = glm::vec3(0.0f,  0.25f, 0.25f),
    .ambient  = glm::vec3(0.1f,  0.1f,  0.1f),
    .diffuse  = glm::vec3(0.0f,  1.0f,  0.0f),
    .specular = glm::vec3(0.05f, 0.05f, 0.05f)
};
PointLight pointLightB = {
    .position = glm::vec3(0.0f,  0.25f, 0.25f),
    .ambient  = glm::vec3(0.1f,  0.1f,  0.1f),
    .diffuse  = glm::vec3(0.0f,  0.0f,  1.0f),
    .specular = glm::vec3(0.05f, 0.05f, 0.05f)
};

// Parallax settings - can be modfied in the GUI.
u32 parallaxMode = 4;
u32 parallaxSteps = 8;
float parallaxScale = 0.05f;

// NOTE: Temporary deferred ubo struct, will change once proper lights are implemented
constexpr u32 MAX_POINT_LIGHTS = 4;
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
    } settings;
} gbuffer = {};

// Initialize the GBuffer resources.
// TOOD: add support for recreating these on window resize.
void InitGBufferResources(CascadedShadowMap* shadowMap);
void DestroyGBufferResources();

// Update the GBuffer UBO. This must be called *after*  successful Device.BeginFrame() call to avoid a race condition
void UpdateGBufferUBO(CascadedShadowMap* shadowMap);

// The user-specified ImGui overlay render callback.
void ImGuiRenderCallback();

// Render models with a given shadowMap and UIOverlay.
void Render(Device* device, CascadedShadowMap* shadowMap, UIOverlay* UI, span<const GLTFModel* const> models);


int main(int argc, char* argv[]) {
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

    UIOverlay* UI = new UIOverlay(window, device, device->GetSwapchainFormat(), Format::D24_UNORM_S8_UINT, ImGuiRenderCallback);
    camera = new Camera(glm::vec3(0.0f, 1.5f, 1.0f), 1.0f, 60.0f, (float)WIDTH / HEIGHT, 0.01f, 0.0f, -30.0f);
    CascadedShadowMap* shadowMap = new CascadedShadowMap(1024, camera, { {0.0f, 3.0f}, {2.5f, 12.0f}, {11.0f, 32.0f}, {30.0f, 128.0f} });

    InitGBufferResources(shadowMap);

    // TODO: Add skybox cubemap.
    // GLTFModel* cube = new GLTFModel(device, gbuffer.materialLayout, "assets/Box.glb");

    GLTFModel* rocks = new GLTFModel(device, gbuffer.materialLayout, "assets/ParallaxTest/rocks.gltf");
    GLTFModel* sponza = new GLTFModel(device, gbuffer.materialLayout, "assets/Sponza/Sponza.gltf");

    double lastFrame = 0.0f;
    while (window->ShouldClose() == false) {
        double currentFrame = glfwGetTime();
        float deltaTime = float(currentFrame - lastFrame);
        lastFrame = currentFrame;

        camera->Update(deltaTime);
        UI->Update(deltaTime);

        // Temporary debug interface to modify parallax params.
        rocks->UpdateMaterialParallax(parallaxMode, parallaxSteps, parallaxScale);

        if (bAnimateLight) {
            float t = float(currentFrame * 0.5);
            dirLight.direction = glm::vec3(glm::cos(t), -1.0f, 0.3f * glm::sin(t));

            pointLightR.position = glm::vec3(-2.0f, glm::cos(2.0f * t) + 1.0f, 2.0f);
            pointLightG.position = glm::vec3(2.0f, 0.25f, 0.0f);
            pointLightB.position = glm::vec3(glm::cos(4.0f * t), 0.25f, -2.0f);
        }

        Render(device, shadowMap, UI, { sponza, rocks });

        glfwPollEvents();
    }

    device->WaitIdle();

    DestroyGBufferResources();

    delete sponza;
    delete rocks;
    //delete cube;

    delete shadowMap;
    delete camera;
    delete UI;

    delete ResourceManager::ptr;
    delete Device::ptr;

    return 0;
}


void Render(Device* device, CascadedShadowMap* shadowMap, UIOverlay* UI, span<const GLTFModel* const> models) {
    if (!device->BeginFrame()) {
        return;
    }

    camera->UpdateUBO();
    shadowMap->UpdateCascadeUBO(dirLight.direction);
    UpdateGBufferUBO(shadowMap);

    CommandBuffer& cmd = device->GetFrameCommandBuffer();

    shadowMap->Render(cmd, models);

    Extent2D extent = device->GetSwapchainExtent();
    cmd.SetViewport((float)extent.width, (float)extent.height);
    cmd.SetScissor({ .offset = {0, 0}, .extent = extent });

    // Offscreen GBuffer pass
    cmd.SetPipeline(gbuffer.offscreen);
    cmd.SetBindGroup(camera->GetCameraBindings(), 0);

    // Transition gbuffer resources to RT
    cmd.ImageBarrier(gbuffer.albedo, Usage::SHADER_RESOURCE, Usage::RENDER_TARGET);
    cmd.ImageBarrier(gbuffer.normal, Usage::SHADER_RESOURCE, Usage::RENDER_TARGET);
    cmd.ImageBarrier(gbuffer.metallicRoughness, Usage::SHADER_RESOURCE, Usage::RENDER_TARGET);
    cmd.ImageBarrier(gbuffer.depth, Usage::SHADER_RESOURCE, Usage::DEPTH_STENCIL);

    cmd.BeginRendering({ WIDTH, HEIGHT }, { gbuffer.albedo, gbuffer.normal, gbuffer.metallicRoughness }, gbuffer.depth);

    for (const GLTFModel* model : models)
        model->Draw(cmd);

    cmd.EndRendering();

    // Deferred pass
    cmd.SetPipeline(gbuffer.deferred);
    cmd.SetBindGroup(gbuffer.deferredBindings[device->FrameIdx()], 0);
    cmd.SetBindGroup(gbuffer.offscreenBindings[device->FrameIdx()], 1);
    cmd.SetBindGroup(shadowMap->GetShadowBindings(), 2);
    cmd.PushConstants(&gbuffer.settings, 0, sizeof(gbuffer.settings), ShaderStage::FRAGMENT);

    // Transition gbuffer resources to shader resources
    cmd.ImageBarrier(gbuffer.albedo, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.normal, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.metallicRoughness, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.depth, Usage::DEPTH_STENCIL, Usage::SHADER_RESOURCE);

    cmd.BeginRenderingSwapchain();
    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // TODO: skybox pass

    UI->Render(cmd);

    device->EndFrame();
}

void InitGBufferResources(CascadedShadowMap* shadowMap) {
    ResourceManager* rm = ResourceManager::ptr;
    Device* device = Device::ptr;

    // Create GBuffer textures
    gbuffer.albedo = rm->CreateTexture({
        .debugName = "GBuffer Albedo",
        .width = WIDTH, .height = HEIGHT,
        .format = Format::RGBA8_UNORM,
        .usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
    });

    gbuffer.normal = rm->CreateTexture({
        .debugName = "GBuffer Normal",
        .width = WIDTH, .height = HEIGHT,
        .format = Format::RGBA8_UNORM,
        .usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
    });

    gbuffer.metallicRoughness = rm->CreateTexture({
        .debugName = "GBuffer Metallic/Roughness",
        .width = WIDTH, .height = HEIGHT,
        .format = Format::RGBA8_UNORM,
        .usage = Usage::RENDER_TARGET | Usage::SHADER_RESOURCE
    });

    gbuffer.depth = rm->CreateTexture({
        .debugName = "GBuffer Depth",
        .width = WIDTH, .height = HEIGHT,
        .format = Format::D24_UNORM_S8_UINT,
        .usage = Usage::DEPTH_STENCIL | Usage::SHADER_RESOURCE
    });

    // rm will transition them to attachment layout but render loop expects them to start in shader readonly layout.
    CommandBuffer& cmd = device->GetCommandBuffer();

    cmd.ImageBarrier(gbuffer.albedo, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.normal, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.metallicRoughness, Usage::RENDER_TARGET, Usage::SHADER_RESOURCE);
    cmd.ImageBarrier(gbuffer.depth, Usage::DEPTH_STENCIL, Usage::SHADER_RESOURCE);

    device->FlushCommandBuffer(cmd);

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

    // Create Offscreen pipeline
    {
        std::vector<u32> vertShader = ReadShaderSpv("shaders/offscreen.vert.spv");
        std::vector<u32> fragShader = ReadShaderSpv("shaders/offscreen.frag.spv");

        gbuffer.offscreen = rm->CreatePipeline({
            .debugName = "Offscreen pipeline",
            .shaderDescs = {
                {.spirv = vertShader, .stage = ShaderStage::VERTEX},
                {.spirv = fragShader, .stage = ShaderStage::FRAGMENT}
            },
            .bindgroupLayouts = { gbuffer.globalsLayout, gbuffer.materialLayout },
            .graphicsState = {
                .colorAttachments = { Format::RGBA8_UNORM, Format::RGBA8_UNORM, Format::RGBA8_UNORM },
                .depthStencilState = {.depthStencilFormat = Format::D24_UNORM_S8_UINT },
                .vertexInputState = GLTFModel::Vertex::InputState
            }
        });
    }

    // Create Deferred pipeline
    {
        std::vector<u32> vertShader = ReadShaderSpv("shaders/deferred.vert.spv");
        std::vector<u32> fragShader = ReadShaderSpv("shaders/deferred.frag.spv");

        gbuffer.deferred = rm->CreatePipeline({
            .debugName = "Deferred pipeline",
            .shaderDescs = {
                {.spirv = vertShader, .stage = ShaderStage::VERTEX},
                {.spirv = fragShader, .stage = ShaderStage::FRAGMENT}
            },
            .bindgroupLayouts = { gbuffer.globalsLayout, gbuffer.materialLayout, shadowMap->GetShadowBindingsLayout() },
            .graphicsState = {
                .colorAttachments = { Format::BGRA8_SRGB },
                .depthStencilState = {.depthStencilFormat = Format::D24_UNORM_S8_UINT },
                .rasterizationState = {.cullMode = CullMode::Front }
            }
        });
    }
}

void DestroyGBufferResources() {
    ResourceManager* rm = ResourceManager::ptr;

    rm->DestroyPipeline(gbuffer.offscreen);
    rm->DestroyPipeline(gbuffer.deferred);
    for (Handle<Buffer> buffer : gbuffer.deferredUBO)
        rm->DestroyBuffer(buffer);

    rm->DestroyTexture(gbuffer.albedo);
    rm->DestroyTexture(gbuffer.normal);
    rm->DestroyTexture(gbuffer.metallicRoughness);
    rm->DestroyTexture(gbuffer.depth);

    rm->DestroyBindGroupLayout(gbuffer.materialLayout);
    rm->DestroyBindGroupLayout(gbuffer.globalsLayout);
}

void UpdateGBufferUBO(CascadedShadowMap* shadowMap) {
    DeferredUBO ubo = {
        .view = camera->view,
        .invProj = glm::inverse(camera->projection),
        .camPos = glm::vec4(camera->position, 1.0f),
        .shadowData = shadowMap->m_shadowData,
        .pointLightCount = 3,
        .dirLight = dirLight,
        .pointLights = {
            pointLightR,
            pointLightG,
            pointLightB
        }
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
    ImGui::ColorEdit3("Specular", &dirLight.specular.x, ImGuiColorEditFlags_Float);

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

        ImGui::EndTable();
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