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

		Ref<Scene> scene = LoadScene(Project::GetActiveAssetDirectory() / metadata.FilePath);
		if (scene)
			scene->Handle = handle;
		return scene;
	}

	Ref<Scene> SceneImporter::LoadScene(const std::filesystem::path& path)
	{
		LUX_PROFILE_FUNCTION("SceneImporter::LoadScene");

		Ref<Scene> scene = Ref<Scene>::Create();
		SceneSerializer serializer(scene);
		serializer.Deserialize(path);

		return scene;
	}

	void SceneImporter::SaveScene(Ref<Scene> scene, const std::filesystem::path& path)
	{
		SceneSerializer serializer(scene);
		serializer.Serialize(Project::GetActiveAssetDirectory() / path);
	}

	void SceneAssetSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<Scene> scene = asset.As<Scene>();
		LUX_CORE_ASSERT(scene);

		SceneSerializer serializer(scene);
		serializer.Serialize(Project::GetEditorAssetManager()->GetFileSystemPath(metadata));
	}

	bool SceneAssetSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		scene->Handle = metadata.Handle;
		scene->SetName(metadata.FilePath.stem().string());

		SceneSerializer serializer(scene);
		if (!serializer.Deserialize(Project::GetEditorAssetManager()->GetFileSystemPath(metadata)))
			return false;

		scene->Handle = metadata.Handle;
		if (scene->GetName().empty() || scene->GetName() == "Untitled")
			scene->SetName(metadata.FilePath.stem().string());

		asset = scene;
		return true;
	}
}
