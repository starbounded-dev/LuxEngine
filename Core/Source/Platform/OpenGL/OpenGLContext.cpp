#include "sepch.h"
#include "Platform/OpenGL/OpenGLContext.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

namespace StarEngine {

	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
		: m_WindowHandle(windowHandle)
	{
		SE_CORE_ASSERT(windowHandle, "Window handle is null!")
	}

	void OpenGLContext::Init()
	{
		SE_PROFILE_FUNCTION("OpenGLContext::Init");

		glfwMakeContextCurrent(m_WindowHandle);
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		SE_CORE_ASSERT(status, "Failed to initialize Glad!");

		SE_CORE_INFO("OpenGL Info:");
		SE_CORE_INFO("  Vendor: {0}", (const char*)glGetString(GL_VENDOR));
		SE_CORE_INFO("  Renderer: {0}", (const char*)glGetString(GL_RENDERER));
		SE_CORE_INFO("  Version: {0}", (const char*)glGetString(GL_VERSION));

		SE_CORE_ASSERT(GLVersion.major > 4 || (GLVersion.major == 4 && GLVersion.minor >= 5), "StarEngine requires at least OpenGL version 4.5!");
	}

	void OpenGLContext::SwapBuffers()
	{
		SE_PROFILE_FUNCTION("OpenGLContext::SwapBuffers");

		glfwSwapBuffers(m_WindowHandle);
	}
}
