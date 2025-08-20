#pragma once

#include "Asset.h"
#include "AssetMetadata.h"

#include "StarEngine/Scene/Scene.h"
#include "StarEngine/Scripting/ScriptAsset.h"

namespace StarEngine {

	class SceneImporter
	{
	public:
		// AssetMetadata filepath is relative to project asset directory
		static Ref<Scene> ImportScene(AssetHandle handle, const AssetMetadata& metadata);

		// Load from filepath
		static Ref<Scene> LoadScene(const std::filesystem::path& path);

		static void SaveScene(Ref<Scene> scene, const std::filesystem::path& path);

		static Ref<ScriptFileAsset> ImportScript(AssetHandle handle, const AssetMetadata& metadata);

		static Ref<ScriptFileAsset> LoadScript(const std::filesystem::path& path);
	};

}
