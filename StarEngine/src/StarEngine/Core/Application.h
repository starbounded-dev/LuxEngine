#pragma once

#include "Timer.h"
#include "StarEngine/Core/Assert.h"
#include "StarEngine/Core/Base.h"

#include "StarEngine/Core/Window.h"
#include "StarEngine/Core/LayerStack.h"
#include "StarEngine/Core/Events/Event.h"
#include "StarEngine/Core/Events/ApplicationEvent.h"

#include "StarEngine/Core/Timestep.h"

#include "StarEngine/ImGui/ImGuiLayer.h"

#include "StarEngine/Core/Timer.h"

#include <deque>

int main(int argc, char** argv);

namespace StarEngine
{
	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](int index) const
		{
			SE_CORE_ASSERT(index < Count);
			return Args[index];
		}
	};

	struct ApplicationSpecification
	{
		std::string Name = "StarEngine Application";
		std::string WorkingDirectory;
		ApplicationCommandLineArgs CommandLineArgs;

		bool EnableImGui = true;
	};

	class Application
	{
		using EventCallbackFn = std::function<void(Event&)>;
	public:
		struct PerformanceTimers
		{
			float MainThreadWorkTime = 0.0f;
			float MainThreadWaitTime = 0.0f;
			float RenderThreadWorkTime = 0.0f;
			float RenderThreadWaitTime = 0.0f;
			float RenderThreadGPUWaitTime = 0.0f;

			float ScriptUpdate = 0.0f;
			float PhysicsStepTime = 0.0f;
		};
		public:
			Application(const ApplicationSpecification& specification);
			virtual ~Application();

			void OnEvent(Event& e);

			void PushLayer(Layer* layer);
			void PushOverlay(Layer* layer);

			Window& GetWindow() { return *m_Window; }

			void Close();

			ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }

			static Application& Get() { return *s_Instance; }

			const ApplicationSpecification& GetSpecification() const { return m_Specification; }

			static std::thread::id GetMainThreadID();
			static bool IsMainThread();
			void SubmitToMainThread(const std::function<void()>& function);

			void AddEventCallback(const EventCallbackFn& eventCallback) { m_EventCallbacks.push_back(eventCallback); }

			template<typename Func>
			void QueueEvent(Func&& func)
			{
				std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
				m_EventQueue.emplace_back(true, func);
			}

			// Creates & Dispatches an event either immediately, or adds it to an event queue which will be processed after the next call
			// to SyncEvents().
			// Waiting until after next sync gives the application some control over _when_ the events will be processed.
			// An example of where this is useful:
			// Suppose an asset thread is loading assets and dispatching "AssetReloaded" events.
			// We do not want those events to be processed until the asset thread has synced its assets back to the main thread.
			template<typename TEvent, bool DispatchImmediately = false, typename... TEventArgs>
			void DispatchEvent(TEventArgs&&... args)
			{
#ifndef SE_COMPILER_GCC
				// TODO(Emily): GCC causes this to fail for AnimationGraphCompiledEvent for some reason. Investigate.
				static_assert(std::is_assignable_v<Event, TEvent>);
#endif

				std::shared_ptr<TEvent> event = std::make_shared<TEvent>(std::forward<TEventArgs>(args)...);
				if constexpr (DispatchImmediately)
				{
					OnEvent(*event);
				}
				else
				{
					std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
					m_EventQueue.emplace_back(false, [event]() { Application::Get().OnEvent(*event); });
				}
			}

			// Mark all waiting events as sync'd.
			// Thus allowing them to be processed on next call to ProcessEvents()
			void SyncEvents();

			PerformanceProfiler* GetPerformanceProfiler() { return m_Profiler; }
			const PerformanceTimers& GetPerformanceTimers() const { return m_PerformanceTimers; }
			PerformanceTimers& GetPerformanceTimers() { return m_PerformanceTimers; }
			const std::unordered_map<const char*, PerformanceProfiler::PerFrameData>& GetProfilerPreviousFrameData() const { return m_ProfilerPreviousFrameData; }
		private:
			void Run();

			bool OnWindowClose(WindowCloseEvent& e);
			bool OnWindowResize(WindowResizeEvent& e);

			void ExecuteMainThreadQueue();
		private:
			ApplicationSpecification m_Specification;
			Scope<Window> m_Window;
			ImGuiLayer* m_ImGuiLayer;
			bool m_Running = true;
			bool m_Minimized = false;
			LayerStack m_LayerStack;
			float m_LastFrameTime = 0.0f;

			std::mutex m_EventQueueMutex;
			std::deque<std::pair<bool, std::function<void()>>> m_EventQueue;
			std::vector<EventCallbackFn> m_EventCallbacks;

			std::vector<std::function<void()>> m_MainThreadQueue;
			std::mutex m_MainThreadQueueMutex;

			PerformanceProfiler* m_Profiler = nullptr; // TODO: Should be null in Dist
			std::unordered_map<const char*, PerformanceProfiler::PerFrameData> m_ProfilerPreviousFrameData;

			PerformanceTimers m_PerformanceTimers; // TODO(Yan): remove for Dist
	private:
				static Application* s_Instance;
				friend int ::main(int argc, char** argv);
	};

	// To be defined in CLIENT
	Application* CreateApplication(ApplicationCommandLineArgs args);
}


