cmake_minimum_required(VERSION 3.20)

# Require VULKAN_SDK environment variable
if(NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "VULKAN_SDK environment variable is not set")
endif()

add_executable(BozoEngine
    main.cpp

    # Engine source
    Rendering/Camera.cpp
    Rendering/GLTFModel.cpp
    Rendering/Shadows.cpp
    Rendering/UIOverlay.cpp
    Vulkan/VulkanDevice.cpp
    Vulkan/VulkanResourceManager.cpp

    # GLFW
    ../Extern/glfw/src/context.c
    ../Extern/glfw/src/egl_context.c
    ../Extern/glfw/src/init.c
    ../Extern/glfw/src/input.c
    ../Extern/glfw/src/monitor.c
    ../Extern/glfw/src/null_init.c
    ../Extern/glfw/src/null_joystick.c
    ../Extern/glfw/src/null_monitor.c
    ../Extern/glfw/src/null_window.c
    ../Extern/glfw/src/osmesa_context.c
    ../Extern/glfw/src/platform.c
    ../Extern/glfw/src/vulkan.c
    ../Extern/glfw/src/wgl_context.c
    ../Extern/glfw/src/win32_init.c
    ../Extern/glfw/src/win32_joystick.c
    ../Extern/glfw/src/win32_module.c
    ../Extern/glfw/src/win32_monitor.c
    ../Extern/glfw/src/win32_thread.c
    ../Extern/glfw/src/win32_time.c
    ../Extern/glfw/src/win32_window.c
    ../Extern/glfw/src/window.c

    # Dear ImGui
    ../Extern/imgui/imgui.cpp
    ../Extern/imgui/imgui_demo.cpp
    ../Extern/imgui/imgui_draw.cpp
    ../Extern/imgui/imgui_tables.cpp
    ../Extern/imgui/imgui_widgets.cpp
    ../Extern/imgui/backends/imgui_impl_glfw.cpp

    # SPIRV-Cross
    ../Extern/SPIRV-Cross/spirv_cfg.cpp
    ../Extern/SPIRV-Cross/spirv_cross.cpp
    ../Extern/SPIRV-Cross/spirv_cross_parsed_ir.cpp
    ../Extern/SPIRV-Cross/spirv_parser.cpp

    # volk
    ../Extern/volk/volk.c

    # GLM natvis
    ../Extern/glm/util/glm.natvis
)

target_compile_features(BozoEngine PRIVATE cxx_std_20)

target_include_directories(BozoEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $ENV{VULKAN_SDK}/include
    ../Extern/imgui
    ../Extern
    ../Extern/glm
    ../Extern/glfw/include
    ../Extern/volk
)

target_compile_definitions(BozoEngine PRIVATE
    _CONSOLE
    _GLFW_WIN32
    _CRT_SECURE_NO_WARNINGS
    VK_USE_PLATFORM_WIN32_KHR
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# Shader compilation
set(GLSLANG "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set(SHADERS
    deferred.vert.glsl
    deferred.frag.glsl
    offscreen.vert.glsl
    offscreen.frag.glsl
    shadowMap.vert.glsl
    skybox.vert.glsl
    skybox.frag.glsl
    uioverlay.vert.glsl
    uioverlay.frag.glsl
)

set(SPV_OUTPUTS)
foreach(SHADER ${SHADERS})
    string(REPLACE ".glsl" ".spv" SPV_NAME "${SHADER}")
    set(SPV_OUT "${SHADER_OUT_DIR}/${SPV_NAME}")
    add_custom_command(
        OUTPUT "${SPV_OUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
        COMMAND "${GLSLANG}" "${SHADER_SRC_DIR}/${SHADER}" -gVS -V -o "${SPV_OUT}"
        DEPENDS "${SHADER_SRC_DIR}/${SHADER}"
    )
    list(APPEND SPV_OUTPUTS "${SPV_OUT}")
endforeach()

add_custom_target(Shaders DEPENDS ${SPV_OUTPUTS})
add_dependencies(BozoEngine Shaders)

# Create a junction from the build directory to the source assets directory
file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/Assets" ASSETS_JUNCTION)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Assets" ASSETS_SOURCE)
add_custom_command(TARGET BozoEngine POST_BUILD
    COMMAND cmd /c "if not exist \"${ASSETS_JUNCTION}\" mklink /J \"${ASSETS_JUNCTION}\" \"${ASSETS_SOURCE}\""
    COMMENT "Linking assets directory"
)

# MSVC-specific settings
if(MSVC)
    target_compile_options(BozoEngine PRIVATE
        /W3
        /sdl
        /permissive-
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Oi>
    )
    target_link_options(BozoEngine PRIVATE
        /SUBSYSTEM:CONSOLE
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/OPT:REF>
    )
    set_target_properties(BozoEngine PROPERTIES
        VS_GLOBAL_CharacterSet "Unicode"
    )
endif()
