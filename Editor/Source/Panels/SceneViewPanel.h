// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Viewport/Viewport.h"

#include <functional>

namespace Lux
{

	// The layer retains its viewport/renderer and specialized drawing. The registry owns
	// visibility, persistence and the View/command-palette entry like every other panel.
	class SceneViewPanel : public EditorPanel
	{
	public:
		SceneViewPanel(const Ref<Viewport>& viewport, std::function<void(bool&)> draw)
			: m_Viewport(viewport), m_Draw(std::move(draw))
		{
		}
		void OnImGuiRender(bool& open) override
		{
			m_Draw(open);
		}
		void OnClose() override
		{
			m_Viewport->Hide();
		}

	private:
		Ref<Viewport> m_Viewport;
		std::function<void(bool&)> m_Draw;
	};

} // namespace Lux
