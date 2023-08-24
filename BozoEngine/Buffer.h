#pragma once

#include "Common.h"
#include "Device.h"

struct BufferDesc {
	const char* debugName = nullptr;

	u64 byteSize	= 0;
	Usage usage		= Usage::NONE;
	Memory memory	= Memory::DEFAULT;

	span<const u8> initialData;
};

struct Buffer {
	VkBuffer		buffer = VK_NULL_HANDLE;
	VkDeviceMemory	memory = VK_NULL_HANDLE;
	void*			mapped = nullptr;
	VkDeviceSize	size   = 0;

	bool Map  (const Device& device);
	void Unmap(const Device& device);
	bool Flush(const Device& device);

	void Destroy(const Device& device);

	static Buffer Create(const Device& device, const BufferDesc&& desc);
};