#pragma once

#include <source_location>
#include <vulkan/vk_enum_string_helper.h>

// SGR escape sequences: https://docs.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
#define SGR_SET_BG_GRAY  "\x1B[100;1m"
#define SGR_SET_BG_BLUE	 "\x1B[44;1m"
#define SGR_SET_BG_RED	 "\x1B[41;1m"
#define SGR_SET_TXT_BLUE "\x1B[34;1m"
#define SGR_SET_DEFAULT  "\x1B[0m"

// Hack to get both source_location as a default parameter 
// and variadic args for formatted string messages in the same function.
struct StringWithLocation {
	const char* str;
	std::source_location loc;
	StringWithLocation(const char* format = "", const std::source_location& location = std::source_location::current())
		: str{ format }, loc{ location } {}
};

// Assert an expresson evaluates to true. If not, the expression is printed, along with a (potentially) formatted message.
// The _assume(expression) is just there to get rid of "pointer could be null" errors.
#define Check(expression, message, ...) _Check(expression, #expression, message, __VA_ARGS__); _assume(expression)

// Assert result. If not true, expression and message are printed, along with source location information.
bool _Check(bool result, const char* expression, StringWithLocation message);

// Assert result. If not VK_SUCCESS, message is printed, along with source location information.
VkResult VkCheck(VkResult result, StringWithLocation message = StringWithLocation());

// Templated function definition cannot be seperated from its declaration.
// Assert result. If not true, expression and the formatted message are printed, along with source location information.
template <typename... Args>
bool _Check(bool result, const char* expression, StringWithLocation format, Args... args) {
	if (result == false) {
		char message[512];
		_snprintf_s(message, _TRUNCATE, format.str, args...);
		_Check(result, expression, StringWithLocation(message, format.loc));
	}
	return result;
}

template <typename... Args>
VkResult VkCheck(VkResult result, StringWithLocation format, Args... args) {
	if (result != VK_SUCCESS) {
		char message[512];
		_snprintf_s(message, _TRUNCATE, format.str, args...);
		VkCheck(result, StringWithLocation(message, format.loc));
	}
	return result;
}

// Get a DebugMessengerCreateInfo pointing to a custom DebugCallBack.
VkDebugUtilsMessengerCreateInfoEXT GetDebugMessengerCreateInfo();