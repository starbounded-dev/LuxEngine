#include "lpch.h"
#include "AssetImporter.h"

#include "TextureImporter.h"
#include "SceneImporter.h"
#include "AudioImporter.h"

#include <map>

namespace Lux {

	using AssetImportFunction = std::function<Ref<Asset>(AssetHandle, const AssetMetadata&)>;
	static std::map<AssetType, AssetImportFunction> s_AssetImportFunctions = {
		{AssetType::Texture, TextureImporter::ImportTexture },
		{ AssetType::Scene, SceneImporter::ImportScene },
		{ AssetType::Audio, AudioImporter::ImportAudio },/*
		{ AssetType::ObjModel, ObjModelImporter::ImportObjModel },
		{ AssetType::ScriptFile, SceneImporter::ImportScript }*/
	};

	Ref<Asset> AssetImporter::ImportAsset(AssetHandle handle, const AssetMetadata& metadata)
	{
		LUX_PROFILE_FUNCTION_COLOR("AssetImporter::ImportAsset", 0xF2FA8A);

		{
			LUX_PROFILE_SCOPE_COLOR("AssetImporter::ImportAsset Scope", 0x27628A);

			if (s_AssetImportFunctions.find(metadata.Type) == s_AssetImportFunctions.end())
			{
				LUX_CORE_ERROR("No importer available for asset type: {}", (uint16_t)metadata.Type);
				return nullptr;
			}
		}

		auto& result = s_AssetImportFunctions.at(metadata.Type);//(metadata.Type)(handle, metadata);

		{
			LUX_PROFILE_SCOPE_COLOR("AssetImporter::ImportAsset 2 Scope", 0xD1C48A);

			return result(handle, metadata);
		}

	}

}
