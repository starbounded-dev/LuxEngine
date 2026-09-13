#include "lpch.h"
#include "AssetManager.h"
#include "AssetImporter.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/UI/Font.h"

namespace Lux
{
	namespace
	{
		std::unordered_map<AssetType, std::function<Ref<Asset>()>> s_AssetPlaceholderTable =
		{
			{ AssetType::Texture, []() { return Renderer::GetWhiteTexture(); } },
			{ AssetType::EnvMap, []() { return Renderer::GetEmptyEnvironment(); } },
			{ AssetType::Font, []() { return Font::GetDefaultFont(); } }
		};
	}

	AssetHandle AssetManager::ImportAsset(const std::filesystem::path& path)
	{
		auto* manager = dynamic_cast<EditorAssetManager*>(Project::GetAssetManager().Raw());
		if (!manager)
		{
			LUX_CORE_ERROR_TAG("AssetManager", "Asset import requires an editor project");
			return 0;
		}
		return manager->ImportAsset(path);
	}

	void AssetManager::SaveAsset(const Ref<Asset>& asset)
	{
		auto* manager = dynamic_cast<EditorAssetManager*>(Project::GetAssetManager().Raw());
		if (!manager || !asset || !manager->IsAssetHandleValid(asset->Handle))
		{
			LUX_CORE_ERROR_TAG("AssetManager", "Asset save requires a registered editor asset");
			return;
		}
		AssetImporter::Serialize(manager->GetMetadata(asset->Handle), asset);
	}

	Ref<Asset> AssetManager::GetPlaceholderAsset(AssetType type)
	{
		if (s_AssetPlaceholderTable.contains(type))
			return s_AssetPlaceholderTable.at(type)();

		return nullptr;
	}
}
