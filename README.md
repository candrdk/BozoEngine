# Bozo Engine
What kinda bozo would try to write an engine from scratch???

![Unaware 1st year computer science students attempts to write a vulkan renderer.](https://cdn.discordapp.com/attachments/707920399752626247/1123779412769386619/vulkan_unaware.png)

## What
Small `Vulkan 1.3` engine. Project is written in `c++20` and is exclusively targeting Windows. Error handling currently consists of violently asserting and shutting down the program whenever BozoEngine gets slightly uncomfortable, so please be nice!

The goal is just to learn vulkan implement a very basic deffered renderer along the way.

## Features
- Custom [Dear ImGui](https://github.com/ocornut/imgui) vulkan backend for use with dynamic rendering.
- Reversed depth buffering for better precision.
- Yeah thats kinda it... Don't judge mmkay.

## Dependencies
Bozo Engine uses the following 3rd party libraries:
- Window handeling with [glfw](https://github.com/glfw/glfw)
- Math stuff with [glm](https://github.com/g-truc/glm)
- Vulkan meta-loader: [volk](https://github.com/zeux/volk)
- Image loading with [stb](https://github.com/nothings/stb) (only using stb_image.h)
- Mesh loading with [fast_obj](https://github.com/thisistherk/fast_obj)
- UI with [Dear ImGui](https://github.com/ocornut/imgui)
