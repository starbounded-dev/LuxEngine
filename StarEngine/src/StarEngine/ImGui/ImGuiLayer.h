#pragma once

#include "StarEngine/Core/Layer.h"

#include "ImGuiRenderer.h"

namespace StarEngine {

	class ImGuiLayer : public Layer
	{
	public:
		static ImGuiLayer* Create() { return snew ImGuiLayer(); }

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		void Begin();
		void End();

		void SetDarkThemeColors();
		void SetDarkThemeV2Colors();

		void AllowInputEvents(bool allowEvents);
	public:
		ImGuiLayer() = default;
		virtual ~ImGuiLayer() = default;
	private:
		void InitPlatformInterface();
	private:
		std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;
	};



}
