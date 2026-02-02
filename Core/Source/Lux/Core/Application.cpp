#include "lpch.h"
#include "Lux/Core/Application.h"

#include "Lux/Core/Log.h"

#include "Lux/Core/Input.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Scripting/ScriptEngine.h"

#include "Lux/Utils/PlatformUtils.h"

namespace Lux
{
	Application* Application::s_Instance = nullptr;

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		LUX_PROFILE_FUNCTION("Application::Application");

		LUX_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;
		// Set working directory here
		if (!m_Specification.WorkingDirectory.empty())
			std::filesystem::current_path(m_Specification.WorkingDirectory);

		m_Window = Window::Create(WindowProps(m_Specification.Name));
		m_Window->SetEventCallback(LUX_BIND_EVENT_FN(Application::OnEvent));

		Renderer::Init();

		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);
	}

	Application::~Application()
	{
		LUX_PROFILE_FUNCTION("Application::~Application");

		ScriptEngine::Shutdown();
		Renderer::Shutdown();
	}

	void Application::PushLayer(Layer* layer)
	{
		LUX_PROFILE_FUNCTION("Application::PushLayer");

		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		LUX_PROFILE_FUNCTION("Application::PushOverlay");

		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::Close()
	{
		m_Running = false;
	}

	void Application::SubmitToMainThread(const std::function<void()>& function)
	{
		std::scoped_lock<std::mutex> lock(m_MainThreadQueueMutex);

		m_MainThreadQueue.emplace_back(function);
	}

	void Application::OnEvent(Event& e)
	{
		LUX_PROFILE_FUNCTION("Application::OnEvent");

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(LUX_BIND_EVENT_FN(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(LUX_BIND_EVENT_FN(Application::OnWindowResize));

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (e.Handled)
				break;
			(*it)->OnEvent(e);
		}
	}

	void Application::Run()
	{
		LUX_PROFILE_FUNCTION("Application::Run");

		while (m_Running)
		{
			LUX_PROFILE_SCOPE("RunLoop");

			float time = Time::GetTime();
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			ExecuteMainThreadQueue();

			if (!m_Minimized)
			{
				{
					LUX_PROFILE_SCOPE("LayerStack OnUpdate");

					for (Layer* layer : m_LayerStack)
						layer->OnUpdate(timestep);
				}

				m_ImGuiLayer->Begin();
				{
					LUX_PROFILE_SCOPE("LayerStack OnImGuiRender");

					for (Layer* layer : m_LayerStack)
						layer->OnImGuiRender();
				}
				m_ImGuiLayer->End();
			}

			m_Window->OnUpdate();
			LUX_PROFILE_MARK_FRAME;
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		LUX_PROFILE_FUNCTION("Application::OnWindowResize");

		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}

		m_Minimized = false;
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}

	void Application::ExecuteMainThreadQueue()
	{
		std::scoped_lock<std::mutex> lock(m_MainThreadQueueMutex);

		for (auto& func : m_MainThreadQueue)
			func();

		m_MainThreadQueue.clear();
	}

}
