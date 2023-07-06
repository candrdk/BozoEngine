#pragma once

#include "Common.h"

#include "Logging.h"
#include "Device.h"

class Texture {
public:
	VkImage image;
	VkImageView view;
	VkImageLayout layout;
	VkDeviceMemory deviceMemory;

	u32 width, height;
	u32 mipLevels;
	//u32 layerCount;
	VkDescriptorImageInfo descriptor;
	VkSampler sampler;

	void Destroy(const Device& device);

	void LoadFromFile(const char* path, const Device& device, VkQueue copyQueue, VkFormat format, VkImageUsageFlags usage, VkImageLayout requestedImageLayout);

	void CreateFromBuffer(void* buffer, VkDeviceSize bufferSize, const Device& device, VkQueue copyQueue, u32 texWidth, u32 texHeight, VkFormat format, VkImageUsageFlags usage, VkImageLayout requestedImageLayout);

private:
	void CreateImage(const Device& device, VkFormat format, VkImageUsageFlags usage);

	void CreateDefaultSampler(const Device& device);

	void CreateImageView(const Device& device, VkFormat format);
};