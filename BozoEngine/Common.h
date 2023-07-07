#pragma once

#include <Windows.h>

#include <volk.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <vector>
#include <assert.h>

#include "Tools.h"

typedef uint8_t	 u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t	i8;
typedef int16_t	i16;
typedef int32_t	i32;
typedef int64_t	i64;

template <typename T, size_t N>
char(&ArraySizeHelper(T(&array)[N]))[N];

#define arraysize(array) (sizeof(ArraySizeHelper(array)))