#pragma once

#include "ImGuiCore.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetMetadata.h"
//#include "Lux/Editor/AssetEditorPanelInterface.h"
//#include "Lux/Editor/EditorOperation.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiFonts.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Utilities/StringUtils.h"

#include <choc/text/choc_StringUtilities.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

#include <filesystem>
#include <format>
#include <map>

#define UndoDo 0

namespace Lux {

	class FieldStorage;

}

namespace Lux::ImGuiEx {

#define LUX_MESSAGE_BOX_OK_BUTTON BIT(0)
#define LUX_MESSAGE_BOX_CANCEL_BUTTON BIT(1)
#define LUX_MESSAGE_BOX_USER_FUNC BIT(2)
#define LUX_MESSAGE_BOX_AUTO_SIZE BIT(3)

	struct MessageBoxData
	{
		std::string Title = "";
		std::string Body = "";
		uint32_t Flags = 0;
		uint32_t Width = 0;
		uint32_t Height = 0;
		uint32_t MinWidth = 0;
		uint32_t MinHeight = 0;
		uint32_t MaxWidth = -1;
		uint32_t MaxHeight = -1;
		std::function<void()> UserRenderFunction;
		bool ShouldOpen = true;
		bool IsOpen = false;
	};
	static std::unordered_map<std::string, MessageBoxData> s_MessageBoxes;

	static ImFont* FindFont(const std::string& fontName)
	{
		return Fonts::Get(fontName);
	}

	static void PushFontBold()
	{
		ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
	}

	static void PushFontLarge()
	{
		ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
	}

	template<uint32_t flags = 0, typename... Args>
	static void ShowSimpleMessageBox(const char* title, std::format_string<Args...> message, Args&&... args)
	{
		auto& messageBoxData = s_MessageBoxes[title];
		messageBoxData.Title = std::format("{0}##MessageBox{1}", title, s_MessageBoxes.size() + 1);
		messageBoxData.Body = std::format(message, std::forward<Args>(args)...);
		messageBoxData.Flags = flags;
		messageBoxData.Width = 600;
		messageBoxData.Height = 0;
		messageBoxData.ShouldOpen = true;
	}

	static void ShowMessageBox(const char* title, const std::function<void()>& renderFunction, uint32_t width = 600, uint32_t height = 0, uint32_t minWidth = 0, uint32_t minHeight = 0, uint32_t maxWidth = -1, uint32_t maxHeight = -1, uint32_t flags = LUX_MESSAGE_BOX_AUTO_SIZE)
	{
		auto& messageBoxData = s_MessageBoxes[title];
		messageBoxData.Title = std::format("{0}##MessageBox{1}", title, s_MessageBoxes.size() + 1);
		messageBoxData.UserRenderFunction = renderFunction;
		messageBoxData.Flags = LUX_MESSAGE_BOX_USER_FUNC | flags;
		messageBoxData.Width = width;
		messageBoxData.Height = height;
		messageBoxData.MinWidth = minWidth;
		messageBoxData.MinHeight = minHeight;
		messageBoxData.MaxWidth = maxWidth;
		messageBoxData.MaxHeight = maxHeight;
		messageBoxData.ShouldOpen = true;
	}

	static void RenderMessageBoxes()
	{
		for (auto& [key, messageBoxData] : s_MessageBoxes)
		{
			if (messageBoxData.ShouldOpen && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
			{
				ImGui::OpenPopup(messageBoxData.Title.c_str());
				messageBoxData.ShouldOpen = false;
				messageBoxData.IsOpen = true;
			}

			if (!messageBoxData.IsOpen)
				continue;

			if (!ImGui::IsPopupOpen(messageBoxData.Title.c_str()))
			{
				messageBoxData.IsOpen = false;
				continue;
			}

			if (messageBoxData.Width != 0 || messageBoxData.Height != 0)
			{
				ImVec2 center = ImGui::GetMainViewport()->GetCenter();
				ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
				ImGui::SetNextWindowSize(ImVec2{ (float)messageBoxData.Width, (float)messageBoxData.Height }, ImGuiCond_Appearing);
			}

			ImGuiWindowFlags windowFlags = 0;
			if (messageBoxData.Flags & LUX_MESSAGE_BOX_AUTO_SIZE)
			{
				windowFlags |= ImGuiWindowFlags_AlwaysAutoResize;
			}
			else
			{
				ImGui::SetNextWindowSizeConstraints(ImVec2{ (float)messageBoxData.MinWidth, (float)messageBoxData.MinHeight }, ImVec2{ (float)messageBoxData.MaxWidth, (float)messageBoxData.MaxHeight });
			}

			if (ImGui::BeginPopupModal(messageBoxData.Title.c_str(), &messageBoxData.IsOpen, windowFlags))
			{
				if (messageBoxData.Flags & LUX_MESSAGE_BOX_USER_FUNC)
				{
					LUX_CORE_VERIFY(messageBoxData.UserRenderFunction, "No render function provided for message box!");
					messageBoxData.UserRenderFunction();
				}
				else
				{
					ImGui::TextWrapped(messageBoxData.Body.c_str());

					if (messageBoxData.Flags & LUX_MESSAGE_BOX_OK_BUTTON)
					{
						if (ImGui::Button("Ok"))
							ImGui::CloseCurrentPopup();

						if (messageBoxData.Flags & LUX_MESSAGE_BOX_CANCEL_BUTTON)
							ImGui::SameLine();
					}

					if (messageBoxData.Flags & LUX_MESSAGE_BOX_CANCEL_BUTTON && ImGui::Button("Cancel"))
					{
						ImGui::CloseCurrentPopup();
					}
				}

				ImGui::EndPopup();
			}
		}
	}

	static bool PropertyGridHeader(const std::string& name, bool openByDefault = true)
	{
		ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_Framed
			| ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_AllowOverlap
			| ImGuiTreeNodeFlags_FramePadding;

		if (openByDefault)
			treeNodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

		bool open = false;
		const float framePaddingX = 6.0f;
		const float framePaddingY = 6.0f; // affects height of the header

		ImGuiEx::ScopedStyle headerRounding(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGuiEx::ScopedStyle headerPaddingAndHeight(ImGuiStyleVar_FramePadding, ImVec2{ framePaddingX, framePaddingY });

		//UI::PushID();
		ImGui::PushID(name.c_str());
		open = ImGui::TreeNodeEx("##dummy_id", treeNodeFlags, Utils::String::ToUpperCopy(name).c_str());
		//UI::PopID();
		ImGui::PopID();

		return open;
	}

	static bool TreeNodeWithIcon(const std::string& label, const Ref<Texture2D>& icon, const ImVec2& size, bool openByDefault = true)
	{
		ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_Framed
			| ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_AllowOverlap
			| ImGuiTreeNodeFlags_FramePadding;

		if (openByDefault)
			treeNodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

		bool open = false;
		const float framePaddingX = 6.0f;
		const float framePaddingY = 6.0f; // affects height of the header

		ImGuiEx::ScopedStyle headerRounding(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGuiEx::ScopedStyle headerPaddingAndHeight(ImGuiStyleVar_FramePadding, ImVec2{ framePaddingX, framePaddingY });

		ImGui::PushID(label.c_str());
		open = ImGui::TreeNodeEx("##dummy_id", treeNodeFlags, "");

		const ImRect headerRect = ImGuiEx::GetItemRect();
		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;

		const float lineHeight = headerRect.GetHeight();
		const float textHeight = ImGui::GetTextLineHeight();

		const float arrowWidth = g.FontSize; // ImGui's triangle width
		const float contentStartX = headerRect.Min.x + style.FramePadding.x + arrowWidth + style.ItemInnerSpacing.x;

		// Icon
		const float iconY = headerRect.Min.y + (lineHeight - size.y) * 0.5f;
		ImGui::GetWindowDrawList()->AddImage(
			ImGuiEx::GetTextureID(icon),
			ImVec2(contentStartX, iconY),
			ImVec2(contentStartX + size.x, iconY + size.y),
			ImVec2(0, 0), ImVec2(1, 1),
			ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));

		// Text
		const float textX = contentStartX + size.x + style.ItemInnerSpacing.x;
		const float textY = headerRect.Min.y + (lineHeight - textHeight) * 0.5f;
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(textX, textY),
			ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Text)),
			Utils::String::ToUpperCopy(label).c_str());

		ImGui::PopID();

		return open;
	}

	static void Separator()
	{
		ImGui::Separator();
	}

	static bool PropertyButton(const char* label, const char* buttonText, const char* helpText = "")
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		modified = ImGui::Button(std::format("{0}##{1}", buttonText, label).c_str());

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, std::string& value, const char* helpText = "", bool doPushUndo = true)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		std::optional<std::string> hold;
		if (doPushUndo)
			hold = value;

		modified = ImGuiEx::InputText(std::format("##{0}", label).c_str(), &value);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<std::string>(&value, *hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyMultiline(const char* label, std::string& value, const char* helpText = "", bool doPushUndo = true)
	{
		bool modified = false;

		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ImGui::PushItemWidth(-1);

		std::optional<std::string> hold;
		if (doPushUndo)
			hold = value;

		modified = ImGuiEx::InputTextMultiline(std::format("##{0}", label).c_str(), &value);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<std::string>(&value, *hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();

		return modified;
	}

	static void Property(const char* label, const std::string& value, const char* helpText = "")
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);
		BeginDisabled();
		ImGuiEx::InputText(std::format("##{0}", label).c_str(), (char*)value.c_str(), value.size(), ImGuiInputTextFlags_ReadOnly);
		EndDisabled();

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();
	}

	static void Property(const char* label, const char* value, const char* helpText = "")
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);
		BeginDisabled();
		ImGuiEx::InputText(std::format("##{0}", label).c_str(), (char*)value, 256, ImGuiInputTextFlags_ReadOnly);
		EndDisabled();

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();
	}

	static bool Property(const char* label, char* value, size_t length, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		/* `TODO`: Needs `EditorStack::Get().PushRaw(&value, *hold);`. */
		bool modified = ImGuiEx::InputText(std::format("##{0}", label).c_str(), value, length);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	// A lime switch toggle: a rounded track + knob, accent when on. Fits the property-grid control
	// column and returns true when toggled. Replaces the plain checkbox for boolean properties.
	static bool ToggleSwitch(const char* strId, bool& value)
	{
		const float height = ImGui::GetFrameHeight() * 0.72f;
		const float width = height * 1.8f;
		const float radius = height * 0.5f;
		const ImVec2 p = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton(strId, ImVec2(width, height));
		bool changed = false;
		if (ImGui::IsItemClicked())
		{
			value = !value;
			changed = true;
		}
		const bool hovered = ImGui::IsItemHovered();

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 trackOn  = Colors::Theme::accent;
		const ImU32 trackOff = Colors::Theme::backgroundDark;
		const ImU32 knobOn   = Colors::Theme::titlebar;      // dark knob reads on the lime track
		const ImU32 knobOff  = hovered ? Colors::Theme::text : Colors::Theme::textDarker;
		dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), value ? trackOn : trackOff, radius);
		const float knobX = value ? (p.x + width - radius) : (p.x + radius);
		dl->AddCircleFilled(ImVec2(knobX, p.y + radius), radius - 2.0f, value ? knobOn : knobOff);
		return changed;
	}

	static bool Property(const char* label, bool& value, const char* helpText = "", bool doPushUndo = true)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		modified = ImGuiEx::ToggleSwitch(std::format("##{0}", label).c_str(), value);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<bool>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, int8_t& value, int8_t min = 0, int8_t max = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragInt8(std::format("##{0}", label).c_str(), &value, 1.0f, min, max);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, int16_t& value, int16_t min = 0, int16_t max = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragInt16(std::format("##{0}", label).c_str(), &value, 1.0f, min, max);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, int32_t& value, int32_t min = 0, int32_t max = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragInt32(std::format("##{0}", label).c_str(), &value, 1.0f, min, max);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, int64_t& value, int64_t min = 0, int64_t max = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragInt64(std::format("##{0}", label).c_str(), &value, 1.0f, min, max);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, uint8_t& value, uint8_t minValue = 0, uint8_t maxValue = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragUInt8(std::format("##{0}", label).c_str(), &value, 1.0f, minValue, maxValue);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, uint16_t& value, uint16_t minValue = 0, uint16_t maxValue = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragUInt16(std::format("##{0}", label).c_str(), &value, 1.0f, minValue, maxValue);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, uint32_t& value, uint32_t minValue = 0, uint32_t maxValue = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragUInt32(std::format("##{0}", label).c_str(), &value, 1.0f, minValue, maxValue);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, uint64_t& value, uint64_t minValue = 0, uint64_t maxValue = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragUInt64(std::format("##{0}", label).c_str(), &value, 1.0f, minValue, maxValue);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyRadio(const char* label, int& chosen, const std::map<int, const std::string_view>& options, const char* helpText = "", const std::map<int, const std::string_view>& optionHelpTexts = {}, bool doPushUndo = true)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ImGui::PushItemWidth(-1);

		auto hold = chosen;

		for (auto [value, option] : options)
		{
			std::string radioLabel = std::format("{}##{}", option.data(), label);
			if (ImGui::RadioButton(radioLabel.c_str(), &chosen, value))
				modified = true;

			if (auto optionHelpText = optionHelpTexts.find(value); optionHelpText != optionHelpTexts.end())
			{
				if (std::strlen(optionHelpText->second.data()) != 0)
				{
					ImGui::SameLine();
					HelpMarker(optionHelpText->second.data());
				}
			}
		}

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&chosen, hold);
#endif
		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, float& value, float delta = 0.1f, float min = 0.0f, float max = 0.0f, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragFloat(std::format("##{0}", label).c_str(), &value, delta, min, max);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, double& value, float delta = 0.1f, double min = 0.0, double max = 0.0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragDouble(std::format("##{0}", label).c_str(), &value, delta, min, max);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, glm::vec2& value, float delta = 0.1f, float min = 0.0f, float max = 0.0f, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragFloat2(std::format("##{0}", label).c_str(), glm::value_ptr(value), delta, min, max);
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, glm::vec3& value, float delta = 0.1f, float min = 0.0f, float max = 0.0f, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		//bool modified = UI::DragFloat3(std::format("##{0}", label).c_str(), glm::value_ptr(value), delta, min, max);
		ImVec2 size(ImGui::GetContentRegionAvail().x - 8.0f, ImGui::GetFrameHeightWithSpacing());
		bool manuallyEdited = false;
		bool modified = ImGuiEx::Widgets::EditVec3(std::format("##{0}", label).c_str(), size, 0.0f, manuallyEdited, value, ImGuiEx::VectorAxis::None, delta, glm::vec3{ min }, glm::vec3{ max });

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, glm::vec4& value, float delta = 0.1f, float min = 0.0f, float max = 0.0f, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::DragFloat4(std::format("##{0}", label).c_str(), glm::value_ptr(value), delta, min, max);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool Property(const char* label, glm::bvec3& value, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(" X");
		ImGui::SameLine();
		bool modified = ImGuiEx::Checkbox(std::format("##1{0}", label).c_str(), &value.x);
		ImGui::SameLine();
		ImGui::TextUnformatted(" Y");
		ImGui::SameLine();
		modified |= ImGuiEx::Checkbox(std::format("##2{0}", label).c_str(), &value.y);
		ImGui::SameLine();
		ImGui::TextUnformatted(" Z");
		ImGui::SameLine();
		modified |= ImGuiEx::Checkbox(std::format("##3{0}", label).c_str(), &value.z);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		return modified;
	}

	static bool PropertySlider(const char* label, int& value, int min, int max, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::SliderInt32(GenerateID(), &value, min, max);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertySlider(const char* label, float& value, float min, float max, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::SliderFloat(GenerateID(), &value, min, max);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertySlider(const char* label, glm::vec2& value, float min, float max, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::SliderFloat2(GenerateID(), glm::value_ptr(value), min, max);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertySlider(const char* label, glm::vec3& value, float min, float max, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::SliderFloat3(GenerateID(), glm::value_ptr(value), min, max);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertySlider(const char* label, glm::vec4& value, float min, float max, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::SliderFloat4(GenerateID(), glm::value_ptr(value), min, max);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyInput(const char* label, int8_t& value, int8_t step = 1, int8_t stepFast = 1, ImGuiInputTextFlags flags = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::InputInt8(GenerateID(), &value, step, stepFast, flags);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyInput(const char* label, int16_t& value, int16_t step = 1, int16_t stepFast = 1, ImGuiInputTextFlags flags = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::InputInt16(GenerateID(), &value, step, stepFast, flags);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyInput(const char* label, int32_t& value, int32_t step = 1, int32_t stepFast = 1, ImGuiInputTextFlags flags = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::InputInt32(GenerateID(), &value, step, stepFast, flags);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyInput(const char* label, int64_t& value, int64_t step = 1, int64_t stepFast = 1, ImGuiInputTextFlags flags = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::InputInt64(GenerateID(), &value, step, stepFast, flags);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyInput(const char* label, uint8_t& value, uint8_t step = 1, uint8_t stepFast = 1, ImGuiInputTextFlags flags = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::InputUInt8(GenerateID(), &value, step, stepFast, flags);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyInput(const char* label, uint16_t& value, uint16_t step = 1, uint16_t stepFast = 1, ImGuiInputTextFlags flags = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::InputUInt16(GenerateID(), &value, step, stepFast, flags);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyInput(const char* label, uint32_t& value, uint32_t step = 1, uint32_t stepFast = 1, ImGuiInputTextFlags flags = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::InputUInt32(GenerateID(), &value, step, stepFast, flags);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyInput(const char* label, uint64_t& value, uint64_t step = 0, uint64_t stepFast = 0, ImGuiInputTextFlags flags = 0, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::InputUInt64(GenerateID(), &value, step, stepFast, flags);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyColor(const char* label, glm::vec3& value, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::ColorEdit3(GenerateID(), glm::value_ptr(value));

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyColor(const char* label, glm::vec4& value, const char* helpText = "", bool doPushUndo = true)
	{
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = value;

		bool modified = ImGuiEx::ColorEdit4(GenerateID(), glm::value_ptr(value));

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	template<typename TEnum, typename TUnderlying = int32_t>
	static bool PropertyDropdown(const char* label, const char** options, int32_t optionCount, TEnum& selected, const char* helpText = "", bool doPushUndo = true)
	{
		TUnderlying selectedIndex = (TUnderlying)selected;

		const char* current = options[selectedIndex];
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		bool modified = false;
		auto hold = selected;

		if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
			current = "---";

		const std::string id = "##" + std::string(label);
		if (ImGuiEx::BeginCombo(id.c_str(), current))
		{
			for (int i = 0; i < optionCount; i++)
			{
				const bool is_selected = (current == options[i]);
				if (ImGui::Selectable(options[i], is_selected))
				{
					current = options[i];
					selected = (TEnum)i;
					modified = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGuiEx::EndCombo();
		}
#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&selected, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyDropdown(const char* label, const char** options, int32_t optionCount, int32_t* selected, const char* helpText = "", bool doPushUndo = true)
	{
		const char* current = options[*selected];
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		bool modified = false;
		auto hold = *selected;

		if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
			current = "---";

		const std::string id = "##" + std::string(label);
		if (ImGuiEx::BeginCombo(id.c_str(), current))
		{
			for (int i = 0; i < optionCount; i++)
			{
				const bool is_selected = (current == options[i]);
				if (ImGui::Selectable(options[i], is_selected))
				{
					current = options[i];
					*selected = i;
					modified = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGuiEx::EndCombo();
		}

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(selected, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyDropdownNoLabel(const char* strID, const char** options, int32_t optionCount, int32_t* selected, bool doPushUndo = true)
	{
		const char* current = options[*selected];
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		bool modified = false;
		auto hold = *selected;
		if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
			current = "---";

		const std::string id = "##" + std::string(strID);
		if (ImGuiEx::BeginCombo(id.c_str(), current))
		{
			for (int i = 0; i < optionCount; i++)
			{
				const bool is_selected = (current == options[i]);
				if (ImGui::Selectable(options[i], is_selected))
				{
					current = options[i];
					*selected = i;
					modified = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGuiEx::EndCombo();
		}

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(selected, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	static bool PropertyDropdownNoLabel(const char* strID, const std::vector<std::string>& options, int32_t* selected, bool advanceColumn = true, bool fullWidth = true, bool doPushUndo = true)
	{
		const char* current = options[*selected].c_str();
		ShiftCursorY(4.0f);

		if (fullWidth)
			ImGui::PushItemWidth(-1);

		bool modified = false;
		auto hold = *selected;

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
			current = "---";

		const std::string id = "##" + std::string(strID);
		if (ImGuiEx::BeginCombo(id.c_str(), current))
		{
			for (int i = 0; i < options.size(); i++)
			{
				const bool is_selected = (current == options[i]);
				if (ImGui::Selectable(options[i].c_str(), is_selected))
				{
					current = options[i].c_str();
					*selected = i;
					modified = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGuiEx::EndCombo();
		}

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (fullWidth)
			ImGui::PopItemWidth();

		if (advanceColumn)
		{
			ImGui::NextColumn();
			Draw::Underline();
		}

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(selected, hold);
#endif

		return modified;
	}

	static bool PropertyDropdown(const char* label, const std::vector<std::string>& options, int32_t optionCount, int32_t* selected, const char* helpText = "", bool doPushUndo = true)
	{
		const char* current = options[*selected].c_str();

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		bool modified = false;
		auto hold = *selected;

		if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
			current = "---";

		const std::string id = "##" + std::string(label);
		if (ImGuiEx::BeginCombo(id.c_str(), current))
		{
			for (int i = 0; i < optionCount; i++)
			{
				const bool is_selected = (current == options[i]);
				if (ImGui::Selectable(options[i].c_str(), is_selected))
				{
					current = options[i].c_str();
					*selected = i;
					modified = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGuiEx::EndCombo();
		}

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(selected, hold);
#endif

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	template<class TGetElementFunc>
	static bool PropertyDropdown(const char* label, int32_t optionCount, int32_t* selected, TGetElementFunc&& getElementCb, const char* helpText = "", std::string_view filter = {}) requires std::is_same_v<std::invoke_result_t<TGetElementFunc, uint32_t>, const char*>
	{
		const char* current = getElementCb(*selected);

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		bool modified = false;

		const std::string id = "##" + std::string(label);
		if (ImGui::BeginCombo(id.c_str(), current))
		{
			for (int i = 0; i < optionCount; i++)
			{
				std::string element(getElementCb(i));

				if (IsMatchingSearch(element, filter))
				{
					const bool isSelected = (std::string(current) == element);
					if (ImGui::Selectable(element.c_str(), isSelected))
					{
						//current = element.c_str();
						*selected = i;
						modified = true;
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (!IsItemDisabled())
			DrawItemActivityOutline();

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return modified;
	}

	template<class TGetElementFunc>
	static bool PropertyItemList(const char* label, int32_t itemCount, int32_t& selected, TGetElementFunc&& getItemNameCallback, const char* helpText = "") requires std::is_same_v<std::invoke_result_t<TGetElementFunc, uint32_t>, const char*>
	{
		bool modified = false;

		ImGuiEx::ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		ImGui::NextColumn();
		ImGuiEx::ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			const float width = ImGui::GetContentRegionAvail().x;// -settings.WidthOffset;
			const float itemHeight = 28.0f;

			std::string buttonText = getItemNameCallback(selected);
			const bool valid = selected >= 0;

			if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
				buttonText = "---";

			// PropertyAssetReference could be called multiple times in same "context"
			// and so we need a unique id for the asset search popup each time.
			// notes
			// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
			// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
			//   buffer which may get overwritten by the time you want to use it later on.
			const std::string itemSearchPopupID = ImGuiEx::GenerateLabelID("ILSP");
			{
				ImGuiEx::ScopedColour buttonLabelColor(ImGuiCol_Text, valid ? ImGui::ColorConvertU32ToFloat4(Colors::Theme::text) : ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError));
				ImGui::Button(ImGuiEx::GenerateLabelID(buttonText), { width, itemHeight });

				if (ImGui::IsItemHovered())
				{
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						ImGui::OpenPopup(itemSearchPopupID.c_str());
				}
			}

			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

			bool cleared = false;
			if (ImGuiEx::Widgets::ItemSearchPopup(itemSearchPopupID.c_str(), selected, itemCount, getItemNameCallback, &cleared))
			{
				if (cleared)
					selected = -1;

				modified = true;
			}
		}

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		ImGuiEx::Draw::Underline();

		return modified;
	}

	enum class PropertyAssetReferenceError
	{
		None = 0, InvalidMetadata
	};

	// TODO(Yan): move this somewhere better when restructuring API
	static AssetHandle s_PropertyAssetReferenceAssetHandle;

	struct PropertyAssetReferenceSettings
	{
		bool AdvanceToNextColumn = true;
		bool NoItemSpacing = false; // After label
		float WidthOffset = 0.0f;
		ImVec4 ButtonLabelColor = ImGui::ColorConvertU32ToFloat4(Colors::Theme::text);
		ImVec4 ButtonLabelColorError = ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError);
		bool ShowFullFilePath = false;
	};
	inline auto AssetValidityAndName(AssetHandle handle, const PropertyAssetReferenceSettings& settings)
	{
		if (handle == 0)
			return std::make_pair(true, std::string("None"));

		std::string name = "Invalid";

		Ref<AssetManagerBase> assetManagerBase = Project::GetAssetManager();
		if (!assetManagerBase)
			return std::make_pair(false, name);

		bool valid = assetManagerBase->IsAssetHandleValid(handle);
		if (valid)
		{
			if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			{
				const AssetMetadata metadata = editorAssetManager->GetMetadata(handle);
				if (metadata.IsValid() && !metadata.FilePath.empty())
				{
					if (settings.ShowFullFilePath)
						name = metadata.FilePath.string();
					else
						name = metadata.FilePath.stem().string();
				}
			}

			if (name == "Invalid")
				name = std::to_string((uint64_t)handle);

			if (assetManagerBase->IsAssetMissing(handle))
			{
				valid = false;
				name += " (Missing)";
			}
		}

		return std::make_pair(valid, name);
	}
#if 1
	template<typename T>
	static bool PropertyAssetReference(const char* label, AssetHandle& outHandle, const char* helpText = "", PropertyAssetReferenceError* outError = nullptr, const PropertyAssetReferenceSettings& settings = {}, bool doPushUndo = true)
	{
		bool modified = false;
		if (outError)
			*outError = PropertyAssetReferenceError::None;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		auto hold = outHandle;

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float width = ImGui::GetContentRegionAvail().x - settings.WidthOffset;
			float itemHeight = 28.0f;

			auto [valid, buttonText] = AssetValidityAndName(outHandle, settings);

			if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
				buttonText = "---";

			// PropertyAssetReference could be called multiple times in same "context"
			// and so we need a unique id for the asset search popup each time.
			// notes
			// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
			// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
			//   buffer which may get overwritten by the time you want to use it later on.
			std::string assetSearchPopupID = GenerateLabelID("ARSP");
			{
				ImGuiEx::ScopedColour buttonLabelColor(ImGuiCol_Text, valid ? settings.ButtonLabelColor : settings.ButtonLabelColorError);
				ImGui::Button(GenerateLabelID(buttonText), { width, itemHeight });

				const bool isHovered = ImGui::IsItemHovered();

				if (isHovered)
				{
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						ImGui::OpenPopup(assetSearchPopupID.c_str());
					}
				}
			}

			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

			bool clear = false;
			if (Widgets::AssetSearchPopup(assetSearchPopupID.c_str(), T::GetStaticType(), outHandle, &clear))
			{
				if (clear)
					outHandle = 0;
				modified = true;
				s_PropertyAssetReferenceAssetHandle = outHandle;
			}
		}

		if (!IsItemDisabled())
		{
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* data = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM");

				if (data && data->DataSize >= (int)sizeof(AssetHandle))
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					s_PropertyAssetReferenceAssetHandle = assetHandle;
					Ref<AssetManagerBase> assetManager = Project::GetAssetManager();
					if (assetManager && assetManager->GetAssetType(assetHandle) == T::GetStaticType())
					{
						outHandle = assetHandle;
						modified = true;
					}
				}

				ImGui::EndDragDropTarget();
			}
		}

		ImGui::PopItemWidth();
		if (settings.AdvanceToNextColumn)
		{
			ImGui::NextColumn();
			Draw::Underline();
		}

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&outHandle, hold);
#endif

		return modified;
	}
#endif

	// Script-class picker: shows the current class as a button that opens a searchable list of
	// all discovered Entity-derived script classes (ScriptEngine::GetAllScripts). Updates both
	// the scriptID and the class name. Returns true if changed.
	bool PropertyScriptReference(const char* label, UUID& outScriptID, std::string& outClassName, const PropertyAssetReferenceSettings& settings = {});
#if 0
	template<AssetType... TAssetTypes>
	static bool PropertyMultiAssetReference(const char* label, AssetHandle& outHandle, const char* helpText = "", PropertyAssetReferenceError* outError = nullptr, const PropertyAssetReferenceSettings& settings = PropertyAssetReferenceSettings(), bool doPushUndo = true)
	{
		bool modified = false;
		if (outError)
			*outError = PropertyAssetReferenceError::None;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		auto hold = outHandle;

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		std::initializer_list<AssetType> assetTypes = { TAssetTypes... };

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float width = ImGui::GetContentRegionAvail().x - settings.WidthOffset;
			float itemHeight = 28.0f;

			auto [valid, buttonText] = AssetValidityAndName(outHandle, settings);

			if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
				buttonText = "---";

			// PropertyAssetReference could be called multiple times in same "context"
			// and so we need a unique id for the asset search popup each time.
			// notes
			// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
			// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
			//   buffer which may get overwritten by the time you want to use it later on.
			std::string assetSearchPopupID = GenerateLabelID("ARSP");

			ImGui::PushStyleColor(ImGuiCol_Text, settings.ButtonLabelColor);
			ImGui::Button(GenerateLabelID(buttonText), { width, itemHeight });

			const bool isHovered = ImGui::IsItemHovered();

			if (isHovered)
			{
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					// NOTE(Peter): Ugly workaround since AssetEditorPanel includes ImGui.h (meaning ImGui.h can't include AssetEditorPanel).
					//				Will rework those includes at a later date...
					AssetEditorPanelInterface::OpenEditor(AssetManager::GetAsset<Asset>(outHandle));
				}
				else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					ImGui::OpenPopup(assetSearchPopupID.c_str());
				}
			}
			ImGui::PopStyleColor();
			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

			bool clear = false;
			if (Widgets::AssetSearchPopup(assetSearchPopupID.c_str(), outHandle, &clear, "Search Assets", ImVec2{ 250.0f, 350.0f }, assetTypes))
			{
				if (clear)
					outHandle = 0;
				modified = true;
				s_PropertyAssetReferenceAssetHandle = outHandle;
			}
		}

		if (ImGui::BeginDragDropTarget())
		{
			auto data = ImGui::AcceptDragDropPayload("asset_payload");

			if (data)
			{
				AssetHandle assetHandle = *(AssetHandle*)data->Data;
				s_PropertyAssetReferenceAssetHandle = assetHandle;
				const auto& metadata = Project::GetEditorAssetManager()->GetMetadata(assetHandle);

				for (AssetType type : assetTypes)
				{
					if (metadata.Type == type)
					{
						outHandle = assetHandle;
						modified = true;
						break;
					}
				}
			}

			ImGui::EndDragDropTarget();
		}

		ImGui::PopItemWidth();
		if (settings.AdvanceToNextColumn)
		{
			ImGui::NextColumn();
			Draw::Underline();
		}

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&outHandle, hold);
#endif

		return modified;
	}
#endif

#if 0
	/* TODO: Add undo. */
	template<typename TAssetType, typename TConversionType, typename Fn>
	static bool PropertyAssetReferenceWithConversion(const char* label, AssetHandle& outHandle, Fn&& conversionFunc, const char* helpText = "", PropertyAssetReferenceError* outError = nullptr, const PropertyAssetReferenceSettings& settings = {}, bool doPushUndo = true)
	{
		bool succeeded = false;
		if (outError)
			*outError = PropertyAssetReferenceError::None;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
		float width = ImGui::GetContentRegionAvail().x - settings.WidthOffset;
		ImGuiEx::PushID();

		float itemHeight = 28.0f;

		auto [valid, buttonText] = AssetValidityAndName(outHandle, settings);

		if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
			buttonText = "---";

		// PropertyAssetReferenceWithConversion could be called multiple times in same "context"
		// and so we need a unique id for the asset search popup each time.
		// notes
		// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
		// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
		//   buffer which may get overwritten by the time you want to use it later on.
		std::string assetSearchPopupID = GenerateLabelID("ARWCSP");
		{
			ImGuiEx::ScopedColour buttonLabelColor(ImGuiCol_Text, valid ? settings.ButtonLabelColor : settings.ButtonLabelColorError);
			ImGui::Button(GenerateLabelID(buttonText), { width, itemHeight });

			const bool isHovered = ImGui::IsItemHovered();

			if (isHovered)
			{
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					// NOTE(Peter): Ugly workaround since AssetEditorPanel includes ImGui.h (meaning ImGui.h can't include AssetEditorPanel).
					//				Will rework those includes at a later date...
					AssetEditorPanelInterface::OpenEditor(AssetManager::GetAsset<Asset>(outHandle));
				}
				else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					ImGui::OpenPopup(assetSearchPopupID.c_str());
				}
			}
		}

		ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

		AssetHandle assetHandle = outHandle;

		bool clear = false;
		if (Widgets::AssetSearchPopup(assetSearchPopupID.c_str(), assetHandle, &clear, "Search Assets", ImVec2(250.0f, 350.0f), { TConversionType::GetStaticType(), TAssetType::GetStaticType() }))
		{
			if (clear)
			{
				outHandle = 0;
				assetHandle = 0;
				succeeded = true;
			}
		}
		ImGuiEx::PopID();

		if (!IsItemDisabled())
		{
			if (ImGui::BeginDragDropTarget())
			{
				auto data = ImGui::AcceptDragDropPayload("asset_payload");

				if (data)
					assetHandle = *(AssetHandle*)data->Data;

				ImGui::EndDragDropTarget();
			}

			if (assetHandle != outHandle && assetHandle != 0)
			{
				s_PropertyAssetReferenceAssetHandle = assetHandle;

				Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
				if (asset)
				{
					// No conversion necessary
					if (asset->GetAssetType() == TAssetType::GetStaticType())
					{
						outHandle = assetHandle;
						succeeded = true;
					}
					// Convert
					else if (asset->GetAssetType() == TConversionType::GetStaticType())
					{
						conversionFunc(asset.As<TConversionType>());
						succeeded = false; // Must be handled by conversion function
					}
				}
				else
				{
					if (outError)
						*outError = PropertyAssetReferenceError::InvalidMetadata;
				}
			}
		}

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();

		return succeeded;
	}
#endif
	/*
	static bool PropertyEntityReference(const char* label, UUID& entityID, Ref<Scene> currentScene, const char* helpText = "", bool doPushUndo = true)
	{
		bool receivedValidEntity = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		auto hold = entityID;

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float width = ImGui::GetContentRegionAvail().x;
			float itemHeight = 28.0f;

			std::string buttonText = "Null";

			Entity entity = currentScene->GetEntityByUUID(entityID);
			if (entity)
				buttonText = entity.GetComponent<TagComponent>().Tag;

			if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
				buttonText = "---";

			// PropertyEntityReference could be called multiple times in same "context"
			// and so we need a unique id for the asset search popup each time.
			// notes
			// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
			// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
			//   buffer which may get overwritten by the time you want to use it later on.
			std::string assetSearchPopupID = GenerateLabelID("ARSP");
			{
				ImGuiEx::ScopedColour buttonLabelColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::text));
				ImGui::Button(GenerateLabelID(buttonText), { width, itemHeight });

				const bool isHovered = ImGui::IsItemHovered();
				if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					ImGui::OpenPopup(assetSearchPopupID.c_str());
			}

			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

			bool clear = false;
			if (Widgets::EntitySearchPopup(assetSearchPopupID.c_str(), currentScene, entityID, &clear))
			{
				if (clear)
					entityID = 0;
				receivedValidEntity = true;
			}
		}

		if (!IsItemDisabled())
		{
			if (ImGui::BeginDragDropTarget())
			{
				auto data = ImGui::AcceptDragDropPayload("scene_entity_hierarchy");
				if (data)
				{
					entityID = *(UUID*)data->Data;
					receivedValidEntity = true;
				}

				ImGui::EndDragDropTarget();
			}
		}

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		Draw::Underline();
		
#if UndoDo
		if (receivedValidEntity && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&entityID, hold);
#endif

		return receivedValidEntity;
	}
	*/
#if 0
	/// <summary>
	/// Same as PropertyEntityReference, except you can pass in components that the entity is required to have.
	/// </summary>
	/// <typeparam name="...TComponents"></typeparam>
	/// <param name="label"></param>
	/// <param name="entity"></param>
	/// <param name="requiresAllComponents">If true, the entity <b>MUST</b> have all of the components. If false the entity must have only one</param>
	/// <returns></returns>
	template<typename... TComponents>
	static bool PropertyEntityReferenceWithComponents(const char* label, UUID& entityID, Ref<Scene> context, const char* helpText = "", bool requiresAllComponents = true, bool doPushUndo = true)
	{
		bool receivedValidEntity = false;
		auto hold = entityID;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float width = ImGui::GetContentRegionAvail().x;
			float itemHeight = 28.0f;

			std::string buttonText = "Null";

			Entity entity = context->TryGetEntityWithUUID(entityID);
			if (entity)
				buttonText = entity.GetComponent<TagComponent>().Tag;

			if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
				buttonText = "---";

			ImGui::Button(GenerateLabelID(buttonText), { width, itemHeight });
		}
		ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

		if (ImGui::BeginDragDropTarget())
		{
			auto data = ImGui::AcceptDragDropPayload("scene_entity_hierarchy");
			if (data)
			{
				UUID tempID = *(UUID*)data->Data;
				Entity temp = context->TryGetEntityWithUUID(tempID);

				if (requiresAllComponents)
				{
					if (temp.HasComponent<TComponents...>())
					{
						entityID = tempID;
						receivedValidEntity = true;
					}
				}
				else
				{
					if (temp.HasAny<TComponents...>())
					{
						entityID = tempID;
						receivedValidEntity = true;
					}
				}
			}

			ImGui::EndDragDropTarget();
		}

		ImGui::PopItemWidth();
		ImGui::NextColumn();

		if (receivedValidEntity && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&entityID, hold);

		return receivedValidEntity;
	}

#endif
#if 0
	template<typename T, typename Fn>
	static bool PropertyAssetReferenceTarget(const char* label, const char* assetName, AssetHandle& outHandle, Fn&& targetFunc, const char* helpText = "", const PropertyAssetReferenceSettings& settings = {}, bool doPushUndo = true)
	{
		bool modified = false;

		auto hold = outHandle;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);
		if (settings.NoItemSpacing)
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
		float width = ImGui::GetContentRegionAvail().x - settings.WidthOffset;
		ImGuiEx::PushID();

		float itemHeight = 28.0f;

		auto [valid, buttonText] = AssetValidityAndName(outHandle, settings);

		if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
			buttonText = "---";

		// PropertyAssetReferenceTarget could be called multiple times in same "context"
		// and so we need a unique id for the asset search popup each time.
		// notes
		// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
		// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
		//   buffer which may get overwritten by the time you want to use it later on.
		std::string assetSearchPopupID = GenerateLabelID("ARTSP");
		{
			ImGuiEx::ScopedColour buttonLabelColor(ImGuiCol_Text, valid ? settings.ButtonLabelColor : settings.ButtonLabelColorError);
			ImGui::Button(GenerateLabelID(buttonText), { width, itemHeight });

			const bool isHovered = ImGui::IsItemHovered();

			if (isHovered)
			{
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					// NOTE(Peter): Ugly workaround since AssetEditorPanel includes ImGui.h (meaning ImGui.h can't include AssetEditorPanel).
					//				Will rework those includes at a later date...
					AssetEditorPanelInterface::OpenEditor(AssetManager::GetAsset<Asset>(outHandle));
				}
				else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					ImGui::OpenPopup(assetSearchPopupID.c_str());
				}
			}
		}

		ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

		bool clear = false;
		if (Widgets::AssetSearchPopup(assetSearchPopupID.c_str(), T::GetStaticType(), outHandle, &clear))
		{
			if (clear)
				outHandle = 0;

			targetFunc(AssetManager::GetAsset<T>(outHandle));
			modified = true;
		}

		ImGuiEx::PopID();

		if (!IsItemDisabled())
		{
			if (ImGui::BeginDragDropTarget())
			{
				auto data = ImGui::AcceptDragDropPayload("asset_payload");

				if (data)
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					s_PropertyAssetReferenceAssetHandle = assetHandle;
					Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
					if (asset && asset->GetAssetType() == T::GetStaticType())
					{
						targetFunc(asset.As<T>());
						modified = true;
					}
				}

				ImGui::EndDragDropTarget();
			}
		}

		ImGui::PopItemWidth();
		if (settings.AdvanceToNextColumn)
		{
			ImGui::NextColumn();
			Draw::Underline();
		}
		if (settings.NoItemSpacing)
			ImGui::PopStyleVar();

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&outHandle, hold);
#endif

		return modified;
	}
#endif

	// To be used in conjunction with AssetSearchPopup. Call ImGui::OpenPopup for AssetSearchPopup if this is clicked.
	// Alternatively the click can be used to clear the reference.
	template<typename TAssetType>
	static bool AssetReferenceDropTargetButton(const char* label, Ref<TAssetType>& object, AssetType supportedType, bool& assetDropped, bool dropTargetEnabled = true)
	{
		bool clicked = false;

		ImGuiEx::ScopedStyle border(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGuiEx::ScopedColourStack colors(ImGuiCol_Button, Colors::Theme::propertyField,
			ImGuiCol_ButtonHovered, Colors::Theme::propertyField,
			ImGuiCol_ButtonActive, Colors::Theme::propertyField);

		ImGuiEx::PushID();

		// For some reason ImGui handles button's width differently
		// So need to manually set it if the user has pushed item width
		auto* window = ImGui::GetCurrentWindow();
		const bool itemWidthChanged = !window->DC.ItemWidthStack.empty();
		const float buttonWidth = itemWidthChanged ? window->DC.ItemWidth : 0.0f;

		if (object)
		{
			if (!AssetManager::IsAssetMissing(object->Handle))
			{
				ImGuiEx::ScopedColour text(ImGuiCol_Text, AssetManager::IsAssetValid(object->Handle) ? Colors::Theme::text : Colors::Theme::textError);
				auto assetFileName = Project::GetEditorAssetManager()->GetMetadata(object->Handle).FilePath.stem().string();

				if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
					assetFileName = "---";

				if (ImGui::Button((char*)assetFileName.c_str(), { buttonWidth, 0.0f }))
					clicked = true;
			}
			else
			{
				if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
				{
					ImGuiEx::ScopedColour text(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.3f, 1.0f));
					if (ImGui::Button("---", { buttonWidth, 0.0f }))
						clicked = true;
				}
				else
				{
					ImGuiEx::ScopedColour text(ImGuiCol_Text, Colors::Theme::textError);
					if (ImGui::Button("Missing", { buttonWidth, 0.0f }))
						clicked = true;
				}
			}
		}
		else
		{
			if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
			{
				ImGuiEx::ScopedColour text(ImGuiCol_Text, Colors::Theme::muted);
				if (ImGui::Button("---", { buttonWidth, 0.0f }))
					clicked = true;
			}
			else
			{
				ImGuiEx::ScopedColour text(ImGuiCol_Text, Colors::Theme::muted);
				if (ImGui::Button("Select Asset", { buttonWidth, 0.0f }))
					clicked = true;
			}
		}

		ImGuiEx::PopID();

		if (dropTargetEnabled)
		{
			if (ImGui::BeginDragDropTarget())
			{
				auto data = ImGui::AcceptDragDropPayload("asset_payload");

				if (data)
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
					if (asset && asset->GetAssetType() == supportedType)
					{
						object = asset.As<TAssetType>();
						assetDropped = true;
					}
				}

				ImGui::EndDragDropTarget();
			}
		}

		DrawItemActivityOutline();
		return clicked;
	}

	static bool DrawComboPreview(const char* preview, float width = 100.0f)
	{
		bool pressed = false;

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::BeginHorizontal("horizontal_node_layout", ImVec2(width, 0.0f));
		ImGui::PushItemWidth(90.0f);
		ImGui::InputText("##selected_asset", (char*)preview, 512, ImGuiInputTextFlags_ReadOnly);
		pressed = ImGui::IsItemClicked();
		ImGui::PopItemWidth();

		ImGui::PushItemWidth(10.0f);
		pressed = ImGui::ArrowButton("combo_preview_button", ImGuiDir_Down) || pressed;
		ImGui::PopItemWidth();

		ImGui::EndHorizontal();
		ImGui::PopStyleVar();

		return pressed;
	}

	static int s_CheckboxCount = 0;

	static void BeginCheckboxGroup(const char* label)
	{
		ImGui::Text(label);
		ImGui::NextColumn();
		ImGui::PushItemWidth(-1);
	}

	static bool PropertyCheckboxGroup(const char* label, bool& value, bool doPushUndo = true)
	{
		bool modified = false;
		auto hold = value;

		if (++s_CheckboxCount > 1)
			ImGui::SameLine();

		ImGui::Text(label);
		ImGui::SameLine();

		modified = ImGuiEx::Checkbox(GenerateID(), &value);

#if UndoDo
		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&value, hold);
#endif

		return modified;
	}

	static bool Button(const char* label, const ImVec2& size = ImVec2(0, 0))
	{
		bool result = ImGui::Button(label, size);
		ImGui::NextColumn();
		return result;
	}

	static void EndCheckboxGroup()
	{
		ImGui::PopItemWidth();
		ImGui::NextColumn();
		s_CheckboxCount = 0;
	}

	enum class FieldDisableCondition
	{
		Never, NotWritable, Always
	};

	bool DrawFieldValue(Ref<Scene> sceneContext, std::string_view fieldName, FieldStorage& storage);
	bool DrawFieldArray(Ref<Scene> sceneContext, std::string_view fieldName, FieldStorage& storage);

#if 0
	template<typename T>
	static bool PropertyAssetReferenceArray(const char* label, AssetHandle& outHandle, Ref<ArrayFieldStorage>& arrayStorage, uint32_t index, intptr_t& elementToRemove, const char* helpText = "", const PropertyAssetReferenceSettings& settings = {})
	{
		bool modified = false;

		ImGuiStyle& style = ImGui::GetStyle();

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (helpText && std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		const float buttonSize = ImGui::GetFrameHeight();
		float itemWidth = ImMax(1.0f, ImGui::CalcItemWidth() - (buttonSize + style.ItemInnerSpacing.x));
		ImGui::SetNextItemWidth(itemWidth);

		ImVec2 originalButtonTextAlign = style.ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float itemHeight = 28.0f;

			auto [valid, buttonText] = AssetValidityAndName(outHandle, settings);

			// PropertyAssetReferenceTarget could be called multiple times in same "context"
			// and so we need a unique id for the asset search popup each time.
			// notes
			// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
			// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
			//   buffer which may get overwritten by the time you want to use it later on.
			std::string assetSearchPopupID = GenerateLabelID("ARTSP");
			{
				UI::ScopedColour buttonLabelColor(ImGuiCol_Text, valid ? settings.ButtonLabelColor : settings.ButtonLabelColorError);
				ImGui::Button(GenerateLabelID(buttonText), { itemWidth, itemHeight });

				const bool isHovered = ImGui::IsItemHovered();

				if (isHovered)
				{
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						// NOTE(Peter): Ugly workaround since AssetEditorPanel includes ImGui.h (meaning ImGui.h can't include AssetEditorPanel).
						//				Will rework those includes at a later date...
						AssetEditorPanelInterface::OpenEditor(AssetManager::GetAsset<Asset>(outHandle));
					}
					else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						ImGui::OpenPopup(assetSearchPopupID.c_str());
					}
				}
			}

			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

			bool clear = false;
			if (Widgets::AssetSearchPopup(assetSearchPopupID.c_str(), T::GetStaticType(), outHandle, &clear))
			{
				if (clear)
					outHandle = 0;
				modified = true;
			}
		}

		if (ImGui::BeginDragDropTarget())
		{
			auto data = ImGui::AcceptDragDropPayload("asset_payload");

			if (data)
			{
				AssetHandle assetHandle = *(AssetHandle*)data->Data;
				s_PropertyAssetReferenceAssetHandle = assetHandle;
				const auto& metadata = Project::GetEditorAssetManager()->GetMetadata(assetHandle);

				if (metadata.Type == T::GetStaticType())
				{
					outHandle = assetHandle;
					modified = true;
				}
			}

			ImGui::EndDragDropTarget();
		}

		if (modified)
			arrayStorage->SetValue<uint64_t>(static_cast<uint32_t>(index), outHandle);

		const ImVec2 backupFramePadding = style.FramePadding;
		style.FramePadding.x = style.FramePadding.y;
		ImGui::SameLine(0, style.ItemInnerSpacing.x);

		if (ImGui::Button(GenerateLabelID("x"), ImVec2(buttonSize, buttonSize)))
			elementToRemove = intptr_t(index);

		style.FramePadding = backupFramePadding;

		ImGui::PopItemWidth();
		ImGui::NextColumn();

		return modified;
	}

	static bool PropertyEntityReferenceArray(const char* label, UUID& entityID, Ref<Scene> context, Ref<ArrayFieldStorage>& arrayStorage, uintptr_t index, intptr_t& elementToRemove)
	{
		bool receivedValidEntity = false;

		ImGuiStyle& style = ImGui::GetStyle();

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		const float buttonSize = ImGui::GetFrameHeight();
		float itemWidth = ImMax(1.0f, ImGui::CalcItemWidth() - (buttonSize + style.ItemInnerSpacing.x));
		ImGui::SetNextItemWidth(itemWidth);

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float width = ImGui::GetContentRegionAvail().x;
			float itemHeight = 28.0f;

			std::string buttonText = "Null";

			Entity entity = context->TryGetEntityWithUUID(entityID);
			if (entity)
				buttonText = entity.GetComponent<TagComponent>().Tag;

			ImGui::Button(GenerateLabelID(buttonText), { width - buttonSize, itemHeight });
		}
		ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

		if (ImGui::BeginDragDropTarget())
		{
			auto data = ImGui::AcceptDragDropPayload("scene_entity_hierarchy");
			if (data)
			{
				entityID = *(UUID*)data->Data;
				receivedValidEntity = true;
			}

			ImGui::EndDragDropTarget();
		}

		if (receivedValidEntity)
			arrayStorage->SetValue<uint64_t>((uint32_t)index, entityID);

		const ImVec2 backupFramePadding = style.FramePadding;
		style.FramePadding.x = style.FramePadding.y;
		ImGui::SameLine(0, style.ItemInnerSpacing.x);

		if (ImGui::Button(GenerateLabelID("x"), ImVec2(buttonSize, buttonSize)))
			elementToRemove = index;

		style.FramePadding = backupFramePadding;

		ImGui::PopItemWidth();
		ImGui::NextColumn();

		return receivedValidEntity;
	}

	static bool DrawFieldArray(Ref<Scene> sceneContext, const std::string& fieldName, Ref<ArrayFieldStorage> arrayStorage)
	{
		bool modified = false;
		intptr_t elementToRemove = -1;

		std::string arrayID = std::format("{0}FieldArray", fieldName);
		ImGui::PushID(arrayID.c_str());

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(fieldName.c_str());
		ImGui::NextColumn();
		ImGui::NextColumn();

		uintptr_t length = arrayStorage->GetLength();
		uintptr_t tempLength = length;
		FieldType nativeType = arrayStorage->GetFieldInfo()->Type;

		if (PropertyInput("Length", tempLength, 1, 1, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			arrayStorage->Resize((uint32_t)tempLength);
			length = tempLength;
			modified = true;
		}

		ShiftCursorY(5.0f);

		auto drawScalarElement = [&arrayStorage, &elementToRemove, &arrayID](std::string_view indexString, uintptr_t index, ImGuiDataType dataType, auto& data, int32_t components, const char* format)
			{
				ImGuiStyle& style = ImGui::GetStyle();

				ShiftCursor(10.0f, 9.0f);
				ImGui::Text(indexString.data());
				ImGui::NextColumn();
				ShiftCursorY(4.0f);
				ImGui::PushItemWidth(-1);

				const float buttonSize = ImGui::GetFrameHeight();
				ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::CalcItemWidth() - (buttonSize + style.ItemInnerSpacing.x)));

				size_t dataSize = ImGui::DataTypeGetInfo(dataType)->Size;
				if (components > 1)
				{
					if (DragScalarN(GenerateID(), dataType, &data, components, 1.0f, (const void*)0, (const void*)0, format, 0))
						arrayStorage->SetValue<std::remove_reference_t<decltype(data)>>((uint32_t)index, data);
				}
				else
				{
					if (DragScalar(GenerateID(), dataType, &data, 1.0f, (const void*)0, (const void*)0, format, (ImGuiSliderFlags)0))
						arrayStorage->SetValue<std::remove_reference_t<decltype(data)>>((uint32_t)index, data);
				}

				const ImVec2 backupFramePadding = style.FramePadding;
				style.FramePadding.x = style.FramePadding.y;
				ImGui::SameLine(0, style.ItemInnerSpacing.x);

				if (ImGui::Button(GenerateLabelID("x"), ImVec2(buttonSize, buttonSize)))
					elementToRemove = index;

				style.FramePadding = backupFramePadding;

				ImGui::PopItemWidth();
				ImGui::NextColumn();
				Draw::Underline();
			};

		auto drawStringElement = [&arrayStorage, &elementToRemove](std::string_view indexString, uintptr_t index, std::string& data)
			{
				ImGuiStyle& style = ImGui::GetStyle();

				ShiftCursor(10.0f, 9.0f);
				ImGui::Text(indexString.data());
				ImGui::NextColumn();
				ShiftCursorY(4.0f);
				ImGui::PushItemWidth(-1);

				const float buttonSize = ImGui::GetFrameHeight();
				ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::CalcItemWidth() - (buttonSize + style.ItemInnerSpacing.x)));

				if (InputText(GenerateID(), &data))
					arrayStorage->SetValue<std::string>((uint32_t)index, data);

				const ImVec2 backupFramePadding = style.FramePadding;
				style.FramePadding.x = style.FramePadding.y;
				ImGui::SameLine(0, style.ItemInnerSpacing.x);

				if (ImGui::Button(GenerateLabelID("x"), ImVec2(buttonSize, buttonSize)))
					elementToRemove = index;

				style.FramePadding = backupFramePadding;

				ImGui::PopItemWidth();
				ImGui::NextColumn();
				Draw::Underline();
			};

		for (uint32_t i = 0; i < (uint32_t)length; i++)
		{
			std::string idString = std::format("[{0}]{1}-{0}", i, arrayID);
			std::string indexString = std::format("[{0}]", i);

			ImGui::PushID(idString.c_str());

			switch (nativeType)
			{
			case FieldType::Bool:
			{
				bool value = arrayStorage->GetValue<bool>(i);
				if (Property(indexString.c_str(), value))
				{
					arrayStorage->SetValue(i, value);
					modified = true;
				}
				break;
			}
			case FieldType::Int8:
			{
				int8_t value = arrayStorage->GetValue<int8_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_S8, value, 1, "%d");
				break;
			}
			case FieldType::Int16:
			{
				int16_t value = arrayStorage->GetValue<int16_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_S16, value, 1, "%d");
				break;
			}
			case FieldType::Int32:
			{
				int32_t value = arrayStorage->GetValue<int32_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_S32, value, 1, "%d");
				break;
			}
			case FieldType::Int64:
			{
				int64_t value = arrayStorage->GetValue<int64_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_S64, value, 1, "%d");
				break;
			}
			case FieldType::UInt8:
			{
				uint8_t value = arrayStorage->GetValue<uint8_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_U8, value, 1, "%d");
				break;
			}
			case FieldType::UInt16:
			{
				uint16_t value = arrayStorage->GetValue<uint16_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_U16, value, 1, "%d");
				break;
			}
			case FieldType::UInt32:
			{
				uint32_t value = arrayStorage->GetValue<uint32_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_U32, value, 1, "%d");
				break;
			}
			case FieldType::UInt64:
			{
				uint64_t value = arrayStorage->GetValue<uint64_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_U64, value, 1, "%d");
				break;
			}
			case FieldType::Float:
			{
				float value = arrayStorage->GetValue<float>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Float, value, 1, "%.3f");
				break;
			}
			case FieldType::Double:
			{
				double value = arrayStorage->GetValue<double>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Double, value, 1, "%.6f");
				break;
			}
			case FieldType::String:
			{
				std::string value = arrayStorage->GetValue<std::string>(i);
				drawStringElement(indexString, i, value);
				break;
			}
			case FieldType::Vector2:
			{
				glm::vec2 value = arrayStorage->GetValue<glm::vec2>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Float, value, 2, "%.3f");
				break;
			}
			case FieldType::Vector3:
			{
				glm::vec3 value = arrayStorage->GetValue<glm::vec3>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Float, value, 3, "%.3f");
				break;
			}
			case FieldType::Vector4:
			{
				glm::vec4 value = arrayStorage->GetValue<glm::vec4>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Float, value, 4, "%.3f");
				break;
			}
			case FieldType::Prefab:
			{
				AssetHandle handle = arrayStorage->GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<Prefab>(indexString.c_str(), handle, arrayStorage, i, elementToRemove);
				break;
			}
			case FieldType::Entity:
			{
				UUID uuid = arrayStorage->GetValue<UUID>(i);
				PropertyEntityReferenceArray(indexString.c_str(), uuid, sceneContext, arrayStorage, i, elementToRemove);
				break;
			}
			case FieldType::Mesh:
			{
				AssetHandle handle = arrayStorage->GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<Mesh>(indexString.c_str(), handle, arrayStorage, i, elementToRemove);
				break;
			}
			case FieldType::StaticMesh:
			{
				AssetHandle handle = arrayStorage->GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<StaticMesh>(indexString.c_str(), handle, arrayStorage, i, elementToRemove);
				break;
			}
			case FieldType::Material:
			{
				AssetHandle handle = arrayStorage->GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<MaterialAsset>(indexString.c_str(), handle, arrayStorage, i, elementToRemove);
				break;
			}
			/*case UnmanagedType::PhysicsMaterial:
			{
				ColliderMaterial material = storage->GetValue<ColliderMaterial>(managedInstance);

				if (PropertyGridHeader(std::format("{} (ColliderMaterial)", fieldName)))
				{
					if (Property("Friction", material.Friction))
					{
						storage->SetValue(managedInstance, material);
						result = true;
					}

					if (Property("Restitution", material.Restitution))
					{
						storage->SetValue(managedInstance, material);
						result = true;
					}

					EndTreeNode();
				}

				AssetHandle handle = valueWrapper.GetOrDefault<AssetHandle>(0);
				PropertyAssetReferenceArray<PhysicsMaterial>(indexString.c_str(), handle, managedInstance, arrayStorage, i, elementToRemove);
				break;
			}*/
			case FieldType::Texture2D:
			{
				AssetHandle handle = arrayStorage->GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<Texture2D>(indexString.c_str(), handle, arrayStorage, i, elementToRemove);
				break;
			}
			case FieldType::Scene:
			{
				AssetHandle handle = arrayStorage->GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<Scene>(indexString.c_str(), handle, arrayStorage, i, elementToRemove);
				break;
			}
			}

			ImGui::PopID();
		}

		ImGui::PopID();

		if (elementToRemove != -1)
		{
			arrayStorage->RemoveAt((uint32_t)elementToRemove);
			modified = true;
		}

		return modified;
	}
#endif
}
