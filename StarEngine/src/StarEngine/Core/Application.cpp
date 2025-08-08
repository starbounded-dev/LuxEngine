#include "sepch.h"
#include "Application.h"

#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Renderer/Framebuffer.h"
#include "StarEngine/Renderer/UI/Font.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include "StarEngine/ImGui/Colors.h"

#include "StarEngine/Asset/AssetManager.h"
#include "StarEngine/Audio/AudioEngine.h"

#include "Input.h"
#include "Memory.h"
#include "FatalSignal.h"

#include "imgui_internal.h"

#include "StarEngine/Scripting/ScriptEngine.h"

#include "StarEngine/Utilities/StringUtils.h"
#include "StarEngine/Debug/Profiler.h"

#include <filesystem>
#include <nfd.hpp>

#include "StarEngine/Editor/EditorApplicationSettings.h"

extern bool g_ApplicationRunning;
extern ImGuiContext* GImGui;
namespace StarEngine
{
#define BIND_EVENT_FN(fn) std::bind(&Application::##fn, this, std::placeholders::_1)

	Application* Application::s_Instance = nullptr;

	static std::thread::id s_MainThreadID;

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification), m_RenderThread(specification.CoreThreadingPolicy), m_AppSettings("App.hsettings")
	{
		//FatalSignal::Install();

		s_Instance = this;
		s_MainThreadID = std::this_thread::get_id();

		m_AppSettings.Deserialize();

		m_RenderThread.Run();

		if (!specification.WorkingDirectory.empty())
			std::filesystem::current_path(specification.WorkingDirectory);

		m_Profiler = snew PerformanceProfiler();

		Renderer::SetConfig(specification.RenderConfig);

		WindowSpecification windowSpec;
		windowSpec.Title = specification.Name;
		windowSpec.Width = specification.WindowWidth;
		windowSpec.Height = specification.WindowHeight;
		windowSpec.Decorated = specification.WindowDecorated;
		windowSpec.Fullscreen = specification.Fullscreen;
		windowSpec.VSync = specification.VSync;
		windowSpec.IconPath = specification.IconPath;
		m_Window = std::unique_ptr<Window>(Window::Create(windowSpec));
		m_Window->Init();
		m_Window->SetEventCallback([this](Event& e) { OnEvent(e); });

		// Load editor settings (will generate default settings if the file doesn't exist yet)
		//EditorApplicationSettingsSerializer::Init();

		SE_CORE_VERIFY(NFD::Init() == NFD_OKAY);

		// Init renderer and execute command queue to compile all shaders
		Renderer::Init();
		// Render one frame (TODO: maybe make a func called Pump or something)
		m_RenderThread.Pump();

		if (specification.StartMaximized)
			m_Window->Maximize();
		else
			m_Window->CenterWindow();
		m_Window->SetResizable(specification.Resizable);

		if (m_Specification.EnableImGui)
		{
			m_ImGuiLayer = ImGuiLayer::Create();
			PushOverlay(m_ImGuiLayer);
		}

		ScriptEngine::Init();
		AudioEngine::Init();
		Font::Init();
	}

	Application::~Application()
	{
		ScriptEngine::Shutdown();

		NFD::Quit();

		EditorApplicationSettingsSerializer::SaveSettings();

		m_Window->SetEventCallback([](Event& e) {});

		m_RenderThread.Terminate();

		for (Layer* layer : m_LayerStack)
		{
			layer->OnDetach();
			delete layer;
		}

		//ScriptEngine::Shutdown();
		Project::SetActive(nullptr);
		Font::Shutdown();
		AudioEngine::Shutdown();

		Renderer::Shutdown();

		delete m_Profiler;
		m_Profiler = nullptr;
	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::PopLayer(Layer* layer)
	{
		m_LayerStack.PopLayer(layer);
		layer->OnDetach();
	}

	void Application::PopOverlay(Layer* layer)
	{
		m_LayerStack.PopOverlay(layer);
		layer->OnDetach();
	}

	void Application::RenderImGui()
	{
		SE_PROFILE_FUNCTION("Application::RenderImGui");
		SE_SCOPE_PERF("Application::RenderImGui");

		m_ImGuiLayer->Begin();

		for (int i = 0; i < m_LayerStack.Size(); i++)
			m_LayerStack[i]->OnImGuiRender();
	}

	void Application::SyncEvents()
	{
		std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
		for (auto& [synced, _] : m_EventQueue)
		{
			synced = true;
		}
	}

	void Application::Run()
	{
		SE_PROFILE_FUNCTION("Application::Run");

		OnInit();
		while (m_Running)
		{
			SE_PROFILE_SCOPE("RunLoop");

			//Wait for render thread to finish frame
			{
				SE_PROFILE_SCOPE("Wait");
				Timer timer;

				m_RenderThread.BlockUntilRenderComplete();

				m_PerformanceTimers.MainThreadWaitTime = timer.ElapsedMillis();
			}

			static uint64_t frameCounter = 0;
			SE_CORE_INFO("-- BEGIN FRAME {0}", frameCounter);

			ProcessEvents();

			m_ProfilerPreviousFrameData = m_Profiler->GetPerFrameData();
			m_Profiler->Clear();

			m_RenderThread.NextFrame();

			// Start rendering previous frame
			m_RenderThread.Kick();

			if (!m_Minimized)
			{
				Timer cpuTimer;

				// On Render Thread
				Renderer::Submit([&]()
					{
						//m_Window->GetSwapChain().BeginFrame();
						m_Window->BeginFrame();
					});

				Renderer::BeginFrame();
				{
					SE_SCOPE_PERF("Application Layer::OnUpdate");
					for (Layer* layer : m_LayerStack)
					{
						layer->OnUpdate(m_TimeStep);
					}
				}

				Ref<Scene> activeScene = ScriptEngine::GetInstance().CurrentScene();
				if (activeScene)
				{
					m_PerformanceTimers.ScriptUpdate = activeScene->GetPerformanceTimers().ScriptUpdate;
					m_PerformanceTimers.PhysicsStepTime = activeScene->GetPerformanceTimers().PhysicsStep;
				}

				//Render ImGui on render Thread
				Application* app = this;
				if (m_Specification.EnableImGui)
				{
					Renderer::Submit([app]() { app->RenderImGui(); });
					Renderer::Submit([=]() { m_ImGuiLayer->End(); });
				}
				Renderer::EndFrame();

				//On Render thread
				Renderer::Submit([&]() {

					//m_Window->GetSwapChain().BeginFrame();
					// Renderer::WaitAndRender();
					m_Window->Present();
					GetGraphicsDevice()->runGarbageCollection();
				});

				m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % Renderer::GetConfig().FramesInFlight;
				m_PerformanceTimers.MainThreadWorkTime = cpuTimer.ElapsedMillis();
			}

			//ScriptEngine::InitializeRuntimeDuplicatedEntities();
			Input::ClearReleasedKeys();

			float time = GetTime();
			m_Frametime = time - m_LastFrameTime;
			m_TimeStep = glm::min<float>(m_Frametime, 0.0333f);
			m_LastFrameTime = time;

			SE_CORE_INFO("-- END FRAME {0}", frameCounter);
			frameCounter++;

			SE_PROFILE_MARK_FRAME;
		}
		OnShutdown();
	}

	void Application::Close()
	{
		m_Running = false;
	}

	void Application::OnShutdown()
	{
		m_EventCallbacks.clear();
		g_ApplicationRunning = false;
	}

	void Application::ProcessEvents()
	{
		Input::TransitionPressedKeys();
		Input::TransitionPressedButtons();

		m_Window->ProcessEvents();

		// Note (0x): we have no control over what func() does.  holding this lock while calling func() is a bad idea:
		// 1) func() might be slow (means we hold the lock for ages)
		// 2) func() might result in events getting queued, in which case we have a deadlock
		std::scoped_lock<std::mutex> lock(m_EventQueueMutex);

		// Process custom event queue, up until we encounter an event that is not yet sync'd
		// If application queues such events, then it is the application's responsibility to call
		// SyncEvents() at the appropriate time.
		while (m_EventQueue.size() > 0)
		{
			const auto& [synced, func] = m_EventQueue.front();
			if (!synced)
			{
				break;
			}
			func();
			m_EventQueue.pop_front();
		}
	}

	void Application::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) { return OnWindowResize(e); });
		dispatcher.Dispatch<WindowMinimizeEvent>([this](WindowMinimizeEvent& e) { return OnWindowMinimize(e); });
		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) { return OnWindowClose(e); });

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); )
		{
			(*--it)->OnEvent(event);
			if (event.Handled)
				break;
		}

		if (event.Handled)
			return;

		// TODO(Peter): Should these callbacks be called BEFORE the layers recieve events?
		//				We may actually want that since most of these callbacks will be functions REQUIRED in order for the game
		//				to work, and if a layer has already handled the event we may end up with problems
		for (auto& eventCallback : m_EventCallbacks)
		{
			eventCallback(event);

			if (event.Handled)
				break;
		}

	}

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		const uint32_t width = e.GetWidth(), height = e.GetHeight();
		if (width == 0 || height == 0)
		{
			//m_Minimized = true;
			return false;
		}
		//m_Minimized = false;

		auto& window = m_Window;
		Renderer::Submit([&window, width, height]() mutable
			{
				//m_Window->GetDeviceManager()->ResizeSwapChain();
				//window->GetSwapChain().OnResize(width, height);
			});

		return false;
	}

	bool Application::OnWindowMinimize(WindowMinimizeEvent& e)
	{
		m_Minimized = e.IsMinimized();
		return false;
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		Close();
		return false; // give other things a chance to react to window close
	}

	float Application::GetTime() const
	{
		return (float)glfwGetTime();
	}

	const char* Application::GetConfigurationName()
	{
		return SE_BUILD_CONFIG_NAME;
	}

	const char* Application::GetPlatformName()
	{
		return SE_BUILD_PLATFORM_NAME;
	}

	std::thread::id Application::GetMainThreadID() { return s_MainThreadID; }

	bool Application::IsMainThread()
	{
		return std::this_thread::get_id() == s_MainThreadID;
	}

}
