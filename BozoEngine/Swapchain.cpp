#include "Common.h"

#include "Logging.h"
#include "Device.h"
#include "Swapchain.h"

static VkSurfaceFormatKHR ChooseSwapchainSurfaceFormat(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice) {
	u32 surfaceFormatCount = 0;
	VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr), "Failed to get surface format count");
	Check(surfaceFormatCount > 0, "Could not find any supported surface formats");

	std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
	VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data()), "Failed to get surface formats");

	for (u32 i = 0; i < surfaceFormatCount; i++) {
		if (surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return surfaceFormats[i];
		}
	}

	return surfaceFormats[0];
}

static VkPresentModeKHR ChooseSwapchainPresentMode(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, bool enableVSync) {
	if (enableVSync) {
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	u32 presentModeCount = 0;
	VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr), "Failed to get surface present mode count");

	std::vector<VkPresentModeKHR> availablePresentModes(presentModeCount);
	VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, availablePresentModes.data()), "Failed to get surface present modes");

	for (u32 i = 0; i < presentModeCount; i++) {
		if (availablePresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
			return availablePresentModes[i];
		}
	}

	Check(false, "Surface does not support immediate present mode");
}

static VkExtent2D ChooseSwapchainExtent(GLFWwindow* window, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice) {
	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	VkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities), "Failed to get surface capabilities");

	if (surfaceCapabilities.currentExtent.width != ~UINT32_MAX) {
		return surfaceCapabilities.currentExtent;
	}

	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);

	VkExtent2D extent = {
		.width = glm::clamp((u32)width,  surfaceCapabilities.minImageExtent.width,  surfaceCapabilities.maxImageExtent.width),
		.height = glm::clamp((u32)height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height),
	};

	return extent;
}

static VkSwapchainKHR CreateSwapchain(Device device, u32 prefferedImageCount, VkSurfaceFormatKHR surfaceFormat, VkPresentModeKHR presentMode, VkExtent2D extent, VkSwapchainKHR oldSwapchain) {
	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	VkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physicalDevice, device.surface, &surfaceCapabilities), "Failed to get surface capabilities");

	u32 minImageCount = glm::clamp(prefferedImageCount, surfaceCapabilities.minImageCount, surfaceCapabilities.maxImageCount);

	VkSwapchainCreateInfoKHR createInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = device.surface,
		.minImageCount = minImageCount,
		.imageFormat = surfaceFormat.format,
		.imageColorSpace = surfaceFormat.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = surfaceCapabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = presentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = oldSwapchain
	};

	VkSwapchainKHR swapchain;
	VkCheck(vkCreateSwapchainKHR(device.device, &createInfo, nullptr, &swapchain), "Failed to create swapchain");

	return swapchain;
}

Swapchain CreateSwapchain(GLFWwindow* window, const Device& device, SwapchainDesc desc) {
	VkSurfaceFormatKHR surfaceFormat = ChooseSwapchainSurfaceFormat(device.surface, device.physicalDevice);
	VkPresentModeKHR presentMode = ChooseSwapchainPresentMode(device.surface, device.physicalDevice, desc.enableVSync);
	VkExtent2D extent = ChooseSwapchainExtent(window, device.surface, device.physicalDevice);

	Swapchain swapchain = {
		.extent = extent,
		.format = surfaceFormat.format,
		.swapchain = CreateSwapchain(device, desc.prefferedImageCount, surfaceFormat, presentMode, extent, desc.oldSwapchain)
	};

	// TODO: further abstract image/imageview stuff so we dont have to do all this here

	// We only specified the minimum number of images we want, so we have to check how many were actually created
	u32 imageCount = 0;
	vkGetSwapchainImagesKHR(device.device, swapchain.swapchain, &imageCount, nullptr);
	swapchain.images.resize(imageCount);
	vkGetSwapchainImagesKHR(device.device, swapchain.swapchain, &imageCount, swapchain.images.data());

	swapchain.imageViews.resize(imageCount);
	for (u32 i = 0; i < imageCount; i++) {
		VkImageViewCreateInfo viewInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain.images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = swapchain.format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		VkCheck(vkCreateImageView(device.device, &viewInfo, nullptr, &swapchain.imageViews[i]), "Failed to create image view");
	}

	return swapchain;
}

void DestroySwapchain(const Device& device, Swapchain& swapchain) {
	for (auto imageView : swapchain.imageViews) {
		vkDestroyImageView(device.device, imageView, nullptr);
	}

	vkDestroySwapchainKHR(device.device, swapchain.swapchain, nullptr);

	swapchain = {};
}