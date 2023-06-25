#include <GLFW/glfw3.h>

#include <volk.h>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <stdio.h>

int main(int argc, char* argv[]) {
	VkResult volkLoaded = volkInitialize();

	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(800, 600, "BozoEngine", nullptr, nullptr);

	uint32_t extensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	printf("%u extensions supported\n", extensionCount);

	glm::mat4 matrix;
	glm::vec4 vec;
	glm::vec4 test = matrix * vec;

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}