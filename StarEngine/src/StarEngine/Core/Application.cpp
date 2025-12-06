#include "sepch.h"
#include "StarEngine/Core/Application.h"

#include "StarEngine/Core/Log.h"
#include "StarEngine/Core/Input.h"
#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Scripting/ScriptEngine.h"
#include "StarEngine/Utilities/PlatformUtils.h"

#include <filesystem>
#include <cstdlib>
#include <fstream>   // 🔹 add this

#include "StarEngine/Platform/Discord/DiscordManager.h"


static constexpr uint64_t s_DiscordAppId = 1341559613228847155;

static std::filesystem::path GetDiscordTokenPath()
{
#ifdef SE_PLATFORM_WINDOWS
	const char* localAppData = std::getenv("LOCALAPPDATA");
	std::filesystem::path base = localAppData ? localAppData : ".";
	std::filesystem::path dir = base / "StarEngine";

	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	return dir / "discord_token.txt";
#else
	return std::filesystem::path("discord_token.txt");
#endif
}


namespace StarEngine
{
	Application* Application::s_Instance = nullptr;

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		SE_PROFILE_FUNCTION("Application::Application");

		SE_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;
		// Set working directory here
		if (!m_Specification.WorkingDirectory.empty())
			std::filesystem::current_path(m_Specification.WorkingDirectory);

		m_Window = Window::Create(WindowProps(m_Specification.Name));
		m_Window->SetEventCallback(SE_BIND_EVENT_FN(Application::OnEvent));

		Renderer::Init();

		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);

		// ----- Discord Social SDK -----
		m_DiscordSDK = CreateScope<DiscordSocialSDK>();

		if (!m_DiscordSDK->Initialize(s_DiscordAppId))
		{
			SE_CORE_ERROR("Failed to initialize Discord Social SDK");
		}
		else
		{
			// Set OnReady ONCE, works for both saved token + fresh auth
			auto* discordSDK = m_DiscordSDK.get();
			m_DiscordSDK->SetOnReadyCallback([discordSDK]()
				{
					DiscordManager::Init(
						discordSDK,
						"StarEditor",             // or "StarEngine" if you prefer
						"stareditor",    // large image key
						"starengine"     // small image key
					);
				});

			// 1) Try loading saved token
			m_DiscordAccessToken = LoadDiscordTokenFromDisk();

			if (!m_DiscordAccessToken.empty())
			{
				SE_CORE_INFO("Using saved Discord token");
				m_DiscordSDK->UpdateToken(m_DiscordAccessToken);
			}
			else
			{
				SE_CORE_INFO("No saved Discord token, starting Discord authorization");

				m_DiscordSDK->Authorize(
					[this](bool success, const std::string& accessToken)
					{
						if (!success)
						{
							SE_CORE_ERROR("Discord authorization failed (see previous logs for reason)");
							return;
						}

						SE_CORE_INFO("Discord authorization succeeded, saving token");
						m_DiscordAccessToken = accessToken;
						SaveDiscordTokenToDisk(accessToken);

						// You *can* set an initial activity here,
						// but DiscordManager::Init + RefreshPresence will also handle it.
						DiscordActivity activity;
						activity.State = "In Editor";
						activity.Details = "Messing with StarEngine";
						activity.StartTimestamp = std::time(nullptr);

						m_DiscordSDK->UpdateActivity(activity);
					});
			}
		}


	}

	Application::~Application()
	{
		SE_PROFILE_FUNCTION("Application::~Application");

		ScriptEngine::Shutdown();
		Renderer::Shutdown();

		if (m_DiscordSDK)
			m_DiscordSDK->Shutdown();
	}

	void Application::PushLayer(Layer* layer)
	{
		SE_PROFILE_FUNCTION("Application::PushLayer");

		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		SE_PROFILE_FUNCTION("Application::PushOverlay");

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
		SE_PROFILE_FUNCTION("Application::OnEvent");

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(SE_BIND_EVENT_FN(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(SE_BIND_EVENT_FN(Application::OnWindowResize));

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (e.Handled)
				break;
			(*it)->OnEvent(e);
		}
	}

	void Application::Run()
	{
		SE_PROFILE_FUNCTION("Application::Run");

		while (m_Running)
		{
			SE_PROFILE_SCOPE("RunLoop");

			float time = Time::GetTime();
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			ExecuteMainThreadQueue();

			if (!m_Minimized)
			{
				{
					SE_PROFILE_SCOPE("LayerStack OnUpdate");

					for (Layer* layer : m_LayerStack)
						layer->OnUpdate(timestep);
				}

				m_ImGuiLayer->Begin();
				{
					SE_PROFILE_SCOPE("LayerStack OnImGuiRender");

					for (Layer* layer : m_LayerStack)
						layer->OnImGuiRender();
				}
				m_ImGuiLayer->End();
			}

			if (m_DiscordSDK)
				m_DiscordSDK->Update();

			m_Window->OnUpdate();
			SE_PROFILE_MARK_FRAME;
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		SE_PROFILE_FUNCTION("Application::OnWindowResize");

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

	std::string Application::LoadDiscordTokenFromDisk()
	{
		auto path = GetDiscordTokenPath();
		if (!std::filesystem::exists(path))
			return {};

		std::ifstream in(path);
		if (!in)
			return {};

		std::string token;
		std::getline(in, token);
		return token;
	}

	void Application::SaveDiscordTokenToDisk(const std::string& token)
	{
		auto path = GetDiscordTokenPath();
		std::ofstream out(path, std::ios::trunc);
		if (!out)
		{
			SE_CORE_ERROR("Failed to write Discord token file: {}", path.string());
			return;
		}

		out << token;
	}


}
