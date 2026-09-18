#include "lpch.h"
#include "AudioFileUtils.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"

#include "AudioEngine.h"
#include <fmod.hpp>
#include <fmod_errors.h>

namespace Lux::AudioFileUtils
{
	std::optional<AudioFileInfo> GetFileInfo(const AssetMetadata& metadata)
	{
		return GetFileInfo(std::filesystem::path(Project::GetEditorAssetManager()->GetFileSystemPathString(metadata)));
	}

	std::optional<AudioFileInfo> GetFileInfo(const std::filesystem::path& filepath)
	{
		auto* engine = AudioEngine::GetEngine();
		if (!engine)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot inspect audio file before FMOD initialization: {0}", filepath.string());
			return {};
		}
		FMOD::Sound* sound = nullptr;
		FMOD_RESULT result = engine->createSound(filepath.string().c_str(), FMOD_OPENONLY, nullptr, &sound);
		if (result != FMOD_OK)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot inspect '{0}': {1}", filepath.string(), FMOD_ErrorString(result));
			return {};
		}
		float sampleRate = 0.0f;
		int channels = 0, bits = 0;
		unsigned int frames = 0;
		result = sound->getDefaults(&sampleRate, nullptr);
		if (result == FMOD_OK)
			result = sound->getFormat(nullptr, nullptr, &channels, &bits);
		if (result == FMOD_OK)
			result = sound->getLength(&frames, FMOD_TIMEUNIT_PCM);
		const FMOD_RESULT released = sound->release();
		std::error_code ec;
		const auto size = std::filesystem::file_size(filepath, ec);
		if (released != FMOD_OK)
			LUX_CORE_ERROR_TAG("Audio", "Cannot release audio metadata reader: {0}", FMOD_ErrorString(released));
		if (result != FMOD_OK || sampleRate <= 0 || ec)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot read audio metadata for '{0}': {1}; {2}", filepath.string(), FMOD_ErrorString(result), ec.message());
			return {};
		}
		return AudioFileInfo{ frames / static_cast<double>(sampleRate), static_cast<uint32_t>(sampleRate),
			static_cast<uint16_t>(bits), static_cast<uint16_t>(channels), size };
	}

	bool IsValidAudioFile(const std::filesystem::path& filepath)
	{
		// TODO: 1. get list of supported formats; 2. optionally open the file and read metadata

		if (Utils::String::EqualsIgnoreCase(filepath.extension().string(), ".wav"))
			return true;
		else if (Utils::String::EqualsIgnoreCase(filepath.extension().string(), ".ogg"))
			return true;

		return false;
	}

	std::string ChannelsToLayoutString(uint16_t numChannels)
	{
		// TODO: should be able to get actual channel layout from some file formats
		//		and should add a function to get the output speaker layout of the audio device

		std::string str;
		switch (numChannels)
		{
		case 1: str = "Mono"; break;
		case 2: str = "Stereo"; break;
		case 3: str = "3.0"; break;
		case 4: str = "Quad"; break;
		case 5: str = "5.0"; break;
		case 6: str = "5.1"; break;
		//case 7: str = "Unknown"; break;
		case 8: str = "7.1"; break;
		default: str = "Unknown layout"; break;
		}

		return str;
	}
}

