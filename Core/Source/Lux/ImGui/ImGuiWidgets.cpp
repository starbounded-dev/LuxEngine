#include "lpch.h"
#include "ImGuiWidgets.h"

#include "Lux/Asset/AssetManager.h"

#include "Lux/Scene/Components.h"
//#include "Lux/Script/ScriptEngine.h"

#include <format>
#include <tuple>

namespace Lux::ImGuiEx
{
	bool Widgets::AssetSearchPopup(const char* ID, AssetHandle& selected, bool* cleared, const char* hint, ImVec2 size, std::initializer_list<AssetType> assetTypes)
	{
		ImGuiEx::ScopedColour popupBG(ImGuiCol_PopupBg, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.6f));

		bool modified = false;

		const auto& assetRegistry = Project::GetActive()->GetEditorAssetManager()->GetAssetRegistry();
		AssetHandle current = selected;

		ImGui::SetNextWindowSize({ size.x, 0.0f });

		static bool grabFocus = true;

		if (ImGuiEx::BeginPopup(ID, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
		{
			static std::string searchString;

			if (ImGui::GetCurrentWindow()->Appearing)
			{
				grabFocus = true;
				searchString.clear();
			}

			// Search widget
			ImGuiEx::ShiftCursor(3.0f, 2.0f);
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - ImGui::GetCursorPosX() * 2.0f);
			SearchWidget(searchString, hint, &grabFocus);

			const bool searching = !searchString.empty();

			// Clear property button
			if (cleared != nullptr)
			{
				ImGuiEx::ScopedColourStack buttonColours(
					ImGuiCol_Button, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.0f),
					ImGuiCol_ButtonHovered, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.2f),
					ImGuiCol_ButtonActive, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 0.9f));

				ImGuiEx::ScopedStyle border(ImGuiStyleVar_FrameBorderSize, 0.0f);

				ImGui::SetCursorPosX(0);

				ImGui::PushItemFlag(ImGuiItemFlags_NoNav, searching);

				if (ImGui::Button("CLEAR", { ImGui::GetWindowWidth(), 0.0f }))
				{
					*cleared = true;
					modified = true;
				}

				ImGui::PopItemFlag();
			}

			// List of assets
			{
				ImGuiEx::ScopedColour listBoxBg(ImGuiCol_FrameBg, IM_COL32_DISABLE);
				ImGuiEx::ScopedColour listBoxBorder(ImGuiCol_Border, IM_COL32_DISABLE);

				ImGuiID listID = ImGui::GetID("##SearchListBox");
				if (ImGui::BeginListBox("##SearchListBox", ImVec2(-FLT_MIN, 0.0f)))
				{
					bool forwardFocus = false;

					ImGuiContext& g = *GImGui;
					if (g.NavJustMovedToId != 0)
					{
						if (g.NavJustMovedToId == listID)
						{
							forwardFocus = true;
							// ActivateItem moves keyboard navigation focus inside of the window
							ImGui::ActivateItemByID(listID);
							ImGui::SetKeyboardFocusHere(1);
						}
					}

					std::vector<std::tuple<std::string, AssetType, AssetHandle>> assets;

					for (const auto& [handle, metadata] : assetRegistry)
					{
						bool isValidType = false;

						for (AssetType type : assetTypes)
						{
							if (metadata.Type == type)
							{
								isValidType = true;
								break;
							}
						}

						if (!isValidType)
							continue;

						const std::string assetName = metadata.FilePath.stem().string();

						if (!searchString.empty() && !ImGuiEx::IsMatchingSearch(assetName, searchString))
							continue;

						assets.emplace_back(std::format("{}##{}", assetName, metadata.FilePath.string()), metadata.Type, handle);
					}

					std::sort(assets.begin(), assets.end(), [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });

					for (const auto& [label, type, handle] : assets)
					{
						bool is_selected = (current == handle);
						if (ImGui::Selectable(label.c_str(), is_selected))
						{
							current = handle;
							selected = handle;
							modified = true;
						}

						{
							auto assetType = Utils::String::ToUpperCopy(Utils::AssetTypeToString(type));
							ImVec2 textSize = ImGui::CalcTextSize(assetType.c_str());
							ImVec2 rectSize = ImGui::GetItemRectSize();
							float paddingX = ImGui::GetStyle().FramePadding.x;

							ImGui::SameLine(rectSize.x - textSize.x - paddingX);

							ImGuiEx::ScopedColour textColour(ImGuiCol_Text, Colors::Theme::textDarker);
							ImGui::TextUnformatted(assetType.c_str());
						}

						if (forwardFocus)
						{
							forwardFocus = false;
						}
						else if (is_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndListBox();
				}
			}
			if (modified)
				ImGui::CloseCurrentPopup();

			ImGuiEx::EndPopup();
		}

		return modified;
	}

	bool Widgets::AssetSearchPopup(const char* ID, AssetType assetType, AssetHandle& selected, bool* cleared, const char* hint /*= "Search Assets"*/, ImVec2 size)
	{
		return AssetSearchPopup(ID, selected, cleared, hint, size, { assetType });
	}

	bool Widgets::EntitySearchPopup(const char* ID, Ref<Scene> scene, UUID& selected, bool* cleared /*= nullptr*/, const char* hint /*= "Search Entities"*/, ImVec2 size /*= { 250.0f, 350.0f }*/)
	{
		ImGuiEx::ScopedColour popupBG(ImGuiCol_PopupBg, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.6f));

		bool modified = false;

		auto entities = scene->GetAllEntitiesWith<IDComponent, TagComponent>();
		UUID current = selected;

		ImGui::SetNextWindowSize({ size.x, 0.0f });

		static bool s_GrabFocus = true;

		if (ImGuiEx::BeginPopup(ID, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
		{
			static std::string searchString;

			if (ImGui::GetCurrentWindow()->Appearing)
			{
				s_GrabFocus = true;
				searchString.clear();
			}

			// Search widget
			ImGuiEx::ShiftCursor(3.0f, 2.0f);
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - ImGui::GetCursorPosX() * 2.0f);
			SearchWidget(searchString, hint, &s_GrabFocus);

			const bool searching = !searchString.empty();

			// Clear property button
			if (cleared != nullptr)
			{
				ImGuiEx::ScopedColourStack buttonColours(
					ImGuiCol_Button, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.0f),
					ImGuiCol_ButtonHovered, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.2f),
					ImGuiCol_ButtonActive, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 0.9f));

				ImGuiEx::ScopedStyle border(ImGuiStyleVar_FrameBorderSize, 0.0f);

				ImGui::SetCursorPosX(0);

				ImGui::PushItemFlag(ImGuiItemFlags_NoNav, searching);

				if (ImGui::Button("CLEAR", { ImGui::GetWindowWidth(), 0.0f }))
				{
					*cleared = true;
					modified = true;
				}

				ImGui::PopItemFlag();
			}

			// List of entities
			{
				ImGuiEx::ScopedColour listBoxBg(ImGuiCol_FrameBg, IM_COL32_DISABLE);
				ImGuiEx::ScopedColour listBoxBorder(ImGuiCol_Border, IM_COL32_DISABLE);

				ImGuiID listID = ImGui::GetID("##SearchListBox");
				if (ImGui::BeginListBox("##SearchListBox", ImVec2(-FLT_MIN, 0.0f)))
				{
					bool forwardFocus = false;

					ImGuiContext& g = *GImGui;
					if (g.NavJustMovedToId != 0)
					{
						if (g.NavJustMovedToId == listID)
						{
							forwardFocus = true;
							// ActivateItem moves keyboard navigation focuse inside of the window
							ImGui::ActivateItemByID(listID);
							ImGui::SetKeyboardFocusHere(1);
						}
					}

					for (auto enttID : entities)
					{
						const auto& idComponent = entities.get<IDComponent>(enttID);
						const auto& tagComponent = entities.get<TagComponent>(enttID);

						if (!searchString.empty() && !ImGuiEx::IsMatchingSearch(tagComponent.Tag, searchString))
							continue;

						bool is_selected = current == idComponent.ID;
						if (ImGui::Selectable(tagComponent.Tag.c_str(), is_selected))
						{
							current = selected = idComponent.ID;
							modified = true;
						}

						if (forwardFocus)
							forwardFocus = false;
						else if (is_selected)
							ImGui::SetItemDefaultFocus();
					}

					ImGui::EndListBox();
				}
			}
			if (modified)
				ImGui::CloseCurrentPopup();

			ImGuiEx::EndPopup();
		}

		return modified;
	}

	bool Widgets::ItemSearchPopup(const char* ID, int32_t& selected, int32_t itemCount, std::function<const char* (int32_t)> onGetElementName, bool* cleared, const char* hint, ImVec2 size)
	{
		ImGuiEx::ScopedColour popupBG(ImGuiCol_PopupBg, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.6f));

		bool modified = false;
		const char* current = onGetElementName(selected);

		ImGui::SetNextWindowSize({ size.x, 0.0f });

		static bool grabFocus = true;

		if (ImGuiEx::BeginPopup(ID, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
		{
			static std::string searchString;

			if (ImGui::GetCurrentWindow()->Appearing)
			{
				grabFocus = true;
				searchString.clear();
			}

			// Search widget
			ImGuiEx::ShiftCursor(3.0f, 2.0f);
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - ImGui::GetCursorPosX() * 2.0f);
			SearchWidget(searchString, hint, &grabFocus);

			const bool searching = !searchString.empty();

			// Clear property button
			if (cleared != nullptr)
			{
				ImGuiEx::ScopedColourStack buttonColours(
					ImGuiCol_Button, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.0f),
					ImGuiCol_ButtonHovered, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 1.2f),
					ImGuiCol_ButtonActive, ImGuiEx::ColourWithMultipliedValue(Colors::Theme::background, 0.9f));

				ImGuiEx::ScopedStyle border(ImGuiStyleVar_FrameBorderSize, 0.0f);

				ImGui::SetCursorPosX(0);

				ImGui::PushItemFlag(ImGuiItemFlags_NoNav, searching);

				if (ImGui::Button("CLEAR", { ImGui::GetWindowWidth(), 0.0f }))
				{
					*cleared = true;
					selected = -1;
					modified = true;
				}

				ImGui::PopItemFlag();
			}

			// List of assets
			{
				ImGuiEx::ScopedColour listBoxBg(ImGuiCol_FrameBg, IM_COL32_DISABLE);
				ImGuiEx::ScopedColour listBoxBorder(ImGuiCol_Border, IM_COL32_DISABLE);

				ImGuiID listID = ImGui::GetID("##SearchListBox");
				if (ImGui::BeginListBox("##SearchListBox", ImVec2(-FLT_MIN, 0.0f)))
				{
					bool forwardFocus = false;

					ImGuiContext& g = *GImGui;
					if (g.NavJustMovedToId != 0)
					{
						if (g.NavJustMovedToId == listID)
						{
							forwardFocus = true;
							// ActivateItem moves keyboard navigation focuse inside of the window
							ImGui::ActivateItemByID(listID);
							ImGui::SetKeyboardFocusHere(1);
						}
					}

					for (int i = 0; i < itemCount; ++i)
					{
						std::string itemName(onGetElementName(i));

						if (!searchString.empty() && !ImGuiEx::IsMatchingSearch(itemName, searchString))
							continue;

						bool isSelected = (current == itemName);
						if (ImGui::Selectable(itemName.c_str(), isSelected))
						{
							current = itemName.c_str();
							selected = i;
							modified = true;
						}

						if (forwardFocus)
						{
							forwardFocus = false;
						}
						else if (isSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndListBox();
				}
			}
			if (modified)
				ImGui::CloseCurrentPopup();

			ImGuiEx::EndPopup();
		}

		return modified;
	}

} // namespace Hazel::UI
