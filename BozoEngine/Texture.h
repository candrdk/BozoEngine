#pragma once

#include "Common.h"
#include "Device.h"

// TODO: CreateFromBuffer and LoadFromFile have *not* been tested and probably still contains a lot of errors.
//		 Loading preexisting mips has not been implemented. At the moment, we just assume the given buffer/image
//		 consists of a single level. We then generate mipmaps for it. In the future, this decision should be left to the user.

class Texture2D {
public:
	VkImage image;
	VkImageView view;
	VkImageLayout layout;
	VkDeviceMemory deviceMemory;

	u32 width, height;
	u32 mipLevels;
	VkDescriptorImageInfo descriptor;
	VkSampler sampler;

	void Destroy(const Device& device);

	void LoadFromFile(const char* path, const Device& device, VkQueue copyQueue, VkFormat format, VkImageUsageFlags usage, VkImageLayout requestedImageLayout);

	void CreateFromBuffer(void* buffer, VkDeviceSize bufferSize, const Device& device, VkQueue copyQueue, 
		u32 texWidth, u32 texHeight, VkFormat format, VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkImageLayout requestedImageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

private:
	void CreateImage(const Device& device, VkFormat format, VkImageUsageFlags usage);

	void CreateDefaultSampler(const Device& device);

	void CreateImageView(const Device& device, VkFormat format);
};