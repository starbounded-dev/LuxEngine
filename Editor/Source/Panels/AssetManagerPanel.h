// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"

namespace Lux {

	class AssetManagerPanel : public EditorPanel
	{
	public:
		AssetManagerPanel() = default;
		virtual ~AssetManagerPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		char m_SearchBuffer[256]{};
		bool m_ShowOnlyLoaded = false;
	};

}
