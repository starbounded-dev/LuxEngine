#include "lpch.h"
#include "Platform/OpenGL/OpenGLContext.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

namespace Lux {

	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
		: m_WindowHandle(windowHandle)
	{
		LUX_CORE_ASSERT(windowHandle, "Window handle is null!")
	}

	void OpenGLContext::Init()
	{
		LUX_PROFILE_FUNCTION("OpenGLContext::Init");

		glfwMakeContextCurrent(m_WindowHandle);
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		LUX_CORE_ASSERT(status, "Failed to initialize Glad!");

		LUX_CORE_INFO("OpenGL Info:");
		LUX_CORE_INFO("  Vendor: {0}", (const char*)glGetString(GL_VENDOR));
		LUX_CORE_INFO("  Renderer: {0}", (const char*)glGetString(GL_RENDERER));
		LUX_CORE_INFO("  Version: {0}", (const char*)glGetString(GL_VERSION));

		LUX_CORE_ASSERT(GLVersion.major > 4 || (GLVersion.major == 4 && GLVersion.minor >= 5), "Lux requires at least OpenGL version 4.5!");
	}

	void OpenGLContext::SwapBuffers()
	{
		LUX_PROFILE_FUNCTION("OpenGLContext::SwapBuffers");

		glfwSwapBuffers(m_WindowHandle);
	}
}
