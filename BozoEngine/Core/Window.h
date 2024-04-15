#pragma once

#include <GLFW/glfw3.h>
#undef APIENTRY	// fix macro redefinition warning

struct GLFWcallbacks {
    GLFWframebuffersizefun FramebufferSize;
    GLFWmousebuttonfun MouseButton;
    GLFWcursorposfun CursorPos;
    GLFWkeyfun Key;
};

class Window {
public:
    Window(const char* title, int width, int height, const GLFWcallbacks& callbacks) {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

        m_width  = width; 
        m_height = height;
        m_window = glfwCreateWindow(m_width, m_height, title, nullptr, nullptr);

        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

        glfwSetFramebufferSizeCallback(m_window, callbacks.FramebufferSize);
        glfwSetMouseButtonCallback(m_window, callbacks.MouseButton);
        glfwSetCursorPosCallback(m_window, callbacks.CursorPos);
        glfwSetKeyCallback(m_window, callbacks.Key);
    }

    ~Window() {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

	// TODO: Comment to explain this part - I forgot lol. Why the while loop?
    void WaitResizeComplete() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);

        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(m_window, &width, &height);
            glfwWaitEvents();
        }

        return;
    }

    void GetWindowSize(int* width, int* height) {
        glfwGetFramebufferSize(m_window, width, height);
    }

    bool ShouldClose() {
        return glfwWindowShouldClose(m_window);
    }


    GLFWwindow* m_window = nullptr;
    int m_width, m_height;
};