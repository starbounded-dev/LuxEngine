#include "lpch.h"
#include "PrefabSerializer.h"

#include "Lux/Project/Project.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneSerializer.h"

namespace Lux
{
	void PrefabSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<Prefab> prefab = asset.As<Prefab>();
		LUX_CORE_ASSERT(prefab);
		if (!prefab || !prefab->GetScene())
			return;

		// A prefab file is just its sub-hierarchy serialized as a scene.
		SceneSerializer serializer(prefab->GetScene());
		serializer.Serialize(Project::GetEditorAssetManager()->GetFileSystemPath(metadata));
	}

	bool PrefabSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		Ref<Scene> scene = Ref<Scene>::Create();
		SceneSerializer serializer(scene);
		if (!serializer.Deserialize(Project::GetEditorAssetManager()->GetFileSystemPath(metadata)))
			return false;

		// The prefab root is the one parentless entity in the loaded hierarchy.
		Entity root = {};
		for (auto handle : scene->GetAllEntitiesWith<IDComponent>())
		{
			Entity entity{ handle, scene.Raw() };
			if (!entity.HasParent())
			{
				root = entity;
				break;
			}
		}

		if (!root)
		{
			LUX_CORE_ERROR_TAG("AssetManager", "Prefab '{}' has no root entity", metadata.FilePath.string());
			return false;
		}

		Ref<Prefab> prefab = Ref<Prefab>::Create();
		prefab->Handle = metadata.Handle;
		prefab->m_Scene = scene;
		prefab->m_Entity = root;

		asset = prefab;
		return true;
	}
}
