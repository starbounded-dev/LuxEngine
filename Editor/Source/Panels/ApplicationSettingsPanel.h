#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Project/UserPreferences.h"

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
			bool* ShowViewportPerformanceHUD = nullptr;
			bool* ShowPhysicsColliders = nullptr;
			std::function<void()> OnPreferencesChanged;
		};

	public:
		ApplicationSettingsPanel(const Ref<ContentBrowserPanel>& contentBrowserPanel, EditorPreferencesBindings bindings, const Ref<UserPreferences>& userPreferences);
		virtual ~ApplicationSettingsPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void DrawPageList();
		void DrawEditorPage();
		void DrawViewportPage();
		void DrawContentBrowserPage();

		void SaveAutoOpenMostRecentProjectSetting(bool enabled) const;
		bool LoadAutoOpenMostRecentProjectSetting() const;
		void SaveUserPreferences() const;
		void ClearRecentProjects();

	private:
		Ref<ContentBrowserPanel> m_ContentBrowserPanel;
		Ref<UserPreferences> m_UserPreferences;
		EditorPreferencesBindings m_Bindings;

		uint32_t m_CurrentPage = 0;
		std::vector<SettingsPage> m_Pages;
	};

}
