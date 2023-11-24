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

// TODO:
// We currently store the actually allocated size of the buffer in the size field.
// This creates a weird situation where we have to pass the size we specified when
// creating the buffer to GetBinding. Should figure out whether the actual size is
// needed. Might be better to just store the requested size of the buffer instead?
struct Buffer {
	VkBuffer		buffer = VK_NULL_HANDLE;
	VkDeviceMemory	memory = VK_NULL_HANDLE;
	u8*				mapped = nullptr;
	VkDeviceSize	size   = 0;

	struct Binding {
		u32 binding;
		VkBuffer buffer;
		VkDeviceSize offset;
		VkDeviceSize size;
	} GetBinding(u32 binding, VkDeviceSize bindSize) const {
		return { binding, buffer, 0, bindSize };
	}

	bool Map  (const Device& device);
	void Unmap(const Device& device);
	bool Flush(const Device& device);

	void Destroy(const Device& device);

	static Buffer Create(const Device& device, const BufferDesc&& desc);
};