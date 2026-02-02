#pragma once

#include "Asset.h"
#include "AssetMetadata.h"

#include "Lux/Audio/AudioSource.h"

namespace Lux {

	class AudioImporter
	{
	public:
		// AssetMetadata filepath is relative to project asset directory
		static Ref<AudioSource> ImportAudio(AssetHandle handle, const AssetMetadata& metadata);

		// Load from filepath
		static Ref<AudioSource> LoadAudio(const std::filesystem::path& path);
	};

}
