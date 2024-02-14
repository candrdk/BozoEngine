# Bozo Engine
Small `Vulkan 1.3` renderer written in c++ from scratch with only minimal dependencies.

![Screenshot of BozoEngien in its current state.](screenshot.png)

## ⚠️Big rewrite in progress⚠️
The code is currently very messy. I'm in the progress of a big rewrite on a local branch.

## Why
To learn vulkan and graphics programming. And for fun!

## Features
- Cascaded shadow mapping. Based on the implementation described in Eric Lengyels FGED2.
- Basic deferred renderer. Albedo, normal, depth, metal/roughness are written to a gbuffer but proper materials / PBR has not been implemented yet.
- Implementations of various parallax mapping techniques such as POM.
- Reversed depth buffering for better precision.
- Cubemapped skybox.
- Basic point lights + directional lights.
- Very basic support for rendering glTF 2.0 models through [tinygltf](https://github.com/syoyo/tinygltf).
- Small custom [Dear ImGui](https://github.com/ocornut/imgui) vulkan backend for use with `dynamic_rendering`.
- *Very* basic spir-v reflection of shaders for pipeline layout generation. Mostly just a rough skeleton.

## Dependencies
Bozo Engine uses the following 3rd party libraries:
- Window handeling with [glfw](https://github.com/glfw/glfw)
- Math stuff with [glm](https://github.com/g-truc/glm)
- Vulkan meta-loader: [volk](https://github.com/zeux/volk)
- Image loading with `stb_image.h` from [stb](https://github.com/nothings/stb)
- glTF model loading with [tinygltf](https://github.com/syoyo/tinygltf)
- UI with [Dear ImGui](https://github.com/ocornut/imgui)
- SPIR-V shader reflection with [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect)
