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
			// Frames per second to pace the main loop to when VSync is off. 0 == unlimited.
			int* TargetFrameRate = nullptr;
			// Swapchain image count. Sets the frame-rate ceiling under MAILBOX.
			int* SwapChainBufferCount = nullptr;
			// IMMEDIATE instead of MAILBOX when VSync is off.
			bool* PreferImmediatePresentMode = nullptr;
			bool* UseGizmoSnap = nullptr;
			float* TranslationSnapValue = nullptr;
			float* RotationSnapValue = nullptr;
			bool* ShowBoundingBoxes = nullptr;
			bool* ShowEntityIcons = nullptr;
			bool* ShowViewportPerformanceHUD = nullptr;
			bool* ShowPhysicsColliders = nullptr;
			// Editor layout mode (Simple vs Advanced). Read through the pointer; switching modes does
			// more than flip a bool (re-docks panels), so the change is routed through the callback
			// rather than written directly.
			bool* SimpleLayout = nullptr;
			std::function<void(bool)> OnLayoutModeChanged;
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
		void DrawDiscordPage();

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

		// Edited in the Discord page; only committed to settings when it actually changes.
		std::string m_DiscordApplicationID;
	};

}
