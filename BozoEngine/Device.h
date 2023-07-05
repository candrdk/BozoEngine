#pragma once

struct Queue {
	VkQueue queue = VK_NULL_HANDLE;
	u32 index = UINT32_MAX;
};

struct Device {
	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	Queue graphicsQueue;
	VkCommandPool commandPool = VK_NULL_HANDLE;
};

Device CreateDevice(GLFWwindow* window);

void DestroyDevice(Device& device);