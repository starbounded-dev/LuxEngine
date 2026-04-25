#include "lpch.h"
#include "AssetManager.h"

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

	Ref<Asset> AssetManager::GetPlaceholderAsset(AssetType type)
	{
		if (s_AssetPlaceholderTable.contains(type))
			return s_AssetPlaceholderTable.at(type)();

		return nullptr;
	}
}
