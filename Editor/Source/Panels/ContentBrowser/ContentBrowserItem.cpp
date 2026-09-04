#include "lpch.h"
#include "ContentBrowserItem.h"

#include "../ContentBrowserPanel.h"

#include "Lux/Asset/AssetManager.h"
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
	}

	ContentBrowserItem::ContentBrowserItem(ItemType type, AssetHandle id, const std::string& name, const Ref<Texture2D>& icon)
		: m_Type(type), m_ID(id), m_DisplayName(name), m_FileName(name), m_Icon(icon)
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

		const float thumbnailSize = context->GetThumbnailSize();
		const bool showAssetTypes = context->GetShowAssetTypes();
		const bool isSelected = context->IsItemSelected(m_ID);

		SetDisplayNameFromFileName(thumbnailSize);

		auto* drawList = ImGui::GetWindowDrawList();
		ImRect itemRect;
		bool hovered = false;

		if (context->IsListView())
		{
			// ---- Compact list row: small icon, name, type ----
			const float rowHeight = ImGui::GetFrameHeight() + 6.0f;
			const float rowWidth = ImGui::GetContentRegionAvail().x;

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

			Ref<Texture2D> displayTexture = m_Icon;
			if (m_Type == ItemType::Asset)
			{
				if (Ref<Texture2D> thumbnail = context->GetItemThumbnail(m_ID))
					displayTexture = thumbnail;
			}
			if (displayTexture)
			{
				ImGuiEx::DrawButtonImage(displayTexture,
					IM_COL32(255, 255, 255, 225), IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 255),
					ImRect(iconMin, ImVec2(iconMin.x + iconSize, iconMin.y + iconSize)),
					ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
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
					SetDisplayNameFromFileName(thumbnailSize);
					result.Set(ContentBrowserAction::Renamed, true);
				}
			}
			else
			{
				drawList->AddText(ImVec2(nameX, textY), isSelected ? Colors::Theme::accent : Colors::Theme::text, m_FileName.c_str());
				if (showAssetTypes && m_Type == ItemType::Asset)
				{
					const std::string typeText = std::string(AssetTypeToString(AssetManager::GetAssetType(m_ID)));
					const float tw = ImGui::CalcTextSize(typeText.c_str()).x;
					drawList->AddText(ImVec2(itemRect.Max.x - tw - 12.0f, textY), Colors::Theme::textDarker, typeText.c_str());
				}
			}

			ImGui::SetCursorScreenPos(itemRect.Max);
		}
		else
		{
			const float textPadding = 6.0f;
			const float typeLineHeight = showAssetTypes && m_Type == ItemType::Asset ? ImGui::GetTextLineHeight() + 2.0f : 0.0f;
			const float infoPanelHeight = ImGui::GetTextLineHeight() * 2.0f + typeLineHeight + textPadding;
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

			Ref<Texture2D> displayTexture = m_Icon;
			if (m_Type == ItemType::Asset)
			{
				if (Ref<Texture2D> thumbnail = context->GetItemThumbnail(m_ID))
					displayTexture = thumbnail;
			}
			if (displayTexture)
			{
				ImGuiEx::DrawButtonImage(displayTexture,
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
					SetDisplayNameFromFileName(thumbnailSize);
					result.Set(ContentBrowserAction::Renamed, true);
				}
			}
			else
			{
				ImGui::TextWrapped("%s", m_DisplayName.c_str());

				if (showAssetTypes && m_Type == ItemType::Asset)
				{
					const AssetType assetType = AssetManager::GetAssetType(m_ID);
					std::string assetTypeText = Utils::String::ToUpperCopy(std::string(AssetTypeToString(assetType)));
					ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
					ImGui::TextWrapped("%s", assetTypeText.c_str());
				}
			}

			ImGui::PopTextWrapPos();
			ImGui::SetCursorScreenPos(itemRect.Max);
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
		SetDisplayNameFromFileName(ContentBrowserPanel::Get().GetThumbnailSize());
	}

	void ContentBrowserItem::Rename(const std::string& newName)
	{
		if (!newName.empty())
			OnRenamed(newName);
	}

	void ContentBrowserItem::SetDisplayNameFromFileName(float thumbnailSize)
	{
		const int maxCharacters = static_cast<int>(0.00152587f * (thumbnailSize * thumbnailSize));
		if (static_cast<int>(m_FileName.size()) > maxCharacters && maxCharacters > 4)
			m_DisplayName = m_FileName.substr(0, static_cast<size_t>(maxCharacters)) + "...";
		else
			m_DisplayName = m_FileName;
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
		: ContentBrowserItem(ItemType::Directory, directoryInfo->Handle, directoryInfo->FilePath.filename().string(), EditorResources::FolderIcon), m_DirectoryInfo(directoryInfo)
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
		: ContentBrowserItem(ItemType::Asset, assetInfo.Handle, assetInfo.FilePath.stem().string(), icon), m_AssetInfo(assetInfo)
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
