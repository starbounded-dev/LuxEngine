#include "sepch.h"
#include "SceneImporter.h"

#include "StarEngine/Project/Project.h"
#include "StarEngine/Scene/SceneSerializer.h"
#include "StarEngine/Scripting/ScriptEngine.h"

#include <stb_image.h>

namespace StarEngine {

	Ref<Scene> SceneImporter::ImportScene(AssetHandle handle, const AssetMetadata& metadata)
	{
		SE_PROFILE_FUNCTION("SceneImporter::ImportScene");

		return LoadScene(Project::GetActiveAssetDirectory() / metadata.FilePath);
	}

	Ref<Scene> SceneImporter::LoadScene(const std::filesystem::path& path)
	{
		SE_PROFILE_FUNCTION("SceneImporter::LoadScene");

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

	Ref<Script> SceneImporter::ImportScript(AssetHandle handle, const AssetMetadata& metadata)
	{
		return LoadScript(Project::GetActiveAssetDirectory() / metadata.FilePath);
	}

	Ref<Script> SceneImporter::LoadScript(const std::filesystem::path& path)
	{
		Ref<Script> result = std::make_shared<Script>();

		return result;
	}
}
