#include "lpch.h"
#include "SceneAssetSerializer.h"

#include "Lux/Project/Project.h"
#include "Lux/Scene/SceneSerializer.h"

namespace Lux
{
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

	bool SceneAssetSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		const AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
		if (!metadata.IsValid())
			return false;

		Ref<Scene> scene = Ref<Scene>::Create();
		SceneSerializer serializer(scene);
		if (!serializer.Deserialize(Project::GetEditorAssetManager()->GetFileSystemPath(metadata)))
			return false;

		scene->Handle = handle;
		return serializer.SerializeToAssetPack(stream, outInfo);
	}

	Ref<Asset> SceneAssetSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		LUX_CORE_WARN("SceneAssetSerializer::DeserializeFromAssetPack is not used directly for scene assets");
		return nullptr;
	}

	Ref<Scene> SceneAssetSerializer::DeserializeSceneFromAssetPack(FileStreamReader& stream, const AssetPackFile::SceneInfo& sceneInfo) const
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		SceneSerializer serializer(scene);
		if (!serializer.DeserializeFromAssetPack(stream, sceneInfo))
			return nullptr;

		return scene;
	}
}
