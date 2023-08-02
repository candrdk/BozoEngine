#pragma once

// For now, we include windows, and glfw everywhere
#include <Windows.h>
#include <volk.h>
#include <GLFW/glfw3.h>

// Everyone uses vector
#include <vector>
#include "span.h"

// VkResult reflection header for logging
#include <vulkan/vk_enum_string_helper.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

// Typedefs
typedef uint8_t	 u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t	i8;
typedef int16_t	i16;
typedef int32_t	i32;
typedef int64_t	i64;

// Global defines
#define MAX_FRAMES_IN_FLIGHT 2

#define Check(expression, message, ...) do {	\
	if(!(expression)) {							\
		fprintf(stderr, "\nCheck `" #expression "` failed in %s at line %d.\n\t(file: %s)\n", __FUNCTION__, __LINE__, __FILE__); \
		if(message[0] != '\0') {				\
			char _check_str[512];				\
			_snprintf(_check_str, arraysize(_check_str), message, __VA_ARGS__); \
			fprintf(stderr, "Message: '%s'\n", _check_str); \
		}										\
		abort();								\
	} } while(0)

#define VkCheck(vkCall, message) do {					\
	VkResult _vkcheck_result = vkCall;					\
	if(_vkcheck_result != VK_SUCCESS) {					\
		fprintf(stderr, "\nVkCheck failed with `%s` in %s at line %d.\n\t(file: %s)\n", string_VkResult(_vkcheck_result), __FUNCTION__, __LINE__, __FILE__); \
		fprintf(stderr, "Message: '%s'\n", message);	\
		abort();										\
	} } while(0)

#define VkAssert(vkCall) Check((vkCall) == VK_SUCCESS, "")

template <typename T, size_t N>
char(&ArraySizeHelper(T(&array)[N]))[N];

#define arraysize(array) (sizeof(ArraySizeHelper(array)))