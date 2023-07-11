#include "Tools.h"

// https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanTools.cpp
void SetImageLayout(
	VkCommandBuffer cmdBuffer, 
	VkImage image, 
	VkImageLayout oldLayout, 
	VkImageLayout newLayout, 
	VkImageSubresourceRange subresourceRange, 
	VkPipelineStageFlags srcStageMask, 
	VkPipelineStageFlags dstStageMask) 
{
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = subresourceRange
	};

	switch (oldLayout) {
	case VK_IMAGE_LAYOUT_UNDEFINED: barrier.srcAccessMask = 0; break;
	case VK_IMAGE_LAYOUT_PREINITIALIZED: barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
	default: break;
	}

	switch (newLayout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: 
		if (barrier.srcAccessMask == 0) { 
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT; 
		} 
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
	default: break;
	}

	vkCmdPipelineBarrier(cmdBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void SetImageLayout(
	VkCommandBuffer cmdBuffer,
	VkImage image,
	VkImageAspectFlags aspectMask,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask) 
{
	VkImageSubresourceRange subresourceRange = {
		.aspectMask = aspectMask,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	SetImageLayout(cmdBuffer, image, oldLayout, newLayout, subresourceRange, srcStageMask, dstStageMask);
}

void SetImageLayout2(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageSubresourceRange subresourceRange) {
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = subresourceRange
	};

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else {
		Check(false, "Unsupported layout transition");
	}

	vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void InsertImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageAspectFlags aspectMask, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask) {
	VkImageSubresourceRange subresourceRange = {
		.aspectMask = aspectMask,
		.baseMipLevel = 0,
		.levelCount = 1,
		.layerCount = 1
	};

	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = srcAccessMask,
		.dstAccessMask = dstAccessMask,
		.oldLayout = oldLayout, //VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = subresourceRange
	};

	vkCmdPipelineBarrier(cmdBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}