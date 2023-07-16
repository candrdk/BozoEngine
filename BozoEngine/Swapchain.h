#pragma once

#include "Common.h"

#include "Device.h"

struct SwapchainDesc {
	bool enableVSync = true;
	u32 preferredImageCount = 2;
	VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE;	// current structure doesn't really facilitate the use of this field
};

class Swapchain {
public:
	VkExtent2D extent = {};
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;

	std::vector<VkImage> images = {};
	std::vector<VkImageView> imageViews = {};
	std::vector<VkRenderingAttachmentInfo> attachmentInfos = {};

	void CreateSwapchain(GLFWwindow* window, const Device& device, SwapchainDesc desc);

	void DestroySwapchain(const Device& device, Swapchain& swapchain);
};
