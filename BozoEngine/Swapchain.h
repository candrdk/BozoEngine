#pragma once

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

Swapchain CreateSwapchain(GLFWwindow* window, const Device& device, SwapchainDesc desc);

void DestroySwapchain(const Device& device, Swapchain& swapchain);