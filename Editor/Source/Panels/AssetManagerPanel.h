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
