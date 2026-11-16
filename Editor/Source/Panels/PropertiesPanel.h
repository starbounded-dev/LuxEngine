// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/SceneHierarchyPanel.h"

namespace Lux
{

	class PropertiesPanel : public EditorPanel
	{
	public:
		explicit PropertiesPanel(const Ref<SceneHierarchyPanel>& hierarchy) : m_Hierarchy(hierarchy)
		{
		}
		void OnImGuiRender(bool& open) override
		{
			m_Hierarchy->OnPropertiesImGuiRender(open);
		}
		void OnEvent(Event& event) override
		{
			m_Hierarchy->OnPropertiesEvent(event);
		}
		void OnClose() override
		{
			m_Hierarchy->OnPropertiesClosed();
		}

	private:
		Ref<SceneHierarchyPanel> m_Hierarchy;
	};

} // namespace Lux
