#pragma once

#include "Lux/Core/Assert.h"

namespace Lux {

	enum class AssetFlag : uint16_t
	{
		None = 0,
		Missing = BIT(0),
		Invalid = BIT(1)
	};

	enum class AssetType : uint16_t
	{
		None = 0,
		Scene,
		Prefab,
		Mesh,
		StaticMesh,
		MeshSource,
		Material,
		Texture,
		EnvMap,
		Audio,
		SoundConfig,
		SpatializationConfig,
		Font,
		Script,
		ScriptFile,
		MeshCollider,
		SoundGraphSound,
		Skeleton,
		Animation,
		AnimationGraph,
		// The FMOD Studio project that authors this game's audio (.fspro), and the banks it builds
		// (.bank). Both are opened in FMOD Studio rather than in an engine editor - they exist as
		// asset types so the Content Browser can show and activate them, not so they can be loaded.
		AudioProject,
		AudioBank,
		AudioSurfaceTable,
		DialogueTable
	};

	namespace Utils {

		inline AssetType AssetTypeFromString(std::string_view assetType)
		{
			if (assetType == "None")                return AssetType::None;
			if (assetType == "Scene")               return AssetType::Scene;
			if (assetType == "Prefab")              return AssetType::Prefab;
			if (assetType == "Mesh")                return AssetType::Mesh;
			if (assetType == "StaticMesh")          return AssetType::StaticMesh;
			if (assetType == "MeshAsset")           return AssetType::MeshSource; // DEPRECATED
			if (assetType == "MeshSource")          return AssetType::MeshSource;
			if (assetType == "Material")            return AssetType::Material;
			if (assetType == "Texture")             return AssetType::Texture;
			if (assetType == "EnvMap")              return AssetType::EnvMap;
			if (assetType == "Audio")               return AssetType::Audio;
			if (assetType == "SoundConfig")         return AssetType::SoundConfig;
			if (assetType == "SpatializationConfig") return AssetType::SpatializationConfig;
			if (assetType == "Font")                return AssetType::Font;
			if (assetType == "Script")              return AssetType::Script;
			if (assetType == "ScriptFile")          return AssetType::ScriptFile;
			if (assetType == "MeshCollider")        return AssetType::MeshCollider;
			if (assetType == "SoundGraphSound")     return AssetType::SoundGraphSound;
			if (assetType == "Skeleton")            return AssetType::Skeleton;
			if (assetType == "Animation")           return AssetType::Animation;
			if (assetType == "AnimationGraph")      return AssetType::AnimationGraph;
			if (assetType == "AudioProject")        return AssetType::AudioProject;
			if (assetType == "DialogueTable")       return AssetType::DialogueTable;
			if (assetType == "AudioSurfaceTable")   return AssetType::AudioSurfaceTable;
			if (assetType == "AudioBank")           return AssetType::AudioBank;

			return AssetType::None;
		}

		inline std::string_view AssetTypeToString(AssetType assetType)
		{
			switch (assetType)
			{
			case AssetType::None:                return "None";
			case AssetType::Scene:               return "Scene";
			case AssetType::Prefab:              return "Prefab";
			case AssetType::Mesh:                return "Mesh";
			case AssetType::StaticMesh:          return "StaticMesh";
			case AssetType::MeshSource:          return "MeshSource";
			case AssetType::Material:            return "Material";
			case AssetType::Texture:             return "Texture";
			case AssetType::EnvMap:              return "EnvMap";
			case AssetType::Audio:               return "Audio";
			case AssetType::SoundConfig:         return "SoundConfig";
			case AssetType::SpatializationConfig:return "SpatializationConfig";
			case AssetType::Font:                return "Font";
			case AssetType::Script:              return "Script";
			case AssetType::ScriptFile:          return "ScriptFile";
			case AssetType::MeshCollider:        return "MeshCollider";
			case AssetType::SoundGraphSound:     return "SoundGraphSound";
			case AssetType::Skeleton:            return "Skeleton";
			case AssetType::Animation:           return "Animation";
			case AssetType::AnimationGraph:      return "AnimationGraph";
			case AssetType::AudioProject:        return "AudioProject";
			case AssetType::DialogueTable:      return "DialogueTable";
			case AssetType::AudioSurfaceTable: return "AudioSurfaceTable";
			case AssetType::AudioBank:           return "AudioBank";
			}

			LUX_CORE_ASSERT(false, "Unknown Asset Type");
			return "None";
		}

	} // namespace Utils

	// Public wrappers in the Lux namespace to match declarations in Asset.h.
	// The implementation lives in Utils to keep things organized, but the
	// symbols expected by other translation units are in Lux::.
	inline AssetType AssetTypeFromString(std::string_view assetType)
	{
		return Utils::AssetTypeFromString(assetType);
	}

	inline std::string_view AssetTypeToString(AssetType assetType)
	{
		return Utils::AssetTypeToString(assetType);
	}

}
