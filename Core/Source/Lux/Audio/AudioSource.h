#pragma once

#include "Lux/Core/Ref.h"

#include "miniaudio.h"

#include <filesystem>
#include <string>

struct ma_sound;

namespace Lux {

	enum class AttenuationModelType
	{
		None = 0,
		Inverse,
		Linear,
		Exponential
	};

	struct AudioSourceConfig
	{
		float VolumeMultiplier = 1.0f;
		float PitchMultiplier = 1.0f;
		bool PlayOnAwake = true;
		bool Looping = false;

		bool Spatialization = false;
		AttenuationModelType AttenuationModel = AttenuationModelType::Inverse;
		float RollOff = 1.0f;
		float MinGain = 0.0f;
		float MaxGain = 1.0f;
		float MinDistance = 0.3f;
		float MaxDistance = 1000.0f;

		float ConeInnerAngle = glm::radians(360.0f);
		float ConeOuterAngle = glm::radians(360.0f);
		float ConeOuterGain = 0.0f;

		float DopplerFactor = 1.0f;
	};

	class AudioSource : public RefCounted
	{
	public:
		AudioSource();
		~AudioSource();

		std::unique_ptr<ma_sound>& GetSound() { return m_Sound; }
		bool LoadFromFile(const std::filesystem::path& filepath);
		bool IsLoaded() const { return m_IsLoaded; }
		const std::filesystem::path& GetFilePath() const { return m_FilePath; }

		void Play();
		void Pause();
		void UnPause();
		void Stop();
		bool IsPlaying();
		uint64_t GetCursorPosition();

		void SetConfig(const AudioSourceConfig& config);

		void SetVolume(float volume);
		void SetPitch(float pitch);
		bool IsLooping();
		void SetLooping(bool state);
		void SetSpatialization(bool state);
		void SetAttenuationModel(AttenuationModelType type);
		void SetRollOff(float rollOff);
		void SetMinGain(float minGain);
		void SetMaxGain(float maxGain);
		void SetMinDistance(float minDistance);
		void SetMaxDistance(float maxDistance);
		void SetCone(float innerAngle, float outerAngle, float outerGain);
		void SetDopplerFactor(float factor);

		void SetPosition(const glm::vec4& position);
		void SetDirection(const glm ::vec3& forward);
		void SetVelocity(const glm ::vec3& velocity);

	private:
		std::unique_ptr<ma_sound> m_Sound;
		std::filesystem::path m_FilePath;
		bool m_Spatialization = false;
		bool m_IsLoaded = false;
		uint64_t m_CursorPos = 0;
	};
}
