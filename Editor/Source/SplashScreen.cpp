// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SplashScreen.h"

#include "Lux/Core/Version.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiFonts.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Utilities/FileDialogs.h"
#include "Lux/Utilities/FileSystem.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <ctime>
#include <format>

namespace Lux {

	namespace {

		namespace Theme = Colors::Theme;

		// Card geometry and type ramp, in unscaled pixels (matches the "LuxEngine Splash Screen"
		// design canvas). Everything goes through S() so it follows the editor's UI scale, which
		// ImGuiLayer bakes into the fonts and applies to the style.
		constexpr float k_Width = 720.0f;
		constexpr float k_Rounding = 14.0f;
		constexpr float k_BannerHeight = 212.0f;
		constexpr float k_FormBannerHeight = 104.0f;
		constexpr float k_StripeHeight = 3.0f;
		constexpr float k_PadX = 28.0f;
		constexpr float k_BodyTopPad = 26.0f;
		constexpr float k_BodyBottomPad = 24.0f;
		constexpr float k_LeftColumnWidth = 236.0f;
		constexpr float k_ColumnGap = 28.0f;
		constexpr float k_ActionHeight = 48.0f;
		constexpr float k_FieldHeight = 44.0f;
		constexpr float k_RowHeight = 52.0f;
		constexpr float k_RowGap = 6.0f;
		constexpr float k_TileSize = 34.0f;
		constexpr float k_FooterHeight = 48.0f;
		constexpr float k_ControlRounding = 8.0f;
		constexpr int k_MaxRecentRows = 6;

		constexpr float k_TitleSize = 52.0f;
		constexpr float k_FormTitleSize = 30.0f;
		constexpr float k_SubtitleSize = 14.0f;
		constexpr float k_BodySize = 15.0f;
		constexpr float k_NoteSize = 13.0f;
		constexpr float k_SmallSize = 12.0f;
		constexpr float k_CaptionSize = 11.0f;

		// The "light" motif in the banner: concentric rings around a lit point, banner-local.
		constexpr ImVec2 k_LightCenter = { 560.0f, 86.0f };
		constexpr ImVec2 k_FormLightCenter = { 640.0f, 52.0f };

		// Folders EditorLayer::CreateProject makes, shown in the "Will create" preview.
		constexpr std::array k_CreatedFolders = { "Assets/Scenes", "Assets/Scripts/Source", "Assets/Materials", "Assets/Meshes/Source", "Assets/Textures" };

		constexpr const char* k_MiddleDot = "\xC2\xB7";

		// Main-thread only (ImGui), refreshed at the top of every OnImGuiRender.
		float s_UiScale = 1.0f;
		float S(float value) { return value * s_UiScale; }
		ImVec2 S(float x, float y) { return ImVec2(x * s_UiScale, y * s_UiScale); }

		// The name becomes the C# namespace and assembly name (ProjectConfig::DefaultNamespace /
		// ScriptModulePath), so it has to be a valid identifier.
		bool IsValidProjectName(const char* name)
		{
			if (!name || !name[0])
				return false;
			if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_')
				return false;

			for (const char* c = name; *c; c++)
			{
				if (!std::isalnum(static_cast<unsigned char>(*c)) && *c != '_')
					return false;
			}
			return true;
		}

		std::string FormatLastOpened(time_t lastOpened)
		{
			if (lastOpened <= 0)
				return {};

			const auto seconds = static_cast<int64_t>(std::difftime(std::time(nullptr), lastOpened));
			if (seconds < 60)
				return "just now";
			if (seconds < 60 * 60)
				return std::format("{} min ago", seconds / 60);
			if (seconds < 60 * 60 * 24)
				return std::format("{} h ago", seconds / (60 * 60));

			const int64_t days = seconds / (60 * 60 * 24);
			if (days == 1)
				return "yesterday";
			if (days < 60)
				return std::format("{} days ago", days);
			if (days < 365)
				return std::format("{} months ago", days / 30);
			return std::format("{} years ago", days / 365);
		}

		void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& value)
		{
			std::memset(buffer, 0, bufferSize);
			std::memcpy(buffer, value.data(), std::min(value.size(), bufferSize - 1));
		}

		ImU32 WithAlpha(ImU32 colour, float alpha)
		{
			ImVec4 value = ImGui::ColorConvertU32ToFloat4(colour);
			value.w *= alpha;
			return ImGui::ColorConvertFloat4ToU32(value);
		}

		// ---- Text drawn straight into the draw list, in a named font at an unscaled size ----------

		ImVec2 MeasureText(const char* font, float size, const char* text, float wrapWidth = 0.0f)
		{
			ImGuiEx::ScopedFont scopedFont(ImGuiEx::Fonts::Get(font), S(size));
			return ImGui::CalcTextSize(text, nullptr, false, wrapWidth > 0.0f ? wrapWidth : -1.0f);
		}

		void DrawText(ImDrawList* drawList, const char* font, float size, ImVec2 pos, ImU32 colour, const char* text, float wrapWidth = 0.0f)
		{
			ImGuiEx::ScopedFont scopedFont(ImGuiEx::Fonts::Get(font), S(size));
			drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, colour, text, nullptr, wrapWidth);
		}

		// Shortens `text` with a trailing "..." until it fits `maxWidth` (scaled pixels).
		std::string FitText(const char* font, float size, const std::string& text, float maxWidth)
		{
			if (MeasureText(font, size, text.c_str()).x <= maxWidth)
				return text;

			std::string fitted = text;
			while (!fitted.empty() && MeasureText(font, size, (fitted + "...").c_str()).x > maxWidth)
				fitted.pop_back();
			return fitted + "...";
		}

		void DrawCaption(ImDrawList* drawList, ImVec2 pos, const char* text)
		{
			DrawText(drawList, "BoldTitle", k_CaptionSize, pos, Theme::textCaption, text);
		}

		// Keyboard hint chip ("Ctrl O", "Esc"), right edge at `rightX`, centered on `centerY`.
		// Returns its width.
		float DrawKeyChip(ImDrawList* drawList, float rightX, float centerY, const char* keys, bool onAccent)
		{
			const ImVec2 textSize = MeasureText("Mono", k_CaptionSize, keys);
			const ImVec2 max(rightX, centerY + textSize.y * 0.5f + S(2.0f));
			const ImVec2 min(rightX - textSize.x - S(12.0f), centerY - textSize.y * 0.5f - S(2.0f));
			drawList->AddRect(min, max, onAccent ? WithAlpha(Theme::titlebar, 0.35f) : Theme::muted, S(4.0f));
			DrawText(drawList, "Mono", k_CaptionSize, ImVec2(min.x + S(6.0f), min.y + S(2.0f)), onAccent ? Theme::titlebar : Theme::textCaption, keys);
			return max.x - min.x;
		}

		// Concentric rings + crosshair around a lit point: the banner's "Lux" motif.
		void DrawLightMotif(ImDrawList* drawList, ImVec2 center, float scale)
		{
			const float k = S(scale);
			const float rings[] = { 30.0f, 58.0f, 92.0f };
			const float ringAlpha[] = { 0.45f, 0.22f, 0.10f };
			for (int i = 0; i < 3; i++)
				drawList->AddCircle(center, rings[i] * k, WithAlpha(Theme::accent, ringAlpha[i]), 64, S(1.0f));
			drawList->AddCircle(center, 10.0f * k, Theme::accent, 32, S(2.0f));
			drawList->AddCircleFilled(center, 4.0f * k, Theme::accent, 16);

			const ImU32 ray = WithAlpha(Theme::accent, 0.7f);
			drawList->AddLine(ImVec2(center.x, center.y - 20.0f * k), ImVec2(center.x, center.y - 46.0f * k), ray, S(1.5f));
			drawList->AddLine(ImVec2(center.x, center.y + 20.0f * k), ImVec2(center.x, center.y + 46.0f * k), ray, S(1.5f));
			drawList->AddLine(ImVec2(center.x - 20.0f * k, center.y), ImVec2(center.x - 52.0f * k, center.y), ray, S(1.5f));
			drawList->AddLine(ImVec2(center.x + 20.0f * k, center.y), ImVec2(center.x + 52.0f * k, center.y), ray, S(1.5f));
		}

		enum class ButtonKind { Primary, Secondary };

		// A full-size action button drawn to the design: rounded, 1px border, optional icon and key
		// hint. `size` is in scaled pixels; `id` must be unique in the current ID scope.
		bool ActionButton(const char* id, const char* icon, const char* label, const char* keys, ImVec2 size, ButtonKind kind, bool centered, bool enabled = true)
		{
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max(min.x + size.x, min.y + size.y);

			bool pressed = false;
			{
				ImGuiEx::ScopedDisable disabled(!enabled);
				pressed = ImGui::InvisibleButton(id, size);
			}
			const bool hovered = enabled && ImGui::IsItemHovered();
			const bool primary = kind == ButtonKind::Primary;
			const float alpha = enabled ? 1.0f : 0.4f;

			const ImU32 fill = primary ? (hovered ? Theme::highlight : Theme::accent) : (hovered ? Theme::backgroundHover : Theme::groupHeader);
			const ImU32 border = primary ? fill : (hovered ? Theme::muted : Theme::borderSubtle);
			const ImU32 text = WithAlpha(primary ? Theme::titlebar : Theme::textBrighter, alpha);

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(min, max, WithAlpha(fill, alpha), S(k_ControlRounding));
			drawList->AddRect(min, max, WithAlpha(border, alpha), S(k_ControlRounding));

			const bool hasLabel = label && label[0];
			const ImVec2 iconSize = icon ? MeasureText("Default", k_BodySize, icon) : ImVec2(0.0f, 0.0f);
			const ImVec2 labelSize = hasLabel ? MeasureText("Default", k_BodySize, label) : ImVec2(0.0f, iconSize.y);
			const float gap = icon && hasLabel ? S(12.0f) : 0.0f;
			float x = centered ? min.x + (size.x - (iconSize.x + gap + labelSize.x)) * 0.5f : min.x + S(14.0f);
			const float y = min.y + (size.y - labelSize.y) * 0.5f;

			if (icon)
			{
				DrawText(drawList, "Default", k_BodySize, ImVec2(x, y), text, icon);
				x += iconSize.x + gap;
			}
			if (hasLabel)
				DrawText(drawList, "Default", k_BodySize, ImVec2(x, y), text, label);
			if (keys)
				DrawKeyChip(drawList, max.x - S(14.0f), min.y + size.y * 0.5f, keys, primary);

			return pressed && enabled;
		}

		// Themed single-line input, 44px tall, with the lime focus ring from the design.
		// `width` is in scaled pixels.
		bool Field(const char* id, char* buffer, size_t bufferSize, float width, const char* hint, const char* font, float fontSize, ImGuiInputTextFlags flags = 0)
		{
			ImGuiEx::ScopedFont scopedFont(ImGuiEx::Fonts::Get(font), S(fontSize));
			const float padY = std::max(0.0f, (S(k_FieldHeight) - ImGui::GetFontSize()) * 0.5f);
			ImGuiEx::ScopedStyleStack style(ImGuiStyleVar_FramePadding, ImVec2(S(12.0f), padY), ImGuiStyleVar_FrameRounding, S(k_ControlRounding), ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGuiEx::ScopedColourStack colours(ImGuiCol_FrameBg, Theme::propertyField, ImGuiCol_Border, Theme::borderSubtle, ImGuiCol_Text, Theme::textBrighter);

			ImGui::SetNextItemWidth(width);
			const bool result = ImGui::InputTextWithHint(id, hint, buffer, bufferSize, flags);
			if (ImGui::IsItemActive())
			{
				const ImVec2 min = ImGui::GetItemRectMin();
				const ImVec2 max = ImGui::GetItemRectMax();
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->AddRect(ImVec2(min.x - S(2.0f), min.y - S(2.0f)), ImVec2(max.x + S(2.0f), max.y + S(2.0f)), WithAlpha(Theme::accent, 0.18f), S(k_ControlRounding + 2.0f), 0, S(3.0f));
				drawList->AddRect(min, max, Theme::accent, S(k_ControlRounding));
			}
			return result;
		}

	}

	SplashScreen::SplashScreen(Callbacks callbacks)
		: m_Callbacks(std::move(callbacks))
	{
	}

	void SplashScreen::Open()
	{
		if (m_Open)
			return;

		m_Open = true;
		m_OpenedFrame = ImGui::GetFrameCount();
		m_ShowCreateForm = false;
		m_CreateFailed = false;
	}

	void SplashScreen::Close()
	{
		m_Open = false;
		m_ShowCreateForm = false;
	}

	void SplashScreen::ShowCreateForm()
	{
		Open();
		m_ShowCreateForm = true;
		m_FocusNameInput = true;
		m_CreateFailed = false;

		if (m_NewProjectLocation[0])
			return;

		// Default the location to the folder that holds the most recent project, since that is where
		// people tend to keep their projects; otherwise the editor's working directory.
		std::filesystem::path location = FileSystem::GetWorkingDirectory();
		if (m_Callbacks.GetRecentProjects)
		{
			const std::vector<RecentProject> recentProjects = m_Callbacks.GetRecentProjects();
			if (!recentProjects.empty())
			{
				const std::filesystem::path projectsFolder = std::filesystem::path(recentProjects.front().FilePath).parent_path().parent_path();
				if (!projectsFolder.empty() && FileSystem::IsDirectory(projectsFolder))
					location = projectsFolder;
			}
		}

		CopyToBuffer(m_NewProjectLocation, sizeof(m_NewProjectLocation), location.lexically_normal().generic_string());
	}

	void SplashScreen::OnImGuiRender(float topInset)
	{
		const bool projectOpen = !m_Callbacks.IsProjectOpen || m_Callbacks.IsProjectOpen();
		if (!projectOpen)
			Open();   // nothing to go back to without a project
		if (!m_Open)
			return;

		s_UiScale = ImGuiEx::Fonts::GetScale();

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(S(k_Width), 0.0f), ImVec2(S(k_Width), FLT_MAX));
		if (m_OpenedFrame == ImGui::GetFrameCount())
			ImGui::SetNextWindowFocus();

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;

		// The window paints no background or border of its own: the backdrop and shadow below are drawn
		// into its draw list and would cover them, so the card surface is drawn after them instead.
		// Padding is zero so the card is laid out exactly; it is popped straight after Begin so the
		// recent-project context menu keeps normal popup styling.
		ImGuiEx::ScopedColour windowColour(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
		ImGuiEx::ScopedStyle rounding(ImGuiStyleVar_WindowRounding, S(k_Rounding));
		ImGuiEx::ScopedStyle border(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool visible = ImGui::Begin("##lux_splash", nullptr, flags);
		ImGui::PopStyleVar();

		if (visible)
		{
			// Sampled before any widget runs: a button pressed this frame becomes the active item, and
			// IsWindowHovered would then report false and read the click as one outside the card.
			const bool cardHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 cardMin = ImGui::GetWindowPos();
			const ImVec2 cardMax(cardMin.x + ImGui::GetWindowWidth(), cardMin.y + ImGui::GetWindowHeight());

			// Drawn first into the card's own draw list, so it sits above the editor but below the card:
			// the dim backdrop (only when there is an editor to dim) and a soft shadow.
			drawList->PushClipRectFullScreen();
			if (projectOpen)
				drawList->AddRectFilled(ImVec2(viewport->Pos.x, viewport->Pos.y + topInset), ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), Theme::backdropDim);
			for (int i = 1; i <= 6; i++)
			{
				const float spread = S(static_cast<float>(i) * 4.0f);
				drawList->AddRectFilled(ImVec2(cardMin.x - spread, cardMin.y - spread * 0.4f), ImVec2(cardMax.x + spread, cardMax.y + spread * 1.6f), IM_COL32(0, 0, 0, 16), S(k_Rounding) + spread);
			}
			drawList->PopClipRect();
			drawList->AddRectFilled(cardMin, cardMax, Theme::backgroundPopup, S(k_Rounding));

			{
				m_PopupItemSpacing[0] = ImGui::GetStyle().ItemSpacing.x;
				m_PopupItemSpacing[1] = ImGui::GetStyle().ItemSpacing.y;
				ImGuiEx::ScopedStyle spacing(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
				if (m_ShowCreateForm)
					DrawCreateForm();
				else
					DrawHome(projectOpen);
			}
			drawList->AddRect(cardMin, cardMax, Theme::borderSubtle, S(k_Rounding));

			// Like Blender's splash: Esc or a click anywhere else dismisses it. Esc on the form steps
			// back to the home page first. Neither applies while no project is open.
			if (m_Open && m_OpenedFrame != ImGui::GetFrameCount())
			{
				const bool clickedOutside = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && !cardHovered;

				// While a field is being typed in, Esc belongs to it (it cancels the edit); only the next
				// Esc leaves the form. WantTextInput still reflects last frame, when the field was active.
				const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput;
				if (escape && m_ShowCreateForm)
					m_ShowCreateForm = false;
				else if (projectOpen && (clickedOutside || escape))
					Close();
			}
		}
		ImGui::End();
	}

	void SplashScreen::DrawHome(bool projectOpen)
	{
		const std::vector<RecentProject> recentProjects = m_Callbacks.GetRecentProjects ? m_Callbacks.GetRecentProjects() : std::vector<RecentProject>{};
		const bool firstLaunch = recentProjects.empty();

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		// ---- Banner --------------------------------------------------------------------------
		{
			const ImVec2 bannerMax(origin.x + S(k_Width), origin.y + S(k_BannerHeight));
			drawList->AddRectFilled(origin, bannerMax, Theme::titlebar, S(k_Rounding), ImDrawFlags_RoundCornersTop);
			drawList->PushClipRect(origin, bannerMax, true);

			// A viewport-style ground grid receding to a vanishing point, then the light motif.
			const ImVec2 vanish(origin.x + S(k_Width * 0.5f), origin.y + S(118.0f));
			for (int i = 0; i < 8; i++)
				drawList->AddLine(vanish, ImVec2(origin.x + S(-200.0f + static_cast<float>(i) * 160.0f), bannerMax.y), Theme::groupHeader, S(1.0f));
			for (const float y : { 150.0f, 170.0f, 186.0f, 199.0f, 208.0f })
				drawList->AddLine(ImVec2(origin.x, origin.y + S(y)), ImVec2(bannerMax.x, origin.y + S(y)), Theme::groupHeader, S(1.0f));
			DrawLightMotif(drawList, ImVec2(origin.x + S(k_LightCenter.x), origin.y + S(k_LightCenter.y)), 1.0f);

			// Build line + version pill, top right.
			const ImVec2 versionSize = MeasureText("Mono", k_CaptionSize, LUX_VERSION);
			const ImVec2 pillMin(bannerMax.x - S(20.0f) - versionSize.x - S(16.0f), origin.y + S(18.0f));
			const ImVec2 pillMax(bannerMax.x - S(20.0f), pillMin.y + versionSize.y + S(6.0f));
			drawList->AddRectFilled(pillMin, pillMax, Theme::accent, S(999.0f));
			DrawText(drawList, "Mono", k_CaptionSize, ImVec2(pillMin.x + S(8.0f), pillMin.y + S(3.0f)), Theme::titlebar, LUX_VERSION);
			const std::string build = std::format("{} {} {}", LUX_BUILD_CONFIG_NAME, k_MiddleDot, LUX_BUILD_PLATFORM_NAME);
			const ImVec2 buildSize = MeasureText("Mono", k_CaptionSize, build.c_str());
			DrawText(drawList, "Mono", k_CaptionSize, ImVec2(pillMin.x - S(8.0f) - buildSize.x, pillMin.y + S(3.0f)), Theme::textCaption, build.c_str());

			// Title + subtitle, bottom left.
			const char* title = firstLaunch ? "Welcome to LuxEngine" : "LuxEngine";
			const char* subtitle = firstLaunch ? "Create your first project, or open one you already have." : "Open a project, or start a new one.";
			const ImVec2 subtitleSize = MeasureText("Small", k_SubtitleSize, subtitle);
			const ImVec2 titleSize = MeasureText("Display", k_TitleSize, title);
			const float subtitleY = bannerMax.y - S(k_StripeHeight + 23.0f) - subtitleSize.y;
			const float titleY = subtitleY - S(8.0f) - titleSize.y;
			DrawText(drawList, "Display", k_TitleSize, ImVec2(origin.x + S(32.0f), titleY), Theme::textBrighter, title);
			DrawText(drawList, "Small", k_SubtitleSize, ImVec2(origin.x + S(32.0f), subtitleY), Theme::textCaption, subtitle);

			drawList->PopClipRect();
			drawList->AddRectFilled(ImVec2(origin.x, bannerMax.y - S(k_StripeHeight)), bannerMax, Theme::accent);
			ImGui::Dummy(S(k_Width, k_BannerHeight));
		}

		// ---- Body: Start column + Recent column ------------------------------------------------
		const float bodyTop = S(k_BannerHeight + k_BodyTopPad);
		const float rightColumnX = S(k_PadX + k_LeftColumnWidth + k_ColumnGap);
		const float rightColumnWidth = S(k_Width) - rightColumnX - S(k_PadX);
		float bodyBottom = bodyTop;

		ImGui::SetCursorPos(ImVec2(S(k_PadX), bodyTop));
		ImGui::BeginGroup();
		{
			DrawCaption(drawList, ImGui::GetCursorScreenPos(), "START");
			ImGui::Dummy(S(k_LeftColumnWidth, 24.0f));

			if (ActionButton("##splash_new", LUX_ICON_PLUS, "New Project...", nullptr, S(k_LeftColumnWidth, k_ActionHeight), ButtonKind::Primary, false))
				ShowCreateForm();
			ImGui::Dummy(S(0.0f, 10.0f));
			if (ActionButton("##splash_open", LUX_ICON_FOLDER_OPEN, "Open Project...", "Ctrl O", S(k_LeftColumnWidth, k_ActionHeight), ButtonKind::Secondary, false) && m_Callbacks.BrowseForProject)
			{
				if (m_Callbacks.BrowseForProject())
					Close();
			}

			if (!firstLaunch)
			{
				ImGui::Dummy(S(0.0f, 14.0f));
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				const char* note = "Projects are .luxproj files. Anything you open is added to Recent.";
				const float wrap = S(k_LeftColumnWidth - 4.0f);
				const ImVec2 size = MeasureText("Small", k_SmallSize, note, wrap);
				DrawText(drawList, "Small", k_SmallSize, ImVec2(pos.x + S(2.0f), pos.y), Theme::textCaption, note, wrap);
				ImGui::Dummy(ImVec2(S(k_LeftColumnWidth), size.y));
			}
		}
		ImGui::EndGroup();
		bodyBottom = std::max(bodyBottom, ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y);

		ImGui::SetCursorPos(ImVec2(rightColumnX, bodyTop));
		ImGui::BeginGroup();
		DrawRecentProjects(rightColumnWidth, recentProjects);
		ImGui::EndGroup();
		bodyBottom = std::max(bodyBottom, ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y);

		ImGui::SetCursorPos(ImVec2(0.0f, bodyBottom + S(k_BodyBottomPad)));
		DrawFooter(projectOpen);
	}

	void SplashScreen::DrawRecentProjects(float columnWidth, const std::vector<RecentProject>& recentProjects)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Header: "RECENT" + count.
		{
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			DrawCaption(drawList, ImVec2(pos.x + S(12.0f), pos.y), "RECENT");
			if (!recentProjects.empty())
			{
				const std::string count = std::format("{} project{}", recentProjects.size(), recentProjects.size() == 1 ? "" : "s");
				const ImVec2 size = MeasureText("Small", k_SmallSize, count.c_str());
				DrawText(drawList, "Small", k_SmallSize, ImVec2(pos.x + columnWidth - S(12.0f) - size.x, pos.y - S(1.0f)), Theme::textCaption, count.c_str());
			}
			ImGui::Dummy(ImVec2(columnWidth, S(24.0f)));
		}

		if (recentProjects.empty())
		{
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const float height = S(150.0f);
			drawList->AddRect(min, ImVec2(min.x + columnWidth, min.y + height), Theme::borderSubtle, S(10.0f));

			const float centerX = min.x + columnWidth * 0.5f;
			const ImVec2 iconSize = MeasureText("Default", 22.0f, LUX_ICON_FOLDER_O);
			DrawText(drawList, "Default", 22.0f, ImVec2(centerX - iconSize.x * 0.5f, min.y + S(30.0f)), Theme::textCaption, LUX_ICON_FOLDER_O);
			const char* heading = "No recent projects yet";
			const ImVec2 headingSize = MeasureText("BoldTitle", k_BodySize, heading);
			DrawText(drawList, "BoldTitle", k_BodySize, ImVec2(centerX - headingSize.x * 0.5f, min.y + S(68.0f)), Theme::textBrighter, heading);
			const char* body = "Projects you create or open show up here, newest first.";
			const ImVec2 bodySize = MeasureText("Small", k_NoteSize, body);
			DrawText(drawList, "Small", k_NoteSize, ImVec2(centerX - bodySize.x * 0.5f, min.y + S(94.0f)), Theme::textCaption, body);

			ImGui::Dummy(ImVec2(columnWidth, height));
			return;
		}

		// Opening or removing a project changes the recent list, so both are applied after the loop.
		std::filesystem::path pendingOpen;
		std::filesystem::path pendingRemove;

		const int rowCount = std::min(static_cast<int>(recentProjects.size()), k_MaxRecentRows);
		for (int i = 0; i < rowCount; i++)
		{
			const RecentProject& recentProject = recentProjects[i];
			const std::filesystem::path projectPath(recentProject.FilePath);
			const bool exists = FileSystem::Exists(projectPath);
			const std::string displayName = recentProject.Name.empty() ? projectPath.stem().string() : recentProject.Name;

			ImGuiEx::ScopedID rowID(recentProject.FilePath.c_str());
			if (i > 0)
				ImGui::Dummy(S(0.0f, k_RowGap));

			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max(min.x + columnWidth, min.y + S(k_RowHeight));
			if (ImGui::InvisibleButton("##recent_project", ImVec2(columnWidth, S(k_RowHeight))) && exists)
				pendingOpen = projectPath;
			const bool hovered = ImGui::IsItemHovered();

			if (ImGui::BeginPopupContextItem("##recent_project_context"))
			{
				{
					// Scoped inside the popup so it is popped before EndPopup, in the same window.
					ImGuiEx::ScopedStyle popupSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(m_PopupItemSpacing[0], m_PopupItemSpacing[1]));
					if (ImGui::MenuItem("Open##recent_open", nullptr, false, exists))
						pendingOpen = projectPath;
					if (ImGui::MenuItem("Show in Explorer##recent_show", nullptr, false, exists))
						FileSystem::ShowFileInExplorer(projectPath);
					ImGui::Separator();
					if (ImGui::MenuItem("Remove from List##recent_remove"))
						pendingRemove = projectPath;
				}
				ImGui::EndPopup();
			}

			if (hovered && exists)
				drawList->AddRectFilled(min, max, Theme::backgroundHover, S(k_ControlRounding));

			// Project tile: the mark in accent for the most recent project, muted for the rest.
			const ImVec2 tileMin(min.x + S(12.0f), min.y + S((k_RowHeight - k_TileSize) * 0.5f));
			const ImVec2 tileMax(tileMin.x + S(k_TileSize), tileMin.y + S(k_TileSize));
			if (exists)
			{
				drawList->AddRectFilled(tileMin, tileMax, Theme::titlebar, S(k_ControlRounding));
				const ImVec2 markMin(tileMin.x + S(12.0f), tileMin.y + S(10.0f));
				drawList->AddRectFilled(markMin, ImVec2(markMin.x + S(10.0f), markMin.y + S(14.0f)), i == 0 ? Theme::accent : Theme::textDarker, S(2.0f));
			}
			else
			{
				drawList->AddRect(tileMin, tileMax, Theme::muted, S(k_ControlRounding));
				const ImVec2 iconSize = MeasureText("Default", k_BodySize, LUX_ICON_EXCLAMATION_CIRCLE);
				DrawText(drawList, "Default", k_BodySize, ImVec2(tileMin.x + (S(k_TileSize) - iconSize.x) * 0.5f, tileMin.y + (S(k_TileSize) - iconSize.y) * 0.5f), Theme::textCaption, LUX_ICON_EXCLAMATION_CIRCLE);
			}

			// Status on the right, then name + path in the space that is left.
			const std::string status = exists ? FormatLastOpened(recentProject.LastOpened) : std::string("File not found");
			const ImVec2 statusSize = MeasureText("Small", k_SmallSize, status.c_str());
			DrawText(drawList, "Small", k_SmallSize, ImVec2(max.x - S(12.0f) - statusSize.x, min.y + (S(k_RowHeight) - statusSize.y) * 0.5f), exists ? Theme::textCaption : Theme::textWarning, status.c_str());

			const float textX = tileMax.x + S(12.0f);
			const float textWidth = max.x - S(12.0f) - statusSize.x - S(16.0f) - textX;
			const std::string name = FitText("BoldTitle", k_BodySize, displayName, textWidth);
			const std::string path = FitText("Mono", k_CaptionSize, recentProject.FilePath, textWidth);
			const ImVec2 nameSize = MeasureText("BoldTitle", k_BodySize, name.c_str());
			const ImVec2 pathSize = MeasureText("Mono", k_CaptionSize, path.c_str());
			const float textTop = min.y + (S(k_RowHeight) - nameSize.y - S(3.0f) - pathSize.y) * 0.5f;
			DrawText(drawList, "BoldTitle", k_BodySize, ImVec2(textX, textTop), exists ? Theme::textBrighter : Theme::textCaption, name.c_str());
			DrawText(drawList, "Mono", k_CaptionSize, ImVec2(textX, textTop + nameSize.y + S(3.0f)), Theme::textCaption, path.c_str());
		}

		if (static_cast<int>(recentProjects.size()) > rowCount)
		{
			ImGui::Dummy(S(0.0f, k_RowGap));
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const std::string more = std::format("{} more in File > Recent Projects", recentProjects.size() - rowCount);
			DrawText(drawList, "Small", k_SmallSize, ImVec2(pos.x + S(12.0f), pos.y), Theme::textCaption, more.c_str());
			ImGui::Dummy(ImVec2(columnWidth, S(18.0f)));
		}

		if (!pendingRemove.empty() && m_Callbacks.RemoveRecentProject)
			m_Callbacks.RemoveRecentProject(pendingRemove);
		if (!pendingOpen.empty() && m_Callbacks.OpenProject)
		{
			Close();
			m_Callbacks.OpenProject(pendingOpen);
		}
	}

	void SplashScreen::DrawFooter(bool projectOpen)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 max(min.x + S(k_Width), min.y + S(k_FooterHeight));
		drawList->AddRectFilled(min, max, Theme::backgroundFooter, S(k_Rounding), ImDrawFlags_RoundCornersBottom);
		drawList->AddLine(min, ImVec2(max.x, min.y), Theme::borderSubtle, 1.0f);

		const float localTop = ImGui::GetCursorPosY();
		{
			ImGuiEx::ScopedFont font(ImGuiEx::Fonts::Get("Default"), S(k_NoteSize));
			ImGuiEx::ScopedStyleStack style(ImGuiStyleVar_FramePadding, S(3.0f, 3.0f), ImGuiStyleVar_FrameRounding, S(4.0f), ImGuiStyleVar_FrameBorderSize, 1.0f, ImGuiStyleVar_ItemInnerSpacing, S(8.0f, 0.0f));
			ImGuiEx::ScopedColourStack colours(ImGuiCol_FrameBg, Theme::propertyField, ImGuiCol_Border, Theme::muted, ImGuiCol_CheckMark, Theme::accent, ImGuiCol_Text, Theme::text);
			const float checkboxY = localTop + (S(k_FooterHeight) - ImGui::GetFrameHeight()) * 0.5f;

			ImGui::SetCursorPos(ImVec2(S(k_PadX), checkboxY));
			if (m_Callbacks.GetShowOnStartup && m_Callbacks.SetShowOnStartup)
			{
				bool showOnStartup = m_Callbacks.GetShowOnStartup();
				if (ImGui::Checkbox("Show at startup##splash_show", &showOnStartup))
					m_Callbacks.SetShowOnStartup(showOnStartup);
				ImGui::SameLine(0.0f, S(22.0f));
			}
			if (m_Callbacks.GetReopenLastProject && m_Callbacks.SetReopenLastProject)
			{
				bool reopen = m_Callbacks.GetReopenLastProject();
				if (ImGui::Checkbox("Reopen last project automatically##splash_reopen", &reopen))
					m_Callbacks.SetReopenLastProject(reopen);
			}
		}

		// Right-aligned hint.
		const float centerY = min.y + S(k_FooterHeight) * 0.5f;
		if (projectOpen)
		{
			const char* tail = "to close";
			const ImVec2 tailSize = MeasureText("Small", k_SmallSize, tail);
			float x = max.x - S(k_PadX) - tailSize.x;
			DrawText(drawList, "Small", k_SmallSize, ImVec2(x, centerY - tailSize.y * 0.5f), Theme::textCaption, tail);
			x -= S(6.0f);
			x -= DrawKeyChip(drawList, x, centerY, "Esc", false) + S(6.0f);
			const std::string lead = std::format("Right-click a project for more {}", k_MiddleDot);
			const ImVec2 leadSize = MeasureText("Small", k_SmallSize, lead.c_str());
			DrawText(drawList, "Small", k_SmallSize, ImVec2(x - leadSize.x, centerY - leadSize.y * 0.5f), Theme::textCaption, lead.c_str());
		}
		else
		{
			const char* hint = "Open or create a project to continue";
			const ImVec2 size = MeasureText("Small", k_SmallSize, hint);
			DrawText(drawList, "Small", k_SmallSize, ImVec2(max.x - S(k_PadX) - size.x, centerY - size.y * 0.5f), Theme::textCaption, hint);
		}

		ImGui::SetCursorPos(ImVec2(0.0f, localTop));
		ImGui::Dummy(S(k_Width, k_FooterHeight));
	}

	std::string SplashScreen::ValidateCreate() const
	{
		if (!m_NewProjectName[0])
			return "Enter a project name.";
		if (!IsValidProjectName(m_NewProjectName))
			return "Use letters, digits and underscores, starting with a letter. The name is also the C# namespace.";
		if (!m_NewProjectLocation[0])
			return "Choose a location.";

		const std::filesystem::path location(m_NewProjectLocation);
		if (!FileSystem::IsDirectory(location))
			return "The location folder does not exist.";

		const std::filesystem::path projectDirectory = location / m_NewProjectName;
		if (FileSystem::Exists(projectDirectory))
		{
			std::error_code ec;
			if (!FileSystem::IsDirectory(projectDirectory) || !std::filesystem::is_empty(projectDirectory, ec) || ec)
				return std::format("'{}' already exists in that location and is not an empty folder.", m_NewProjectName);
		}

		return {};
	}

	void SplashScreen::DrawCreateForm()
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float contentWidth = S(k_Width - k_PadX * 2.0f);

		// ---- Header ------------------------------------------------------------------------------
		{
			const ImVec2 bannerMax(origin.x + S(k_Width), origin.y + S(k_FormBannerHeight));
			drawList->AddRectFilled(origin, bannerMax, Theme::titlebar, S(k_Rounding), ImDrawFlags_RoundCornersTop);
			drawList->PushClipRect(origin, bannerMax, true);
			DrawLightMotif(drawList, ImVec2(origin.x + S(k_FormLightCenter.x), origin.y + S(k_FormLightCenter.y)), 0.8f);
			drawList->PopClipRect();
			drawList->AddRectFilled(ImVec2(origin.x, bannerMax.y - S(k_StripeHeight)), bannerMax, Theme::accent);

			const float innerHeight = S(k_FormBannerHeight - k_StripeHeight);
			ImGui::SetCursorPos(ImVec2(S(20.0f), (innerHeight - S(k_FieldHeight)) * 0.5f));
			if (ActionButton("##splash_back", LUX_ICON_CHEVRON_LEFT, nullptr, nullptr, S(k_FieldHeight, k_FieldHeight), ButtonKind::Secondary, true))
				m_ShowCreateForm = false;
			ImGui::SetItemTooltip("Back (Esc)");

			const char* subtitle = "Creates a project folder and opens it. The current project stays open until then.";
			const ImVec2 titleSize = MeasureText("Display", k_FormTitleSize, "New Project");
			const ImVec2 subtitleSize = MeasureText("Small", k_NoteSize, subtitle);
			const float textTop = origin.y + (innerHeight - titleSize.y - S(4.0f) - subtitleSize.y) * 0.5f;
			DrawText(drawList, "Display", k_FormTitleSize, ImVec2(origin.x + S(78.0f), textTop), Theme::textBrighter, "New Project");
			DrawText(drawList, "Small", k_NoteSize, ImVec2(origin.x + S(78.0f), textTop + titleSize.y + S(4.0f)), Theme::textCaption, subtitle);

			ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
			ImGui::Dummy(S(k_Width, k_FormBannerHeight));
		}

		const auto label = [&](const char* text)
		{
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			DrawText(drawList, "BoldTitle", k_NoteSize, pos, Theme::textBrighter, text);
			ImGui::Dummy(ImVec2(contentWidth, MeasureText("BoldTitle", k_NoteSize, text).y + S(8.0f)));
		};

		ImGui::SetCursorPos(ImVec2(S(k_PadX), S(k_FormBannerHeight + k_BodyTopPad)));
		ImGui::BeginGroup();

		// ---- Name --------------------------------------------------------------------------------
		label("Name");
		if (m_FocusNameInput)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusNameInput = false;
		}
		const bool submitted = Field("##new_project_name", m_NewProjectName, sizeof(m_NewProjectName), contentWidth, "MyGame", "Default", k_BodySize, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::Dummy(S(0.0f, 8.0f));
		{
			const char* help = "Letters, digits and underscores, starting with a letter. It also becomes the C# namespace.";
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			DrawText(drawList, "Small", k_SmallSize, pos, Theme::textCaption, help, contentWidth);
			ImGui::Dummy(ImVec2(contentWidth, MeasureText("Small", k_SmallSize, help, contentWidth).y));
		}
		ImGui::Dummy(S(0.0f, 18.0f));

		// ---- Location ----------------------------------------------------------------------------
		label("Location");
		{
			const char* browseLabel = "Browse...";
			const float browseWidth = MeasureText("Default", k_BodySize, LUX_ICON_FOLDER_OPEN).x + S(12.0f) + MeasureText("Default", k_BodySize, browseLabel).x + S(36.0f);
			Field("##new_project_location", m_NewProjectLocation, sizeof(m_NewProjectLocation), contentWidth - browseWidth - S(8.0f), "", "Mono", k_NoteSize);
			ImGui::SameLine(0.0f, S(8.0f));
			if (ActionButton("##new_project_browse", LUX_ICON_FOLDER_OPEN, browseLabel, nullptr, ImVec2(browseWidth, S(k_FieldHeight)), ButtonKind::Secondary, true))
			{
				const std::string folder = FileDialogs::OpenFolder();
				if (!folder.empty())
					CopyToBuffer(m_NewProjectLocation, sizeof(m_NewProjectLocation), std::filesystem::path(folder).lexically_normal().generic_string());
			}
		}
		ImGui::Dummy(S(0.0f, 18.0f));

		// ---- "Will create" preview, or why the project cannot be created -------------------------
		const std::string error = ValidateCreate();
		{
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const float pad = S(16.0f);
			const float innerWidth = contentWidth - pad * 2.0f;
			const float lineHeight = MeasureText("Mono", k_NoteSize, "M").y;
			const float headerHeight = MeasureText("BoldTitle", k_CaptionSize, "W").y;
			const float chipHeight = MeasureText("Mono", k_CaptionSize, "A").y + S(6.0f);
			float height = S(14.0f) + headerHeight + S(10.0f);

			std::string message;
			ImU32 messageColour = Theme::textBrighter;
			std::vector<std::pair<ImVec2, const char*>> chips;   // offsets inside the chip area
			if (error.empty())
			{
				const std::filesystem::path projectFile = std::filesystem::path(m_NewProjectLocation) / m_NewProjectName / (std::string(m_NewProjectName) + ".luxproj");
				message = FitText("Mono", k_NoteSize, projectFile.lexically_normal().generic_string(), innerWidth);
				height += lineHeight + S(10.0f);

				float x = 0.0f, y = 0.0f;
				for (const char* folder : k_CreatedFolders)
				{
					const float chipWidth = MeasureText("Mono", k_CaptionSize, folder).x + S(16.0f);
					if (x > 0.0f && x + chipWidth > innerWidth)
					{
						x = 0.0f;
						y += chipHeight + S(6.0f);
					}
					chips.emplace_back(ImVec2(x, y), folder);
					x += chipWidth + S(6.0f);
				}
				height += y + chipHeight;
			}
			else
			{
				message = error;
				messageColour = m_NewProjectName[0] ? Theme::textWarning : Theme::textCaption;
				height += MeasureText("Default", k_NoteSize, message.c_str(), innerWidth).y;
			}
			if (m_CreateFailed)
				height += S(10.0f) + lineHeight;
			height += S(14.0f);

			const ImVec2 max(min.x + contentWidth, min.y + height);
			drawList->AddRectFilled(min, max, Theme::background, S(10.0f));
			drawList->AddRect(min, max, Theme::borderSubtle, S(10.0f));

			float y = min.y + S(14.0f);
			const char* icon = error.empty() ? LUX_ICON_CHECK : LUX_ICON_EXCLAMATION_TRIANGLE;
			DrawText(drawList, "Default", k_NoteSize, ImVec2(min.x + pad, y), error.empty() ? Theme::accent : messageColour, icon);
			DrawCaption(drawList, ImVec2(min.x + pad + S(22.0f), y + S(1.0f)), error.empty() ? "WILL CREATE" : "CAN'T CREATE YET");
			y += headerHeight + S(10.0f);

			if (error.empty())
			{
				DrawText(drawList, "Mono", k_NoteSize, ImVec2(min.x + pad, y), Theme::textBrighter, message.c_str());
				y += lineHeight + S(10.0f);
				for (const auto& [offset, folder] : chips)
				{
					const ImVec2 chipMin(min.x + pad + offset.x, y + offset.y);
					const ImVec2 chipMax(chipMin.x + MeasureText("Mono", k_CaptionSize, folder).x + S(16.0f), chipMin.y + chipHeight);
					drawList->AddRectFilled(chipMin, chipMax, Theme::backgroundDark, S(6.0f));
					drawList->AddRect(chipMin, chipMax, Theme::borderSubtle, S(6.0f));
					DrawText(drawList, "Mono", k_CaptionSize, ImVec2(chipMin.x + S(8.0f), chipMin.y + S(3.0f)), Theme::text, folder);
				}
				y += chips.back().first.y + chipHeight;
			}
			else
			{
				DrawText(drawList, "Default", k_NoteSize, ImVec2(min.x + pad, y), messageColour, message.c_str(), innerWidth);
				y += MeasureText("Default", k_NoteSize, message.c_str(), innerWidth).y;
			}
			if (m_CreateFailed)
				DrawText(drawList, "Default", k_NoteSize, ImVec2(min.x + pad, y + S(10.0f)), Theme::textError, "The project could not be created. See the Log for details.");

			ImGui::Dummy(ImVec2(contentWidth, height));
		}
		ImGui::Dummy(S(0.0f, 22.0f));

		// ---- Actions ----------------------------------------------------------------------------
		bool create = false;
		{
			const float rowTop = ImGui::GetCursorPosY();
			const ImVec2 rowPos = ImGui::GetCursorScreenPos();
			const char* note = "Scripting turns on once the C# project is built.";
			const ImVec2 noteSize = MeasureText("Small", k_SmallSize, note);
			DrawText(drawList, "Small", k_SmallSize, ImVec2(rowPos.x, rowPos.y + (S(k_FieldHeight) - noteSize.y) * 0.5f), Theme::textCaption, note);

			const float cancelWidth = MeasureText("Default", k_BodySize, "Cancel").x + S(36.0f);
			const float createWidth = MeasureText("Default", k_BodySize, "Create Project").x + S(36.0f);
			ImGui::SetCursorPos(ImVec2(S(k_Width - k_PadX) - createWidth - S(10.0f) - cancelWidth, rowTop));
			if (ActionButton("##new_project_cancel", nullptr, "Cancel", nullptr, ImVec2(cancelWidth, S(k_FieldHeight)), ButtonKind::Secondary, true))
				m_ShowCreateForm = false;
			ImGui::SameLine(0.0f, S(10.0f));
			create = ActionButton("##new_project_create", nullptr, "Create Project", nullptr, ImVec2(createWidth, S(k_FieldHeight)), ButtonKind::Primary, true, error.empty());
		}
		ImGui::EndGroup();
		ImGui::Dummy(S(0.0f, 22.0f));

		if ((create || (submitted && error.empty())) && m_Callbacks.CreateProject)
		{
			m_CreateFailed = !m_Callbacks.CreateProject(m_NewProjectName, std::filesystem::path(m_NewProjectLocation));
			if (!m_CreateFailed)
			{
				m_NewProjectName[0] = 0;
				Close();
			}
		}
	}

}
