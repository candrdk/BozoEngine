#pragma once

typedef unsigned int u32;	// temporary

struct SwapchainDesc {
	bool enableVSync = true;
	u32 prefferedImageCount = 2;
	VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE;
};

struct Swapchain {
	VkExtent2D extent = {};
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;

	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
};

// temporary: this will be moved into its own Device.h/.cpp abstraction
struct Device {
	VkSurfaceKHR surface;
	VkDevice device;
	VkPhysicalDevice physicalDevice;
};

Swapchain CreateSwapchain(GLFWwindow* window, const Device& device, SwapchainDesc desc);
void DestroySwapchain(const Device& device, Swapchain& swapchain);