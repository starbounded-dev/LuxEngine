#pragma once

#include "Lux/Editor/EditorPanel.h"

#include <functional>
#include <vector>

namespace Lux {

	class ContentBrowserPanel;

	struct SettingsPage
	{
		using PageRenderFunction = std::function<void()>;

		const char* Name = "";
		PageRenderFunction RenderFunction;
	};

	class ApplicationSettingsPanel : public EditorPanel
	{
	public:
		struct EditorPreferencesBindings
		{
			bool* VSync = nullptr;
			bool* UseGizmoSnap = nullptr;
			float* TranslationSnapValue = nullptr;
			float* RotationSnapValue = nullptr;
			bool* ShowBoundingBoxes = nullptr;
			bool* ShowEntityIcons = nullptr;
			bool* ShowPhysicsColliders = nullptr;
			std::function<void()> OnPreferencesChanged;
		};

	public:
		ApplicationSettingsPanel(const Ref<ContentBrowserPanel>& contentBrowserPanel, EditorPreferencesBindings bindings);
		virtual ~ApplicationSettingsPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void DrawPageList();
		void DrawEditorPage();
		void DrawViewportPage();
		void DrawContentBrowserPage();

		void SaveAutoOpenMostRecentProjectSetting(bool enabled) const;
		bool LoadAutoOpenMostRecentProjectSetting() const;
		void ClearRecentProjects() const;

	private:
		Ref<ContentBrowserPanel> m_ContentBrowserPanel;
		EditorPreferencesBindings m_Bindings;

		uint32_t m_CurrentPage = 0;
		std::vector<SettingsPage> m_Pages;
	};

}
