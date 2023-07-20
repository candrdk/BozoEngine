#include "Tools.h"

void ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageSubresourceRange subresourceRange, 
	VkPipelineStageFlags2	srcStage,	VkPipelineStageFlags2	dstStage, 
	VkAccessFlags2			srcAccess,	VkAccessFlags2			dstAccess,
	VkImageLayout			srcLayout,	VkImageLayout			dstLayout) 
{
	VkImageMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = srcStage,
		.srcAccessMask = srcAccess,
		.dstStageMask = dstStage,
		.dstAccessMask = dstAccess,
		.oldLayout = srcLayout,
		.newLayout = dstLayout,
		.image = image,
		.subresourceRange = subresourceRange
	};

	VkDependencyInfo dependencyInfo = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier
	};

	vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
};

void ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageAspectFlags aspectMask,
	VkPipelineStageFlags2	srcStage,	VkPipelineStageFlags2	dstStage,
	VkAccessFlags2			srcAccess,	VkAccessFlags2			dstAccess,
	VkImageLayout			srcLayout,	VkImageLayout			dstLayout) 
{
	VkImageSubresourceRange subresourceRange = {
		.aspectMask = aspectMask,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	ImageBarrier(cmdBuffer, image, subresourceRange, srcStage, dstStage, srcAccess, dstAccess, srcLayout, dstLayout);
}