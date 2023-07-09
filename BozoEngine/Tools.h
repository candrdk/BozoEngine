#pragma once

#include "Common.h"

void SetImageLayout(
	VkCommandBuffer cmdBuffer,
	VkImage image,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkImageSubresourceRange subresourceRange,
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask);

void SetImageLayout(
	VkCommandBuffer cmdBuffer,
	VkImage image,
	VkImageAspectFlags aspect,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask);

// Deprecate this
void SetImageLayout2(
	VkCommandBuffer commandBuffer,
	VkImage image,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkImageSubresourceRange subresourceRange);

void InsertImageBarrier(
	VkCommandBuffer cmdBuffer,
	VkImage image,
	VkImageAspectFlags aspectMask,
	VkAccessFlags srcAccessMask,
	VkAccessFlags dstAccessMask,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask);