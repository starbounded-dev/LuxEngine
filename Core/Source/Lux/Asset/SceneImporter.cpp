#include "lpch.h"
#include "SceneImporter.h"

#include "Lux/Project/Project.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Scripting/ScriptEngine.h"

#include <stb_image.h>

namespace Lux {

	Ref<Scene> SceneImporter::ImportScene(AssetHandle handle, const AssetMetadata& metadata)
	{
		LUX_PROFILE_FUNCTION("SceneImporter::ImportScene");

		return LoadScene(Project::GetActiveAssetDirectory() / metadata.FilePath);
	}

	Ref<Scene> SceneImporter::LoadScene(const std::filesystem::path& path)
	{
		LUX_PROFILE_FUNCTION("SceneImporter::LoadScene");

		Ref<Scene> scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		serializer.Deserialize(path);

		return scene;
	}

	void SceneImporter::SaveScene(Ref<Scene> scene, const std::filesystem::path& path)
	{
		SceneSerializer serializer(scene);
		serializer.Serialize(Project::GetActiveAssetDirectory() / path);
	}
}
