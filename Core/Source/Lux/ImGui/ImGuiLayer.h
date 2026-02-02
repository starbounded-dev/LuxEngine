#pragma once

#include "Lux/Core/Layer.h"

#include "Lux/Events/ApplicationEvent.h"
#include "Lux/Events/KeyEvent.h"
#include "Lux/Events/MouseEvent.h"

namespace Lux {

	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnEvent(Event& e) override;

		void Begin();
		void End();

		void BlockEvents(bool block) { m_BlockEvents = block; }

		void SetDarkThemeColors();

		uint32_t GetActiveWidgetID() const;
	private:
		bool m_BlockEvents = true;
	};

}
