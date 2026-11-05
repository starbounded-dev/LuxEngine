// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ContentBrowserItem.h"

#include "../ContentBrowserPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Core/Input.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace Lux {

	namespace
	{
		char s_RenameBuffer[MAX_INPUT_BUFFER_LENGTH]{};

		// Rendered previews (materials, images) sit inset in the tile with rounded corners; type
		// icons keep the tighter square layout they were drawn for.
		constexpr float k_GridThumbnailPadding = 10.0f;
		constexpr float k_GridThumbnailRounding = 6.0f;
		constexpr float k_ListThumbnailRounding = 3.0f;

		// Thumbnails are stored bottom-up, hence the flipped V.
		void DrawThumbnail(const Ref<Texture2D>& thumbnail, const ImRect& rect, float rounding)
		{
			const ImU32 tint = ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 235);
			ImGui::GetWindowDrawList()->AddImageRounded(ImGuiEx::GetTextureID(thumbnail), rect.Min, rect.Max,
				ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint, rounding);
		}
	}

	ContentBrowserItem::ContentBrowserItem(ItemType type, AssetHandle id, const std::string& name,
										   const Ref<Texture2D>& icon, std::string path)
		: m_Type(type), m_ID(id), m_FileName(name), m_Icon(icon), m_PathLabel(std::move(path))
	{
	}

	void ContentBrowserItem::OnRenderBegin()
	{
		ImGui::PushID(reinterpret_cast<const void*>(static_cast<uint64_t>(m_ID)));
		ImGui::BeginGroup();
	}

	CBItemActionResult ContentBrowserItem::OnRender(ContentBrowserPanel* context)
	{
		CBItemActionResult result;

		const float thumbnailSize =
			std::min(context->GetThumbnailSize(), std::max(1.0f, ImGui::GetContentRegionAvail().x));
		const bool showAssetTypes = context->GetShowAssetTypes();
		const bool isSelected = context->IsItemSelected(m_ID);


		auto* drawList = ImGui::GetWindowDrawList();
		ImRect itemRect;
		bool hovered = false;

		if (context->IsListView())
		{
			// ---- Compact list row: small icon, name, type ----
			const float rowHeight = ImGui::GetFrameHeight() + 6.0f;
			// At least a pixel wide: InvisibleButton asserts on a zero size.
			const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);

			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::InvisibleButton("##ItemButton", ImVec2(rowWidth, rowHeight));
			ImGui::PopStyleVar();

			itemRect = ImGuiEx::GetItemRect();
			hovered = ImGui::IsItemHovered();

			if (isSelected)
			{
				drawList->AddRectFilled(itemRect.Min, itemRect.Max, Colors::Theme::selectionMuted, 3.0f);
				drawList->AddRect(itemRect.Min, itemRect.Max, Colors::Theme::accent, 3.0f, 0, 1.0f);
			}
			else if (hovered)
			{
				drawList->AddRectFilled(itemRect.Min, itemRect.Max, IM_COL32(255, 255, 255, 14), 3.0f);
			}

			const float iconSize = rowHeight - 8.0f;
			const ImVec2 iconMin(itemRect.Min.x + 6.0f, itemRect.Min.y + 4.0f);

			const ImRect iconRect(iconMin, ImVec2(iconMin.x + iconSize, iconMin.y + iconSize));
			Ref<Texture2D> thumbnail = m_Type == ItemType::Asset ? context->GetItemThumbnail(m_ID) : nullptr;
			if (thumbnail)
			{
				DrawThumbnail(thumbnail, iconRect, k_ListThumbnailRounding);
			}
			else if (m_Icon)
			{
				ImGuiEx::DrawButtonImage(m_Icon,
					IM_COL32(255, 255, 255, 225), IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 255),
					iconRect, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			}

			const float textY = itemRect.Min.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
			const float nameX = iconMin.x + iconSize + 8.0f;

			if (m_IsRenaming)
			{
				ImGui::SetCursorScreenPos(ImVec2(nameX, itemRect.Min.y + 3.0f));
				ImGui::SetKeyboardFocusHere();
				ImGui::SetNextItemWidth(itemRect.Max.x - nameX - 10.0f);
				ImGuiEx::InputText("##Rename", s_RenameBuffer, MAX_INPUT_BUFFER_LENGTH, ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::IsItemDeactivatedAfterEdit() || Input::IsKeyDown(KeyCode::Enter))
				{
					Rename(s_RenameBuffer);
					m_IsRenaming = false;
					result.Set(ContentBrowserAction::Renamed, true);
				}
			}
			else
			{
				const bool showType =
					showAssetTypes && m_Type == ItemType::Asset && rowWidth > ImGui::GetFontSize() * 20.0f;
				const char* type = showType ? AssetTypeToString(AssetManager::GetAssetType(m_ID)).data() : "";
				const float typeWidth =
					showType ? ImGui::CalcTextSize(type).x + ImGui::GetStyle().ItemSpacing.x : 0.0f;
				ImGui::RenderTextEllipsis(
					drawList, {nameX, textY}, {std::max(nameX, itemRect.Max.x - typeWidth), itemRect.Max.y},
					std::max(nameX, itemRect.Max.x - typeWidth), m_FileName.c_str(), nullptr, nullptr);
				if (showType)
					drawList->AddText({itemRect.Max.x - typeWidth, textY}, Colors::Theme::textDarker, type);
			}

			ImGui::SetCursorScreenPos(itemRect.Max);
			ImGui::Dummy({0.0f, 0.0f});
		}
		else
		{
			const float textPadding = std::min(ImGui::GetStyle().FramePadding.x, thumbnailSize * 0.1f);
			const float typeLineHeight = showAssetTypes && m_Type == ItemType::Asset ? ImGui::GetTextLineHeight() + 2.0f : 0.0f;
			const float infoPanelHeight = ImGui::GetTextLineHeight() + typeLineHeight + textPadding;
			const ImVec2 itemSize(thumbnailSize, thumbnailSize + infoPanelHeight);

			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::InvisibleButton("##ItemButton", itemSize);
			ImGui::PopStyleVar();

			itemRect = ImGuiEx::GetItemRect();
			ImRect thumbRect(itemRect.Min, ImVec2(itemRect.Max.x, itemRect.Min.y + thumbnailSize));
			ImRect infoRect(ImVec2(itemRect.Min.x, itemRect.Min.y + thumbnailSize), itemRect.Max);
			hovered = ImGui::IsItemHovered();

			if (m_Type == ItemType::Directory)
			{
				if (hovered || isSelected)
				{
					drawList->AddRectFilled(itemRect.Min, itemRect.Max, Colors::Theme::groupHeader, 6.0f);
					drawList->AddRect(itemRect.Min, itemRect.Max, hovered ? Colors::Theme::accent : Colors::Theme::selection, 6.0f, 0, isSelected ? 1.5f : 1.0f);
				}
			}
			else
			{
				drawList->AddRectFilled(thumbRect.Min, thumbRect.Max, Colors::Theme::backgroundDark, 6.0f, ImDrawFlags_RoundCornersTop);
				drawList->AddRectFilled(infoRect.Min, infoRect.Max, Colors::Theme::groupHeader, 6.0f, ImDrawFlags_RoundCornersBottom);

				if (hovered || isSelected)
					drawList->AddRect(itemRect.Min, itemRect.Max, hovered ? Colors::Theme::accent : Colors::Theme::selection, 6.0f, 0, isSelected ? 1.5f : 1.0f);
			}

			Ref<Texture2D> thumbnail = m_Type == ItemType::Asset ? context->GetItemThumbnail(m_ID) : nullptr;
			if (thumbnail)
			{
				DrawThumbnail(thumbnail,
							  ImGuiEx::RectExpanded(thumbRect,
													-std::min(k_GridThumbnailPadding, thumbnailSize * 0.1f),
													-std::min(k_GridThumbnailPadding, thumbnailSize * 0.1f)),
							  k_GridThumbnailRounding);
			}
			else if (m_Icon)
			{
				ImGuiEx::DrawButtonImage(m_Icon,
					IM_COL32(255, 255, 255, 225), IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 255),
					ImGuiEx::RectExpanded(thumbRect, -6.0f, -6.0f),
					ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			}

			ImGui::SetCursorScreenPos(ImVec2(infoRect.Min.x + textPadding, infoRect.Min.y + textPadding * 0.5f));
			ImGui::PushTextWrapPos(infoRect.Max.x - textPadding);

			if (m_IsRenaming)
			{
				ImGui::SetKeyboardFocusHere();
				ImGui::SetNextItemWidth(infoRect.GetWidth() - textPadding * 2.0f);
				ImGuiEx::InputText("##Rename", s_RenameBuffer, MAX_INPUT_BUFFER_LENGTH, ImGuiInputTextFlags_EnterReturnsTrue);

				if (ImGui::IsItemDeactivatedAfterEdit() || Input::IsKeyDown(KeyCode::Enter))
				{
					Rename(s_RenameBuffer);
					m_IsRenaming = false;
					result.Set(ContentBrowserAction::Renamed, true);
				}
			}
			else
			{
				const ImVec2 nameMin = ImGui::GetCursorScreenPos();
				ImGui::RenderTextEllipsis(drawList, nameMin, {infoRect.Max.x - textPadding, infoRect.Max.y},
										  infoRect.Max.x - textPadding, m_FileName.c_str(), nullptr, nullptr);
				ImGui::Dummy(
					{std::max(1.0f, thumbnailSize - textPadding * 2.0f), ImGui::GetTextLineHeight()});

				if (showAssetTypes && m_Type == ItemType::Asset)
				{
					const AssetType assetType = AssetManager::GetAssetType(m_ID);
					std::string assetTypeText = Utils::String::ToUpperCopy(std::string(AssetTypeToString(assetType)));
					ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
					ImGui::RenderTextEllipsis(
						drawList, ImGui::GetCursorScreenPos(), {infoRect.Max.x - textPadding, infoRect.Max.y},
						infoRect.Max.x - textPadding, assetTypeText.c_str(), nullptr, nullptr);
				}
			}

			ImGui::PopTextWrapPos();
			ImGui::SetCursorScreenPos(itemRect.Max);
			ImGui::Dummy({0.0f, 0.0f});
		}

		if (!m_IsRenaming && context->IsFocused() && isSelected && Input::IsKeyDown(KeyCode::F2))
			result.Set(ContentBrowserAction::StartRenaming, true);

		UpdateDrop(result, context);

		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			m_IsDragging = true;

			if (!context->IsItemSelected(m_ID))
			{
				context->ClearSelections();
				context->SelectItem(m_ID);
			}

			for (size_t selectionIndex = 0; selectionIndex < context->GetSelectionCount(); selectionIndex++)
			{
				AssetHandle selectedHandle = context->GetSelection(selectionIndex);
				size_t selectedIndex = context->GetCurrentItems().FindItem(selectedHandle);
				if (selectedIndex == ContentBrowserItemList::InvalidItem)
					continue;

				const Ref<ContentBrowserItem>& item = context->GetCurrentItems()[selectedIndex];
				ImGui::TextUnformatted(item->GetName().c_str());
			}

			ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", context->GetSelectionData(), sizeof(AssetHandle) * context->GetSelectionCount());
			ImGui::EndDragDropSource();
		}
		else
		{
			m_IsDragging = false;
		}

		if (hovered)
		{
			if (!m_IsRenaming && !m_IsDragging)
				ImGui::SetTooltip("%s\n%s", m_FileName.c_str(), m_PathLabel.c_str());
			result.Set(ContentBrowserAction::Hovered, true);

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !m_IsRenaming)
			{
				result.Set(ContentBrowserAction::Activated, true);
			}
			else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const bool control = Input::IsKeyDown(KeyCode::LeftControl) || Input::IsKeyDown(KeyCode::RightControl);
				const bool shift = Input::IsKeyDown(KeyCode::LeftShift) || Input::IsKeyDown(KeyCode::RightShift);

				if (shift && context->GetSelectionCount() > 0)
				{
					result.Set(ContentBrowserAction::SelectToHere, true);
				}
				else if (control)
				{
					result.Set(isSelected ? ContentBrowserAction::Deselected : ContentBrowserAction::Selected, true);
				}
				else
				{
					if (!isSelected || context->GetSelectionCount() > 1)
						result.Set(ContentBrowserAction::ClearSelections, true);
					result.Set(ContentBrowserAction::Selected, true);
				}
			}
		}

		if (ImGui::BeginPopupContextItem("CBItemContextMenu"))
		{
			if (!isSelected)
			{
				result.Set(ContentBrowserAction::ClearSelections, true);
				result.Set(ContentBrowserAction::Selected, true);
			}

			OnContextMenuOpen(result, context);
			ImGui::EndPopup();
		}

		return result;
	}

	void ContentBrowserItem::OnRenderEnd()
	{
		ImGui::EndGroup();
		ImGui::NextColumn();
		ImGui::PopID();
	}

	void ContentBrowserItem::StartRenaming()
	{
		if (m_IsRenaming)
			return;

		memset(s_RenameBuffer, 0, MAX_INPUT_BUFFER_LENGTH);
		memcpy(s_RenameBuffer, m_FileName.c_str(), std::min<size_t>(m_FileName.size(), MAX_INPUT_BUFFER_LENGTH - 1));
		m_IsRenaming = true;
	}

	void ContentBrowserItem::StopRenaming()
	{
		m_IsRenaming = false;
		memset(s_RenameBuffer, 0, MAX_INPUT_BUFFER_LENGTH);
	}

	void ContentBrowserItem::Rename(const std::string& newName)
	{
		if (!newName.empty())
			OnRenamed(newName);
	}

	void ContentBrowserItem::OnContextMenuOpen(CBItemActionResult& actionResult, ContentBrowserPanel* context)
	{
		if (m_Type == ItemType::Asset && ImGui::MenuItem("Reload"))
			actionResult.Set(ContentBrowserAction::Reload, true);

		if (context->GetSelectionCount() == 1 && ImGui::MenuItem("Rename"))
			actionResult.Set(ContentBrowserAction::StartRenaming, true);

		if (ImGui::MenuItem("Copy"))
			actionResult.Set(ContentBrowserAction::Copy, true);

		if (ImGui::MenuItem("Duplicate"))
			actionResult.Set(ContentBrowserAction::Duplicate, true);

		if (m_Type == ItemType::Asset && AssetManager::GetAssetType(m_ID) == AssetType::Prefab
			&& ImGui::MenuItem("Create Variant"))
			actionResult.Set(ContentBrowserAction::CreateVariant, true);

		// With several clips selected, one Random Sound plays a different one each time.
		if (m_Type == ItemType::Asset && AssetManager::GetAssetType(m_ID) == AssetType::Audio)
		{
			const size_t clips = ContentBrowserPanel::Get().GetSoundClips(m_ID).size();
			const std::string label = clips > 1 ? std::format("Create Random Sound ({} clips)###create_sound", clips) : std::string("Create Sound###create_sound");
			if (ImGui::MenuItem(label.c_str()))
				actionResult.Set(ContentBrowserAction::CreateSound, true);
		}
		if (m_Type == ItemType::Asset && AssetManager::GetAssetType(m_ID) == AssetType::Material
			&& ImGui::MenuItem("Create Instance"))
			actionResult.Set(ContentBrowserAction::CreateVariant, true);

		if (ImGui::MenuItem("Delete"))
			actionResult.Set(ContentBrowserAction::OpenDeleteDialogue, true);

		ImGui::Separator();

		if (ImGui::MenuItem("Show In Explorer"))
			actionResult.Set(ContentBrowserAction::ShowInExplorer, true);

		if (ImGui::MenuItem("Open Externally"))
			actionResult.Set(ContentBrowserAction::OpenExternal, true);

		RenderCustomContextItems();
	}

	ContentBrowserDirectory::ContentBrowserDirectory(const Ref<DirectoryInfo>& directoryInfo)
		: ContentBrowserItem(ItemType::Directory, directoryInfo->Handle,
							 Utils::PathToUTF8(directoryInfo->FilePath.filename()),
							 EditorResources::FolderIcon, Utils::PathToUTF8(directoryInfo->FilePath)),
		  m_DirectoryInfo(directoryInfo)
	{
		if (m_FileName.empty())
			m_FileName = Project::GetActive()->GetConfig().AssetDirectory.string();
	}

	void ContentBrowserDirectory::Delete()
	{
		ContentBrowserPanel::Get().DeleteDirectory(m_DirectoryInfo);
	}

	bool ContentBrowserDirectory::Move(const std::filesystem::path& destination)
	{
		return ContentBrowserPanel::Get().MoveDirectory(m_DirectoryInfo, destination);
	}

	void ContentBrowserDirectory::OnRenamed(const std::string& newName)
	{
		if (ContentBrowserPanel::Get().RenameDirectory(m_DirectoryInfo, newName))
			m_FileName = newName;
	}

	void ContentBrowserDirectory::UpdateDrop(CBItemActionResult& actionResult, ContentBrowserPanel* context)
	{
		if (context->IsItemSelected(m_ID))
			return;

		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM");
			if (payload)
			{
				if (context->MoveSelectionTo(m_DirectoryInfo->FilePath))
					actionResult.Set(ContentBrowserAction::Refresh, true);
			}

			ImGui::EndDragDropTarget();
		}
	}

	ContentBrowserAsset::ContentBrowserAsset(const AssetMetadata& assetInfo, const Ref<Texture2D>& icon)
		: ContentBrowserItem(ItemType::Asset, assetInfo.Handle, Utils::PathToUTF8(assetInfo.FilePath.stem()),
							 icon, Utils::PathToUTF8(assetInfo.FilePath)),
		  m_AssetInfo(assetInfo)
	{
	}

	void ContentBrowserAsset::Delete()
	{
		ContentBrowserPanel::Get().DeleteAsset(m_AssetInfo.Handle);
	}

	bool ContentBrowserAsset::Move(const std::filesystem::path& destination)
	{
		return ContentBrowserPanel::Get().MoveAsset(m_AssetInfo.Handle, destination);
	}

	void ContentBrowserAsset::OnRenamed(const std::string& newName)
	{
		if (ContentBrowserPanel::Get().RenameAsset(m_AssetInfo.Handle, newName))
			m_FileName = newName;
	}

}
