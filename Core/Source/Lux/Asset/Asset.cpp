#include "sepch.h"
#include "Asset.h"

namespace StarEngine {

	std::string_view AssetTypeToString(AssetType type)
	{
		switch (type)
		{
		case AssetType::None:        return "None";
		case AssetType::Scene:       return "Scene";
		case AssetType::Texture2D:   return "Texture2D";
		case AssetType::Audio:       return "Audio";/*
		case AssetType::ObjModel:    return "ObjModel";
		case AssetType::ScriptFile:  return "ScriptFile";*/
		}

		return "<Invalid>";
	}

	AssetType AssetTypeFromString(std::string_view assetType)
	{
		if (assetType == "None")      return AssetType::None;
		if (assetType == "Scene")     return AssetType::Scene;
		if (assetType == "Texture2D") return AssetType::Texture2D;
		if (assetType == "Audio")     return AssetType::Audio;/*
		if (assetType == "ObjModel")  return AssetType::ObjModel;
		if (assetType == "ScriptFile")  return AssetType::ScriptFile;*/

		return AssetType::None;
	}

}
