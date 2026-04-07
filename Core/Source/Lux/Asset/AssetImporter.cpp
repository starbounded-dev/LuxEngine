#include "lpch.h"
#include "AssetImporter.h"

#include "TextureImporter.h"
#include "SceneImporter.h"
#include "AudioImporter.h"
#include "TextureSerializer.h"
#include "MeshSerializer.h"
#include "MaterialSerializer.h"

#include <map>
#include <memory>

namespace Lux {

	// Serializer-based dispatch table (Hazel-style).
	// Each entry owns its AssetSerializer instance.
	static std::map<AssetType, std::unique_ptr<AssetSerializer>> s_Serializers;

	static void InitSerializers()
	{
		// Texture
		s_Serializers[AssetType::Texture]    = std::make_unique<TextureSerializer>();
		s_Serializers[AssetType::EnvMap]     = std::make_unique<TextureSerializer>();

		// Mesh
		s_Serializers[AssetType::MeshSource] = std::make_unique<MeshSourceSerializer>();
		s_Serializers[AssetType::Mesh]       = std::make_unique<MeshSerializer>();
		s_Serializers[AssetType::StaticMesh] = std::make_unique<StaticMeshSerializer>();

		// Material
		s_Serializers[AssetType::Material]   = std::make_unique<MaterialSerializer>();
	}

	Ref<Asset> AssetImporter::ImportAsset(AssetHandle handle, const AssetMetadata& metadata)
	{
		LUX_PROFILE_FUNCTION_COLOR("AssetImporter::ImportAsset", 0xF2FA8A);

		// Lazy-initialise the serializer table once.
		static std::once_flag s_InitFlag;
		std::call_once(s_InitFlag, InitSerializers);

		// ── Serializer-based types ────────────────────────────────────────────
		auto serializerIt = s_Serializers.find(metadata.Type);
		if (serializerIt != s_Serializers.end())
		{
			// Build a metadata copy that carries the handle (in case the
			// metadata came in without it already set).
			AssetMetadata meta = metadata;
			if (meta.Handle == 0)
				meta.Handle = handle;

			Ref<Asset> asset;
			if (serializerIt->second->TryLoadData(meta, asset))
				return asset;

			LUX_CORE_ERROR("AssetImporter: serializer failed for type {} (handle {})",
				(uint16_t)metadata.Type, (uint64_t)handle);
			return nullptr;
		}

		// ── Legacy function-pointer importers ─────────────────────────────────
		using AssetImportFunction = std::function<Ref<Asset>(AssetHandle, const AssetMetadata&)>;
		static const std::map<AssetType, AssetImportFunction> s_LegacyImportFunctions = {
			{ AssetType::Scene, SceneImporter::ImportScene },
			{ AssetType::Audio, AudioImporter::ImportAudio },
		};

		auto legacyIt = s_LegacyImportFunctions.find(metadata.Type);
		if (legacyIt == s_LegacyImportFunctions.end())
		{
			LUX_CORE_ERROR("AssetImporter: no importer for asset type {} (handle {})",
				(uint16_t)metadata.Type, (uint64_t)handle);
			return nullptr;
		}

		return legacyIt->second(handle, metadata);
	}

}
