# Bozo Engine
What kinda bozo would try to write an engine from scratch?

## What
Small `Vulkan 1.3` hobby engine/renderer written in `c++20` for Windows. The goal is to implement a very basic physically based deferred renderer along with some basic rendering techniques.

## Why
To learn vulkan, graphics programming, and the theory behind modern rendering techniques.

## Features
- Basic deferred rendering of lights. Albedo, normal, depth, metal/roughness are all written to gbuffer. Material parameters like roughness are not used yet.
- Implementations of naive parallax mapping, steep parallax mapping and parallax occlusion mapping.
- Cubemap skybox.
- Reversed depth buffering for better precision.
- Basic point lights + directional lights (no shadows yet).
- Very basic support for rendering glTF 2.0 models. Only glTF features used by the sample assets are implemented.
- Small custom [Dear ImGui](https://github.com/ocornut/imgui) vulkan backend for use with dynamic rendering.
- *Very* basic spir-v reflection of shaders for pipeline layout generation. Not used atm, and not sure if I'll keep it around, but the skeleton of an implementation is there.

## Dependencies
Bozo Engine uses the following 3rd party libraries:
- Window handeling with [glfw](https://github.com/glfw/glfw)
- Math stuff with [glm](https://github.com/g-truc/glm)
- Vulkan meta-loader: [volk](https://github.com/zeux/volk)
- Image loading with [stb](https://github.com/nothings/stb) (only using stb_image.h)
- glTF loading with [tinygltf](https://github.com/syoyo/tinygltf)
- UI with [Dear ImGui](https://github.com/ocornut/imgui)
- SPIR-V shader reflection with [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect)
