# Bozo Engine
What kinda bozo would try to write an engine from scratch???

<img src="https://cdn.discordapp.com/attachments/707920399752626247/1123779412769386619/vulkan_unaware.png" alt="Unaware 1st year computer science students attempts to write a vulkan renderer." width="400">

## What
Small `Vulkan 1.3` engine/renderer written in `c++20`. Exclusively targeting Windows. The end goal is to implement a very basic deferred renderer. Error handling currently consists of violently asserting and shutting down the program whenever BozoEngine gets slightly uncomfortable (there is no error handling).

## Why
The goal is just to learn vulkan and simple rendering techniques.

## Features
- Implementations of naive parallax mapping, steep parallax mapping and parallax occlusion mapping.
- Basic deferred rendering of lights. Albedo, normal, depth, metal/roughness are all written to gbuffer. Material parameters like roughness are not used yet.
- Reversed depth buffering for better precision.
- Very basic support for rendering glTF 2.0 models. Only glTF features used by the sample assets are implemented.
- Small custom [Dear ImGui](https://github.com/ocornut/imgui) vulkan backend for use with dynamic rendering.
- *Very* basic spir-v reflection of shaders for pipeline layout generation. Not used atm, and not sure if I'll keep it around, but the skeleton an implementation is there.

## Dependencies
Bozo Engine uses the following 3rd party libraries:
- Window handeling with [glfw](https://github.com/glfw/glfw)
- Math stuff with [glm](https://github.com/g-truc/glm)
- Vulkan meta-loader: [volk](https://github.com/zeux/volk)
- Image loading with [stb](https://github.com/nothings/stb) (only using stb_image.h)
- glTF loading with [tinygltf](https://github.com/syoyo/tinygltf)
- UI with [Dear ImGui](https://github.com/ocornut/imgui)
- SPIR-V shader reflection with [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect)
