// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SkeletalAssetExtraction.h"

#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Asset/SkeletalImporter.h"
#include "Lux/Project/Project.h"
#include "Lux/Serialization/FileStream.h"

#include <set>

namespace Lux
{
	namespace
	{
		std::string FileSafe(const std::string& name)
		{
			std::string safe;
			for (const char c : name)
				safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
			return safe.empty() ? "Animation" : safe;
		}

		template<typename T>
		bool WriteBlob(const Ref<T>& asset, const std::filesystem::path& path)
		{
			FileStreamWriter writer(path);
			asset->Serialize(&writer);
			return writer.IsStreamGood();
		}
	}

	bool ExtractSkeletalAssets(AssetHandle meshSource, SkeletalExtractionResult& result)
	{
		result = {};
		auto manager = Project::GetEditorAssetManager();
		if (!manager)
			return false;

		const AssetMetadata metadata = manager->GetMetadata(meshSource);
		if (!metadata.IsValid() || metadata.Type != AssetType::MeshSource)
		{
			LUX_CORE_ERROR("ExtractSkeletalAssets: asset is not a mesh source");
			return false;
		}

		const std::filesystem::path source = manager->GetFileSystemPath(metadata);
		Ref<Skeleton> skeleton;
		std::vector<Ref<Animation>> clips;
		if (!ImportSkeletonAndAnimations(source, skeleton, clips))
			return false;
		if (!skeleton)
		{
			LUX_CORE_WARN("ExtractSkeletalAssets: '{}' has no skeleton", source.filename().string());
			return false;
		}

		const std::filesystem::path stem = source.parent_path() / source.stem();
		const std::filesystem::path skeletonPath = stem.string() + ".lskel";
		if (!WriteBlob(skeleton, skeletonPath))
		{
			LUX_CORE_ERROR("ExtractSkeletalAssets: could not write '{}'", skeletonPath.string());
			return false;
		}
		result.Skeleton = manager->ImportAsset(skeletonPath);

		std::set<std::string> usedNames;
		for (Ref<Animation>& clip : clips)
		{
			std::string name = FileSafe(clip->GetName());
			for (int suffix = 2; !usedNames.insert(name).second; suffix++)
				name = FileSafe(clip->GetName()) + "_" + std::to_string(suffix);

			clip->SetSkeletonHandle(result.Skeleton);
			const std::filesystem::path clipPath = stem.string() + "_" + name + ".lanim";
			if (!WriteBlob(clip, clipPath))
			{
				LUX_CORE_ERROR("ExtractSkeletalAssets: could not write '{}'", clipPath.string());
				continue;
			}
			result.Clips.push_back(manager->ImportAsset(clipPath));
		}

		LUX_CORE_INFO("Extracted skeleton ({} bones) and {} animation(s) from '{}'", skeleton->GetNumBones(), result.Clips.size(), source.filename().string());
		return true;
	}
}
