#include "sepch.h"
#include "AssetManager.h"

#include "StarEngine/Asset/Asset.h"   // where AssetType is declared
#include <string_view>

namespace StarEngine {

	std::string_view AssetTypeToString(AssetType type)
	{
		using enum AssetType;
		switch (type)
		{
			case None:         return "None";
			case Texture:      return "Texture";
			case Audio:        return "Audio";
			case Mesh:         return "Mesh";
			case Material:     return "Material";
			case Scene:        return "Scene";
			case Script:       return "Script";
			case Font:         return "Font";
			case Prefab:       return "Prefab";
				// add any others your enum has
		}
		return "None";
	}

	AssetType AssetTypeFromString(std::string_view s)
	{
		// compare case-sensitively; adjust if you want case-insensitive
		if (s == "None")     return AssetType::None;
		if (s == "Texture")  return AssetType::Texture;
		if (s == "Audio")    return AssetType::Audio;
		if (s == "Mesh")     return AssetType::Mesh;
		if (s == "Material") return AssetType::Material;
		if (s == "Scene")    return AssetType::Scene;
		if (s == "Script")   return AssetType::Script;
		if (s == "Font")     return AssetType::Font;
		if (s == "Prefab")   return AssetType::Prefab;
		return AssetType::None;
	}

} // namespace StarEngine
