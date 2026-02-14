#pragma once

#include "Lux/Core/UUID.h"
#include "Lux/Core/Ref.h"

#include <string_view>

#include "AssetTypes.h"


namespace Lux {

	using AssetHandle = UUID;

	std::string_view AssetTypeToString(AssetType type);
	AssetType AssetTypeFromString(std::string_view assetType);

	class Asset : public RefCounted
	{
	public:
		AssetHandle Handle = 0;

		virtual ~Asset() {}

		static AssetType GetStaticType() { return AssetType::None; }
		virtual AssetType GetAssetType() const { return AssetType::None; }

		virtual void OnDependencyUpdated(AssetHandle handle) {}

		virtual bool operator==(const Asset& other) const
		{
			return Handle == other.Handle;
		}

		virtual bool operator!=(const Asset& other) const
		{
			return !(*this == other);
		}

	private:
		// If you want to find out whether assets are valid or missing, use AssetManager::IsAssetValid(handle), IsAssetMissing(handle)
		// This cleans up and removes inconsistencies from rest of the code.
		// You simply go AssetManager::GetAsset<Whatever>(handle), and so long as you get a non-null pointer back, you're good to go.
		// No IsValid(), IsFlagSet(AssetFlag::Missing) etc. etc. all throughout the code.
		friend class EditorAssetManager;
		friend class RuntimeAssetManager;
		friend class AssimpMeshImporter;
		friend class TextureSerializer;
	};

}
