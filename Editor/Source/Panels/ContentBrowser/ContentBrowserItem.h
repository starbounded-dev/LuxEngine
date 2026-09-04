#pragma once

#include "Lux/Asset/AssetMetadata.h"
#include "Lux/Renderer/Texture.h"

#include <filesystem>
#include <map>

namespace Lux {

#define MAX_INPUT_BUFFER_LENGTH 128

	enum class ContentBrowserAction
	{
		None = 0,
		Refresh = BIT(0),
		ClearSelections = BIT(1),
		Selected = BIT(2),
		Deselected = BIT(3),
		Hovered = BIT(4),
		Renamed = BIT(5),
		OpenDeleteDialogue = BIT(6),
		SelectToHere = BIT(7),
		ShowInExplorer = BIT(8),
		OpenExternal = BIT(9),
		Reload = BIT(10),
		Copy = BIT(11),
		Duplicate = BIT(12),
		StartRenaming = BIT(13),
		Activated = BIT(14),
		CreateVariant = BIT(15)
	};

	struct CBItemActionResult
	{
		uint16_t Field = 0;

		void Set(ContentBrowserAction flag, bool value)
		{
			if (value)
				Field |= static_cast<uint16_t>(flag);
			else
				Field &= ~static_cast<uint16_t>(flag);
		}

		bool IsSet(ContentBrowserAction flag) const
		{
			return (Field & static_cast<uint16_t>(flag)) != 0;
		}
	};

	class ContentBrowserPanel;

	class ContentBrowserItem : public RefCounted
	{
	public:
		enum class ItemType : uint16_t
		{
			Directory, Asset
		};

	public:
		ContentBrowserItem(ItemType type, AssetHandle id, const std::string& name, const Ref<Texture2D>& icon);
		virtual ~ContentBrowserItem() = default;

		void OnRenderBegin();
		CBItemActionResult OnRender(ContentBrowserPanel* context);
		void OnRenderEnd();

		virtual void Delete() {}
		virtual bool Move(const std::filesystem::path& destination) { return false; }

		AssetHandle GetID() const { return m_ID; }
		ItemType GetType() const { return m_Type; }
		const std::string& GetName() const { return m_FileName; }
		Ref<Texture2D> GetIcon() const { return m_Icon; }

		void StartRenaming();
		void StopRenaming();
		bool IsRenaming() const { return m_IsRenaming; }

		void Rename(const std::string& newName);
		void SetDisplayNameFromFileName(float thumbnailSize);

	protected:
		virtual void OnRenamed(const std::string& newName) { m_FileName = newName; }
		virtual void RenderCustomContextItems() {}
		virtual void UpdateDrop(CBItemActionResult& actionResult, ContentBrowserPanel* context) {}

		void OnContextMenuOpen(CBItemActionResult& actionResult, ContentBrowserPanel* context);

	protected:
		ItemType m_Type;
		AssetHandle m_ID;
		std::string m_DisplayName;
		std::string m_FileName;
		Ref<Texture2D> m_Icon;

		bool m_IsRenaming = false;
		bool m_IsDragging = false;

	private:
		friend class ContentBrowserPanel;
	};

	struct DirectoryInfo : public RefCounted
	{
		AssetHandle Handle = AssetHandle();
		Ref<DirectoryInfo> Parent;

		std::filesystem::path FilePath;
		std::vector<AssetHandle> Assets;
		std::map<AssetHandle, Ref<DirectoryInfo>> SubDirectories;
	};

	class ContentBrowserDirectory : public ContentBrowserItem
	{
	public:
		explicit ContentBrowserDirectory(const Ref<DirectoryInfo>& directoryInfo);
		virtual ~ContentBrowserDirectory() = default;

		Ref<DirectoryInfo> GetDirectoryInfo() const { return m_DirectoryInfo; }

		virtual void Delete() override;
		virtual bool Move(const std::filesystem::path& destination) override;

	private:
		virtual void OnRenamed(const std::string& newName) override;
		virtual void UpdateDrop(CBItemActionResult& actionResult, ContentBrowserPanel* context) override;

	private:
		Ref<DirectoryInfo> m_DirectoryInfo;
	};

	class ContentBrowserAsset : public ContentBrowserItem
	{
	public:
		ContentBrowserAsset(const AssetMetadata& assetInfo, const Ref<Texture2D>& icon);
		virtual ~ContentBrowserAsset() = default;

		const AssetMetadata& GetAssetInfo() const { return m_AssetInfo; }

		virtual void Delete() override;
		virtual bool Move(const std::filesystem::path& destination) override;

	private:
		virtual void OnRenamed(const std::string& newName) override;

	private:
		AssetMetadata m_AssetInfo;
	};

}
