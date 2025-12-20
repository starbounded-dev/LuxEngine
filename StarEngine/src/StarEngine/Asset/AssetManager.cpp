#include "sepch.h"
#include "AssetManager.h"

#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Renderer/Font.h"

namespace StarEngine {

	static std::unordered_map<AssetType, std::function<Ref<Asset>()>> s_AssetPlaceholderTable =
	{
		{ AssetType::Texture, []() { return Renderer::GetWhiteTexture(); }},
		//{ AssetType::EnvMap, []() { return Renderer::GetEmptyEnvironment(); }},
		{ AssetType::Font, []() { return Font::GetDefault(); }},
	};

	Ref<Asset> AssetManager::GetPlaceholderAsset(AssetType type)
	{
		if (s_AssetPlaceholderTable.contains(type))
			return s_AssetPlaceholderTable.at(type)();

		return nullptr;
	}
}
