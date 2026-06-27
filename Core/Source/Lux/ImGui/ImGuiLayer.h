#pragma once

#include "Lux/Core/Layer.h"

#include "ImGuiRenderer.h"

#include <functional>

namespace Lux {

	class ImGuiLayer : public Layer
	{
	public:
		static ImGuiLayer* Create() { return lnew ImGuiLayer(); }

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void Begin();
		void End();
		void SubmitDrawData();

		void SetDarkThemeColors();
		void SetDarkThemeV2Colors();

		void AllowInputEvents(bool allowEvents);

		ImGuiRenderer* GetImGuiRenderer();
	public:
		ImGuiLayer() = default;
		virtual ~ImGuiLayer() = default;
	private:
		void InitPlatformInterface();
	private:
		std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;
		std::vector<std::function<void()>> m_PendingRenderTasks;
	};



}
