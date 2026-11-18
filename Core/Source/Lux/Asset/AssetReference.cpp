// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AssetReference.h"

#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Project/Project.h"

namespace Lux::AssetReference::Detail
{
	namespace
	{
		Ref<EditorAssetManager> RegistryManager()
		{
			// The runtime's asset manager has no path registry.
			const auto manager = Project::GetAssetManager();
			if (!manager || !dynamic_cast<const EditorAssetManager*>(manager.Raw()))
				return {};
			return manager.As<EditorAssetManager>();
		}
	}

	AssetHandle LookupRegisteredHandle(const std::string& path)
	{
		const auto manager = RegistryManager();
		if (!manager)
			return 0;
		if (const AssetHandle handle = manager->GetAssetHandleFromPathKey(path))
			return handle;
		return manager->GetAssetHandleFromFilePath(std::filesystem::path(path).lexically_normal());
	}

	std::string LookupRegisteredPath(AssetHandle handle)
	{
		const auto manager = RegistryManager();
		if (!manager)
			return {};
		const AssetMetadata metadata = manager->GetMetadata(handle);
		return metadata.IsValid() ? metadata.FilePath.generic_string() : std::string();
	}
}
