#pragma once

#include "Asset.h"
#include "AssetMetadata.h"

#include "StarEngine/Audio/AudioSource.h"

namespace StarEngine {

	class AudioImporter
	{
	public:
		// AssetMetadata filepath is relative to project asset directory
		static Ref<AudioSource> ImportAudio(AssetHandle handle, const AssetMetadata& metadata);

		// Load from filepath
		static Ref<AudioSource> LoadAudio(const std::filesystem::path& path);
	};

}
