#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Editor/EditorPanel.h"
#include "Lux/Project/Project.h"

#include "ContentBrowser/ContentBrowserItem.h"
#include "ThumbnailCache.h"

#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace Lux {

	class SelectionStack
	{
	public:
		void CopyFrom(const SelectionStack& other)
		{
			m_Selections.assign(other.begin(), other.end());
		}

		void CopyFrom(const std::vector<AssetHandle>& other)
		{
			m_Selections.assign(other.begin(), other.end());
		}

		void Select(AssetHandle handle)
		{
			if (IsSelected(handle))
				return;

			m_Selections.push_back(handle);
		}

		void Deselect(AssetHandle handle)
		{
			for (auto it = m_Selections.begin(); it != m_Selections.end(); ++it)
			{
				if (*it == handle)
				{
					m_Selections.erase(it);
					return;
				}
			}
		}

		bool IsSelected(AssetHandle handle) const
		{
			for (AssetHandle selectedHandle : m_Selections)
			{
				if (selectedHandle == handle)
					return true;
			}

			return false;
		}

		void Clear()
		{
			m_Selections.clear();
		}

		size_t SelectionCount() const { return m_Selections.size(); }
		const AssetHandle* SelectionData() const { return m_Selections.data(); }

		AssetHandle operator[](size_t index) const
		{
			LUX_CORE_ASSERT(index < m_Selections.size());
			return m_Selections[index];
		}

		std::vector<AssetHandle>::iterator begin() { return m_Selections.begin(); }
		std::vector<AssetHandle>::const_iterator begin() const { return m_Selections.begin(); }
		std::vector<AssetHandle>::iterator end() { return m_Selections.end(); }
		std::vector<AssetHandle>::const_iterator end() const { return m_Selections.end(); }

	private:
		std::vector<AssetHandle> m_Selections;
	};

	struct ContentBrowserItemList
	{
		static constexpr size_t InvalidItem = std::numeric_limits<size_t>::max();

		std::vector<Ref<ContentBrowserItem>> Items;

		std::vector<Ref<ContentBrowserItem>>::iterator begin() { return Items.begin(); }
		std::vector<Ref<ContentBrowserItem>>::iterator end() { return Items.end(); }
		std::vector<Ref<ContentBrowserItem>>::const_iterator begin() const { return Items.begin(); }
		std::vector<Ref<ContentBrowserItem>>::const_iterator end() const { return Items.end(); }

		Ref<ContentBrowserItem>& operator[](size_t index) { return Items[index]; }
		const Ref<ContentBrowserItem>& operator[](size_t index) const { return Items[index]; }

		ContentBrowserItemList() = default;

		ContentBrowserItemList(const ContentBrowserItemList& other)
			: Items(other.Items)
		{
		}

		ContentBrowserItemList& operator=(const ContentBrowserItemList& other)
		{
			Items = other.Items;
			return *this;
		}

		void Clear()
		{
			std::scoped_lock<std::mutex> lock(Mutex);
			Items.clear();
		}

		void erase(AssetHandle handle)
		{
			size_t index = FindItem(handle);
			if (index == InvalidItem)
				return;

			std::scoped_lock<std::mutex> lock(Mutex);
			Items.erase(Items.begin() + static_cast<int64_t>(index));
		}

		size_t FindItem(AssetHandle handle) const
		{
			std::scoped_lock<std::mutex> lock(Mutex);
			for (size_t i = 0; i < Items.size(); i++)
			{
				if (Items[i]->GetID() == handle)
					return i;
			}

			return InvalidItem;
		}

		mutable std::mutex Mutex;
	};

	class ContentBrowserPanel : public EditorPanel
	{
	public:
		ContentBrowserPanel();

		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void OnEvent(Event& e) override;
		virtual void OnProjectChanged(const Ref<Project>& project) override;

		ContentBrowserItemList& GetCurrentItems() { return m_CurrentItems; }
		Ref<DirectoryInfo> GetDirectory(const std::filesystem::path& filepath) const;
		Ref<ThumbnailCache> GetThumbnailCache() const { return m_ThumbnailCache; }

		void RegisterItemActivateCallbackForType(AssetType type, const std::function<void(const AssetMetadata&)>& callback)
		{
			m_ItemActivationCallbacks[type] = callback;
		}

		bool IsItemSelected(AssetHandle handle) const { return m_SelectedItems.IsSelected(handle); }
		size_t GetSelectionCount() const { return m_SelectedItems.SelectionCount(); }
		const AssetHandle* GetSelectionData() const { return m_SelectedItems.SelectionData(); }
		AssetHandle GetSelection(size_t index) const { return m_SelectedItems[index]; }
		AssetHandle GetSelectionAnchor() const { return m_SelectionAnchor; }

		void SelectItem(AssetHandle handle);
		void DeselectItem(AssetHandle handle);
		void ClearSelections();
		void SelectRangeTo(AssetHandle handle);
		void SetSelectionAnchor(AssetHandle handle) { m_SelectionAnchor = handle; }

		bool MoveSelectionTo(const std::filesystem::path& destination);

		bool DeleteDirectory(const Ref<DirectoryInfo>& directory);
		bool MoveDirectory(const Ref<DirectoryInfo>& directory, const std::filesystem::path& destination);
		bool RenameDirectory(const Ref<DirectoryInfo>& directory, const std::string& newName);

		bool DeleteAsset(AssetHandle handle);
		bool MoveAsset(AssetHandle handle, const std::filesystem::path& destination);
		bool RenameAsset(AssetHandle handle, const std::string& newName);

		Ref<Texture2D> GetItemThumbnail(AssetHandle handle);
		float GetThumbnailSize() const { return m_ThumbnailSize; }
		bool GetShowAssetTypes() const { return m_ShowAssetType; }
		bool IsFocused() const { return m_IsContentBrowserFocused; }
		void SetThumbnailSize(float size);
		void SetShowAssetTypes(bool show);

		static ContentBrowserPanel& Get()
		{
			LUX_CORE_ASSERT(s_Instance);
			return *s_Instance;
		}

	private:
		AssetHandle ProcessDirectory(const std::filesystem::path& directoryPath, const Ref<DirectoryInfo>& parent);
		void ChangeDirectory(const Ref<DirectoryInfo>& directory);
		void OnBrowseBack();
		void OnBrowseForward();

		void RenderDirectoryHierarchy(const Ref<DirectoryInfo>& directory);
		void RenderTopBar(float height);
		void RenderItems();
		void RenderBottomBar(float height);

		void Refresh();
		void UpdateInput();

		bool OnKeyPressedEvent(KeyPressedEvent& e);
		bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

		void PasteCopiedAssets();
		void RenderDeleteDialogue();
		void RemoveDirectory(Ref<DirectoryInfo> directory, bool removeFromParent = true);
		void UpdateDropArea(const Ref<DirectoryInfo>& target);
		void SortItemList();
		void LoadSettings();
		void SaveSettings() const;
		void StartRenamingItem(AssetHandle handle);
		void ActivateAsset(const AssetMetadata& metadata);

		bool CreateNewFolder();
		AssetHandle CreateSceneAsset();
		AssetHandle CreateMaterialAsset();

		ContentBrowserItemList Search(const std::string& query, const Ref<DirectoryInfo>& directoryInfo) const;
		Ref<Texture2D> GetAssetIcon(const AssetMetadata& metadata) const;

	private:
		Ref<Project> m_Project;
		Ref<ThumbnailCache> m_ThumbnailCache;

		std::map<std::string, Ref<Texture2D>> m_AssetIconMap;

		ContentBrowserItemList m_CurrentItems;
		Ref<DirectoryInfo> m_CurrentDirectory;
		Ref<DirectoryInfo> m_BaseDirectory;
		Ref<DirectoryInfo> m_NextDirectory;
		Ref<DirectoryInfo> m_PreviousDirectory;

		std::unordered_map<AssetHandle, Ref<DirectoryInfo>> m_Directories;
		std::unordered_map<AssetType, std::function<void(const AssetMetadata&)>> m_ItemActivationCallbacks;

		SelectionStack m_SelectedItems;
		SelectionStack m_CopiedAssets;
		AssetHandle m_SelectionAnchor = 0;

		char m_SearchBuffer[MAX_INPUT_BUFFER_LENGTH]{};
		std::vector<Ref<DirectoryInfo>> m_BreadCrumbData;
		bool m_UpdateNavigationPath = false;

		bool m_IsAnyItemHovered = false;
		bool m_IsContentBrowserHovered = false;
		bool m_IsContentBrowserFocused = false;
		bool m_OpenDeletePopup = false;
		bool m_FocusSearchWidget = false;

		float m_ThumbnailSize = 128.0f;
		bool m_ShowAssetType = true;

	private:
		static ContentBrowserPanel* s_Instance;
	};

}
