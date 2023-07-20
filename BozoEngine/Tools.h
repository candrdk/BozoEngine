#pragma once

#include "Common.h"

void ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageSubresourceRange subresourceRange,
	VkPipelineStageFlags2	srcStage,	VkPipelineStageFlags2	dstStage,
	VkAccessFlags2			srcAccess,	VkAccessFlags2			dstAccess,
	VkImageLayout			srcLayout,	VkImageLayout			dstLayout);

void ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageAspectFlags aspectMask,
	VkPipelineStageFlags2	srcStage,	VkPipelineStageFlags2	dstStage,
	VkAccessFlags2			srcAccess,	VkAccessFlags2			dstAccess,
	VkImageLayout			srcLayout,	VkImageLayout			dstLayout);