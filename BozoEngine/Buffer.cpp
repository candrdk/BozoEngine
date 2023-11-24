#include "Buffer.h"

static VkBufferUsageFlags ParseUsageFlags(Usage value) {
	VkBufferUsageFlags usage = 0;

	if (HasFlag(value, Usage::TRANSFER_SRC))		usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	if (HasFlag(value, Usage::TRANSFER_DST))		usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (HasFlag(value, Usage::VERTEX_BUFFER))		usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (HasFlag(value, Usage::INDEX_BUFFER))		usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (HasFlag(value, Usage::UNIFORM_BUFFER))		usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	return usage;
}

static VkMemoryPropertyFlags GetMemoryProperties(Memory memory) {
	switch (memory) {
	case Memory::DEFAULT:	return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	case Memory::UPLOAD:	return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	case Memory::READBACK:	return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	default:				return VK_MEMORY_PROPERTY_FLAG_BITS_MAX_ENUM;
	}
}

bool Buffer::Map(const Device& device) {
	return vkMapMemory(device.logicalDevice, memory, 0, size, 0, (void**)&mapped);
}

void Buffer::Unmap(const Device& device) {
	if (mapped) {
		vkUnmapMemory(device.logicalDevice, memory);
		mapped = nullptr;
	}
}

bool Buffer::Flush(const Device& device) {
	VkMappedMemoryRange mappedRange = {
		.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
		.memory = memory,
		.offset = 0,
		.size = size
	};

	return VK_SUCCESS == vkFlushMappedMemoryRanges(device.logicalDevice, 1, &mappedRange);
}

void Buffer::Destroy(const Device& device) {
	Unmap(device);

	if (buffer) vkDestroyBuffer(device.logicalDevice, buffer, nullptr);
	if (memory) vkFreeMemory(device.logicalDevice, memory, nullptr);
}

Buffer Buffer::Create(const Device& device, const BufferDesc&& desc) {
	Buffer buffer;

	VkDeviceSize size = desc.byteSize ? desc.byteSize : desc.initialData.size();
	Check(size != 0, "BufferDesc must contain initial data of nonzero size if desc.bytesize is zero");

	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = ParseUsageFlags(desc.usage),
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VkCheck(vkCreateBuffer(device.logicalDevice, &bufferInfo, nullptr, &buffer.buffer), "Failed to create buffer");

	VkDebugUtilsObjectNameInfoEXT nameInfo = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = VK_OBJECT_TYPE_BUFFER,
		.objectHandle = (u64)buffer.buffer,
		.pObjectName = desc.debugName
	};
	VkCheck(vkSetDebugUtilsObjectNameEXT(device.logicalDevice, &nameInfo), "Failed to name buffer");

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device.logicalDevice, buffer.buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = device.GetMemoryType(memRequirements.memoryTypeBits, GetMemoryProperties(desc.memory))
	};

	VkCheck(vkAllocateMemory(device.logicalDevice, &allocInfo, nullptr, &buffer.memory), "Failed to allocate buffer memory");
	buffer.size = memRequirements.size;

	if (desc.initialData.data() != nullptr) {
		buffer.Map(device);
		memcpy(buffer.mapped, desc.initialData.data(), desc.initialData.size());
		if (!(GetMemoryProperties(desc.memory) & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
			buffer.Flush(device);
		}
		buffer.Unmap(device);
	}

	vkBindBufferMemory(device.logicalDevice, buffer.buffer, buffer.memory, 0);

	return buffer;
}