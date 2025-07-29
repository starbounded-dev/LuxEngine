#include "sepch.h"
#include "AudioImporter.h"

#include "StarEngine/Audio/AudioEngine.h"

#include "StarEngine/Project/Project.h"
#include "StarEngine/Scene/SceneSerializer.h"

namespace StarEngine {

	Ref<AudioSource> AudioImporter::ImportAudio(AssetHandle handle, const AssetMetadata& metadata)
	{
		SE_PROFILE_FUNCTION("AudioImporter::ImportAudio");

		return LoadAudio(Project::GetActiveAssetDirectory() / metadata.FilePath);
	}

	Ref<AudioSource> AudioImporter::LoadAudio(const std::filesystem::path& path)
	{
		SE_PROFILE_FUNCTION_COLOR("AudioImporter::LoadAudio", 0xD17F8A);
		Ref<AudioSource> audioSource = CreateRef<AudioSource>();

		auto* engine = static_cast<ma_engine*>(AudioEngine::GetEngine());

		{
			SE_PROFILE_SCOPE_COLOR("AudioImporter::LoadAudio Scope", 0x3C7F8A);

			const ma_result result = ma_sound_init_from_file(engine, path.string().c_str(), MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, audioSource->GetSound().get());
			if (result != MA_SUCCESS)
				SE_CORE_ERROR("Failed to initialize sound: {}", path.string());
		}

		return audioSource;
	}
}
