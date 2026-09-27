// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "PrefabSerializer.h"

#include "Lux/Project/Project.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneSerializer.h"

#include "Lux/Serialization/Yaml.h"

#include <fstream>

namespace Lux
{
	void PrefabSerializer::WritePrefabFile(const std::filesystem::path& path, const Ref<Scene>& scene, AssetHandle basePrefab)
	{
		if (!scene)
			return;

		// A base prefab is a plain scene file — keep that format byte-for-byte.
		if (basePrefab == 0)
		{
			SceneSerializer(scene).Serialize(path);
			return;
		}

		// A variant adds one top-level BasePrefab key after the scene document. The document is a
		// top-level block map, so appending the key as the last line keeps the scene content exactly
		// as SceneSerializer wrote it.
		std::string sceneYaml = SceneSerializer(scene).SerializeToString();
		if (!sceneYaml.empty() && sceneYaml.back() != '\n')
			sceneYaml += '\n';
		sceneYaml += "BasePrefab: " + std::to_string(static_cast<uint64_t>(basePrefab)) + "\n";

		std::ofstream fout(path);
		fout << sceneYaml;
	}

	void PrefabSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		Ref<Prefab> prefab = asset.As<Prefab>();
		LUX_CORE_ASSERT(prefab);
		if (!prefab || !prefab->GetScene())
			return;

		WritePrefabFile(Project::GetEditorAssetManager()->GetFileSystemPath(metadata), prefab->GetScene(), prefab->GetBasePrefab());
	}

	bool PrefabSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		const std::filesystem::path path = Project::GetEditorAssetManager()->GetFileSystemPath(metadata);

		Ref<Scene> scene = Ref<Scene>::Create();
		SceneSerializer serializer(scene);
		if (!serializer.Deserialize(path))   // ignores the optional BasePrefab key
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

		// Variant link, if any (absent on plain/base prefabs → 0).
		AssetHandle basePrefab = 0;
		try
		{
			Yaml::Node node = Yaml::LoadFile(path);
			if (node["BasePrefab"])
				basePrefab = node["BasePrefab"].as<uint64_t>(0);
		}
		catch (const std::exception&)
		{
		}

		Ref<Prefab> prefab = Ref<Prefab>::Create();
		prefab->Handle = metadata.Handle;
		prefab->m_Scene = scene;
		prefab->m_Entity = root;
		prefab->SetBasePrefab(basePrefab);

		asset = prefab;
		return true;
	}
}
