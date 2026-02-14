#include "lpch.h"
#include "AudioImporter.h"

#include "Lux/Audio/AudioEngine.h"

#include "Lux/Project/Project.h"
#include "Lux/Scene/SceneSerializer.h"

namespace Lux {

	Ref<AudioSource> AudioImporter::ImportAudio(AssetHandle handle, const AssetMetadata& metadata)
	{
		LUX_PROFILE_FUNCTION("AudioImporter::ImportAudio");

		return LoadAudio(Project::GetActiveAssetDirectory() / metadata.FilePath);
	}

	Ref<AudioSource> AudioImporter::LoadAudio(const std::filesystem::path& path)
	{
		LUX_PROFILE_FUNCTION_COLOR("AudioImporter::LoadAudio", 0xD17F8A);
		Ref<AudioSource> audioSource = Ref<AudioSource>::Create();

		auto* engine = static_cast<ma_engine*>(AudioEngine::GetEngine());

		{
			LUX_PROFILE_SCOPE_COLOR("AudioImporter::LoadAudio Scope", 0x3C7F8A);

			const ma_result result = ma_sound_init_from_file(engine, path.string().c_str(), MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, audioSource->GetSound().get());
			if (result != MA_SUCCESS)
				LUX_CORE_ERROR("Failed to initialize sound: {}", path.string());
		}

		return audioSource;
	}
}
