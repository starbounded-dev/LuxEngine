// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Editor/EditorPanel.h"

#include <optional>

namespace Lux {

	class Scene; // forward declare

	class LightSettingsPanel : public EditorPanel
	{
	public:
		LightSettingsPanel();
		virtual ~LightSettingsPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) override;
		void OnEvent(Event& event) override;
		void OnClose() override
		{
			m_IsFocused = false;
		}

		void SetSceneContext(const Ref<Scene>& context) override
		{
			m_Context = context;
		}
		void OnProjectChanged(const Ref<Project>& project) override
		{
			// OpenProject installs the new scene before broadcasting the project change.
			if (!project)
				m_Context = nullptr;
			m_IsFocused = false;
		}
		Ref<Scene> GetSceneContext() const
		{
			return m_Context;
		}

	private:
		void RenderDirectionalLight();
		void RenderPointLights();
		void RenderSpotLights();
		void RenderSkyLight();

	private:
		Ref<Scene> m_Context;
		bool m_IsFocused = false;
		int m_SelectedLightIndex = -1;
	};

}
