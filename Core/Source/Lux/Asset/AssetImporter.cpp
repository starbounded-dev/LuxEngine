#include "lpch.h"
#include "AssetImporter.h"

#include "AudioImporter.h"
#include "MaterialSerializer.h"
#include "MeshSerializer.h"
#include "SceneImporter.h"
#include "TextureSerializer.h"

#include "Lux/Project/Project.h"

#include <mutex>
#include <unordered_map>

namespace Lux
{
	namespace
	{
		using SerializerMap = std::unordered_map<AssetType, std::unique_ptr<AssetSerializer>>;
		SerializerMap s_Serializers;

		void InitSerializers()
		{
			s_Serializers.clear();
			s_Serializers[AssetType::Scene] = std::make_unique<SceneAssetSerializer>();
			s_Serializers[AssetType::Texture] = std::make_unique<TextureSerializer>();
			s_Serializers[AssetType::EnvMap] = std::make_unique<TextureSerializer>();
			s_Serializers[AssetType::MeshSource] = std::make_unique<MeshSourceSerializer>();
			s_Serializers[AssetType::Mesh] = std::make_unique<MeshSerializer>();
			s_Serializers[AssetType::StaticMesh] = std::make_unique<StaticMeshSerializer>();
			s_Serializers[AssetType::Material] = std::make_unique<MaterialSerializer>();
		}

		void EnsureInitialized()
		{
			static std::once_flag s_InitFlag;
			std::call_once(s_InitFlag, []
			{
				InitSerializers();
			});
		}
	}

	void AssetImporter::Init()
	{
		EnsureInitialized();
	}

	void AssetImporter::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset)
	{
		EnsureInitialized();

		auto serializerIt = s_Serializers.find(metadata.Type);
		if (serializerIt == s_Serializers.end())
		{
			LUX_CORE_WARN("AssetImporter: no serializer for asset type {}", (uint16_t)metadata.Type);
			return;
		}

		serializerIt->second->Serialize(metadata, asset);
	}

	void AssetImporter::Serialize(const Ref<Asset>& asset)
	{
		if (!asset || !Project::GetEditorAssetManager())
			return;

		Serialize(Project::GetEditorAssetManager()->GetMetadata(asset->Handle), asset);
	}

	bool AssetImporter::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
		EnsureInitialized();

		auto serializerIt = s_Serializers.find(metadata.Type);
		if (serializerIt != s_Serializers.end())
			return serializerIt->second->TryLoadData(metadata, asset);

		if (metadata.Type == AssetType::Audio)
		{
			asset = AudioImporter::ImportAudio(metadata.Handle, metadata);
			return asset != nullptr;
		}

		LUX_CORE_WARN("AssetImporter: no loader for asset type {}", (uint16_t)metadata.Type);
		return false;
	}

	void AssetImporter::RegisterDependencies(const AssetMetadata& metadata)
	{
		EnsureInitialized();

		auto serializerIt = s_Serializers.find(metadata.Type);
		if (serializerIt != s_Serializers.end())
			serializerIt->second->RegisterDependencies(metadata);
	}

	Ref<Asset> AssetImporter::ImportAsset(AssetHandle handle, const AssetMetadata& metadata)
	{
		AssetMetadata metadataWithHandle = metadata;
		if (!metadataWithHandle.Handle)
			metadataWithHandle.Handle = handle;

		Ref<Asset> asset;
		if (!TryLoadData(metadataWithHandle, asset))
			return nullptr;

		if (asset)
			asset->Handle = metadataWithHandle.Handle;

		return asset;
	}
}
