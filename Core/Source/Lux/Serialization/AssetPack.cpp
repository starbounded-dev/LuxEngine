#include "lpch.h"
#include "AssetPack.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Platform.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Utilities/FileSystem.h"

namespace Lux
{
	namespace
	{
		static const AssetPackFile::AssetInfo* FindAssetInfo(const AssetPackFile::IndexTable& index, AssetHandle sceneHandle, AssetHandle assetHandle)
		{
			if (sceneHandle)
			{
				auto sceneIt = index.Scenes.find(sceneHandle);
				if (sceneIt != index.Scenes.end())
				{
					auto assetIt = sceneIt->second.Assets.find(assetHandle);
					if (assetIt != sceneIt->second.Assets.end())
						return &assetIt->second;
				}
			}

			for (const auto& [handle, sceneInfo] : index.Scenes)
			{
				auto assetIt = sceneInfo.Assets.find(assetHandle);
				if (assetIt != sceneInfo.Assets.end())
					return &assetIt->second;
			}

			return nullptr;
		}

		static bool IsSerializedByAssetPack(AssetType type)
		{
			switch (type)
			{
				case AssetType::Scene:
				case AssetType::Texture:
				case AssetType::EnvMap:
				case AssetType::MeshSource:
				case AssetType::Mesh:
				case AssetType::StaticMesh:
				case AssetType::Material:
					return true;
				default:
					return false;
			}
		}

		static void AddIfValid(std::unordered_set<AssetHandle>& assets, AssetHandle handle)
		{
			if (handle && AssetManager::IsAssetHandleValid(handle))
				assets.insert(handle);
		}

		static std::unordered_set<AssetHandle> CollectSceneAssetList(Ref<Scene> scene)
		{
			std::unordered_set<AssetHandle> assets;

			for (auto entityID : scene->GetAllEntitiesWith<IDComponent>())
			{
				Entity entity((entt::entity)entityID, scene.get());
				if (!entity)
					continue;

				if (entity.HasComponent<SpriteRendererComponent>())
					AddIfValid(assets, entity.GetComponent<SpriteRendererComponent>().Texture);

				if (entity.HasComponent<TextComponent>())
					AddIfValid(assets, entity.GetComponent<TextComponent>().FontHandle);

				if (entity.HasComponent<AudioSourceComponent>())
				{
					const auto& audio = entity.GetComponent<AudioSourceComponent>();
					AddIfValid(assets, audio.Audio);
					for (AssetHandle playlistHandle : audio.AudioSourceData.Playlist)
						AddIfValid(assets, playlistHandle);
				}

				if (entity.HasComponent<MeshComponent>())
				{
					const auto& mesh = entity.GetComponent<MeshComponent>();
					AddIfValid(assets, mesh.Mesh);
					AddIfValid(assets, mesh.MaterialTable);
				}

				if (entity.HasComponent<StaticMeshComponent>())
				{
					const auto& mesh = entity.GetComponent<StaticMeshComponent>();
					AddIfValid(assets, mesh.Mesh);
					AddIfValid(assets, mesh.MaterialTable);
				}

				if (entity.HasComponent<SkyLightComponent>())
					AddIfValid(assets, entity.GetComponent<SkyLightComponent>().EnvironmentMap);

				if (entity.HasComponent<PrefabComponent>())
					AddIfValid(assets, entity.GetComponent<PrefabComponent>().PrefabID);
			}

			return assets;
		}
	}

	AssetPack::AssetPack(const std::filesystem::path& path)
		: m_Path(path)
	{
	}

	void AssetPack::AddAsset(Ref<Asset> asset)
	{
		if (!asset)
			return;

		m_AssetHandleIndex.insert(asset->Handle);
	}

	void AssetPack::Serialize()
	{
	}

	Ref<Scene> AssetPack::LoadScene(AssetHandle sceneHandle)
	{
		auto it = m_File.Index.Scenes.find(sceneHandle);
		if (it == m_File.Index.Scenes.end())
			return nullptr;

		FileStreamReader stream(m_Path);
		Ref<Scene> scene = AssetImporter::DeserializeSceneFromAssetPack(stream, it->second);
		if (scene)
			scene->Handle = sceneHandle;
		return scene;
	}

	Ref<Asset> AssetPack::LoadAsset(AssetHandle sceneHandle, AssetHandle assetHandle)
	{
		const AssetPackFile::AssetInfo* assetInfo = FindAssetInfo(m_File.Index, sceneHandle, assetHandle);
		if (!assetInfo)
			return nullptr;

		FileStreamReader stream(m_Path);
		Ref<Asset> asset = AssetImporter::DeserializeFromAssetPack(stream, *assetInfo);
		if (asset)
			asset->Handle = assetHandle;
		return asset;
	}

	bool AssetPack::IsAssetHandleValid(AssetHandle assetHandle) const
	{
		return m_AssetHandleIndex.find(assetHandle) != m_AssetHandleIndex.end();
	}

	bool AssetPack::IsAssetHandleValid(AssetHandle sceneHandle, AssetHandle assetHandle) const
	{
		auto sceneIterator = m_File.Index.Scenes.find(sceneHandle);
		if (sceneIterator == m_File.Index.Scenes.end())
			return false;

		return sceneIterator->second.Assets.find(assetHandle) != sceneIterator->second.Assets.end();
	}

	Buffer AssetPack::ReadAppBinary()
	{
		FileStreamReader stream(m_Path);
		stream.SetStreamPosition(m_File.Index.PackedAppBinaryOffset);
		Buffer buffer;
		stream.ReadBuffer(buffer);
		LUX_CORE_VERIFY(m_File.Index.PackedAppBinarySize == (buffer.Size + sizeof(uint64_t)));
		return buffer;
	}

	uint64_t AssetPack::GetBuildVersion()
	{
		return m_File.Header.BuildVersion;
	}

	AssetType AssetPack::GetAssetType(AssetHandle sceneHandle, AssetHandle assetHandle) const
	{
		const AssetPackFile::AssetInfo* assetInfo = FindAssetInfo(m_File.Index, sceneHandle, assetHandle);
		return assetInfo ? (AssetType)assetInfo->Type : AssetType::None;
	}

	Ref<AssetPack> AssetPack::CreateFromActiveProject(std::atomic<float>& progress)
	{
		AssetPackFile assetPackFile;
		assetPackFile.Header.BuildVersion = Platform::GetCurrentDateTimeU64();
		progress = 0.0f;

		const std::unordered_set<AssetHandle> sceneHandles = AssetManager::GetAllAssetsWithType<Scene>();
		const uint32_t sceneCount = (uint32_t)sceneHandles.size();
		if (sceneCount == 0)
		{
			LUX_CORE_ERROR("There are no scenes in the project. Nothing to serialize to asset pack.");
			return nullptr;
		}

		const float progressIncrement = 0.5f / (float)sceneCount;

		for (AssetHandle sceneHandle : sceneHandles)
		{
			const AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(sceneHandle);
			Ref<Scene> scene = Ref<Scene>::Create();
			SceneSerializer serializer(scene);

			if (!serializer.Deserialize(Project::GetActiveAssetDirectory() / metadata.FilePath))
			{
				LUX_CORE_ERROR("Failed to deserialize scene '{}' ({})", metadata.FilePath.string(), sceneHandle);
				continue;
			}

			std::unordered_set<AssetHandle> sceneAssetList = CollectSceneAssetList(scene);
			AssetPackFile::SceneInfo& sceneInfo = assetPackFile.Index.Scenes[sceneHandle];

			for (AssetHandle assetHandle : sceneAssetList)
			{
				const AssetType assetType = AssetManager::GetAssetType(assetHandle);
				if (!IsSerializedByAssetPack(assetType))
				{
					LUX_CORE_WARN("Skipping unsupported asset-pack type {} ({})", (uint16_t)assetType, (uint64_t)assetHandle);
					continue;
				}

				sceneInfo.Assets[assetHandle].Type = (uint16_t)assetType;
			}

			progress = progress + progressIncrement;
		}

		Buffer appBinary;
		if (std::filesystem::exists(Project::GetActiveScriptModuleFilePath()))
			appBinary = FileSystem::ReadBytes(Project::GetActiveScriptModuleFilePath());

		const std::filesystem::path packPath = Project::GetActiveAssetDirectory() / "AssetPack.lap";
		AssetPackSerializer::Serialize(packPath, assetPackFile, appBinary, progress);
		progress = 1.0f;

		return Load(packPath);
	}

	Ref<AssetPack> AssetPack::LoadActiveProject()
	{
		return Load(Project::GetActiveAssetDirectory() / "AssetPack.lap");
	}

	Ref<AssetPack> AssetPack::Load(const std::filesystem::path& path)
	{
		Ref<AssetPack> assetPack = Ref<AssetPack>::Create();
		assetPack->m_Path = path;

		const bool success = AssetPackSerializer::DeserializeIndex(assetPack->m_Path, assetPack->m_File);
		LUX_CORE_VERIFY(success);
		if (!success)
			return nullptr;

		for (const auto& [sceneHandle, sceneInfo] : assetPack->m_File.Index.Scenes)
		{
			assetPack->m_AssetHandleIndex.insert(sceneHandle);
			for (const auto& [assetHandle, assetInfo] : sceneInfo.Assets)
				assetPack->m_AssetHandleIndex.insert(assetHandle);
		}

		return assetPack;
	}
}
