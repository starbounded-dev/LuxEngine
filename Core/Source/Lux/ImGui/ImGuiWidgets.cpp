// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ImGuiWidgets.h"

#include "Lux/Asset/AssetManager.h"

#include "Lux/Scene/Components.h"
//#include "Lux/Script/ScriptEngine.h"

#include <algorithm>

namespace Lux::ImGuiEx
{
	namespace
	{
		struct AssetPickerRow
		{
			AssetHandle Handle;
			std::string Name;
			std::string Path;
			std::string Type;
		};
		struct AssetPickerCache
		{
			const EditorAssetManager* Owner = nullptr;
			ImGuiID Popup = 0;
			uint64_t Revision = 0;
			std::vector<AssetType> Types;
			std::vector<AssetPickerRow> Rows;
			std::vector<size_t> Matches;
			char Search[256]{};
			std::string Filter;
			bool Focus = false;
		};
		// One visible picker snapshot; owns labels and handles, never a project/asset manager.
		AssetPickerCache s_AssetPicker;
	} // namespace

	bool Widgets::AssetSearchPopup(const char* ID, AssetHandle& selected, bool* cleared, const char* hint,
								   ImVec2 size, std::initializer_list<AssetType> assetTypes)
	{
		ScopedColour popupBG(ImGuiCol_PopupBg, ColourWithMultipliedValue(Colors::Theme::background, 1.6f));
		const float fontScale = ImGui::GetStyle()._MainScale;
		ImGui::SetNextWindowSize({std::min(size.x * fontScale, ImGui::GetMainViewport()->WorkSize.x), 0.0f});
		bool modified = false;
		if (ImGuiEx::BeginPopup(ID, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
		{
			const auto project = Project::GetActive();
			auto manager = project ? Project::GetEditorAssetManager() : nullptr;
			if (!manager)
			{
				ImGui::TextWrapped("Open a project to choose an asset.");
				ImGuiEx::EndPopup();
				return false;
			}
			auto& cache = s_AssetPicker;
			const ImGuiID popup = ImGui::GetCurrentWindow()->ID;
			const bool appearing = ImGui::IsWindowAppearing();
			if (appearing)
			{
				cache.Search[0] = 0;
				cache.Focus = true;
			}
			const auto& registry = manager->GetAssetRegistry();
			const bool typesChanged = cache.Types.size() != assetTypes.size() ||
									  !std::equal(cache.Types.begin(), cache.Types.end(), assetTypes.begin());
			const bool rebuild = appearing || cache.Popup != popup || cache.Owner != manager.Raw() ||
								 cache.Revision != registry.GetRevision() || typesChanged;
			if (rebuild)
			{
				cache.Popup = popup;
				cache.Owner = manager.Raw();
				cache.Revision = registry.GetRevision();
				cache.Types.assign(assetTypes.begin(), assetTypes.end());
				cache.Rows.clear();
				cache.Rows.reserve(registry.Count());
				for (const auto& [handle, metadata] : registry)
				{
					if (!metadata.IsValid() || metadata.FilePath.empty() ||
						metadata.Type == AssetType::None || manager->IsMemoryAsset(handle) ||
						(!cache.Types.empty() &&
						 std::ranges::find(cache.Types, metadata.Type) == cache.Types.end()))
						continue;
					cache.Rows.push_back({handle, Utils::PathToUTF8(metadata.FilePath.stem()),
										  Utils::PathToUTF8(metadata.FilePath),
										  std::string(AssetTypeToString(metadata.Type))});
				}
				std::ranges::sort(cache.Rows, {}, &AssetPickerRow::Path);
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (cache.Focus)
			{
				ImGui::SetKeyboardFocusHere();
				cache.Focus = false;
			}
			const bool commitSearch =
				ImGui::InputTextWithHint("##asset_search", hint, cache.Search, sizeof(cache.Search),
										 ImGuiInputTextFlags_EnterReturnsTrue);
			if (rebuild || cache.Filter != cache.Search)
			{
				cache.Filter = cache.Search;
				cache.Matches.clear();
				cache.Matches.reserve(cache.Rows.size());
				for (size_t i = 0; i < cache.Rows.size(); ++i)
					if (!cache.Search[0] || IsMatchingSearch(cache.Rows[i].Path, cache.Search))
						cache.Matches.push_back(i);
			}
			if (cleared && ImGui::Button("Clear selection", {-FLT_MIN, 0.0f}))
			{
				*cleared = true;
				modified = true;
			}
			const float rowHeight = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
			const float listHeight = std::min(std::max(rowHeight, size.y * fontScale),
											  ImGui::GetMainViewport()->WorkSize.y * 0.6f);
			if (ImGui::BeginListBox("##SearchListBox", {-FLT_MIN, listHeight}))
			{
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(cache.Matches.size()),
							  rowHeight + ImGui::GetStyle().ItemSpacing.y);
				while (clipper.Step())
					for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
					{
						const auto& row = cache.Rows[cache.Matches[static_cast<size_t>(i)]];
						ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(row.Handle)));
						if (ImGui::Selectable("##asset", selected == row.Handle, 0, {0.0f, rowHeight}))
						{
							selected = row.Handle;
							modified = true;
						}
						ImGui::SetItemTooltip("%s\n%s", row.Path.c_str(), row.Type.c_str());
						if (selected == row.Handle)
							ImGui::SetItemDefaultFocus();
						const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
						ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), minimum, maximum, maximum.x,
												  row.Name.c_str(), nullptr, nullptr);
						ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
						ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
												  {minimum.x, minimum.y + ImGui::GetTextLineHeight()},
												  maximum, maximum.x, row.Path.c_str(), nullptr, nullptr);
					}
				if (cache.Matches.empty())
					ImGui::TextWrapped("No matching assets. Change the search or import a compatible asset.");
				ImGui::EndListBox();
			}
			if (commitSearch && !cache.Matches.empty())
			{
				selected = cache.Rows[cache.Matches.front()].Handle;
				modified = true;
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
