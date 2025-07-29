#include "sepch.h"
#include "AudioSource.h"

#include "AudioEngine.h"
#include "StarEngine/Asset/AudioImporter.h"
#include "StarEngine/Project/Project.h"

namespace StarEngine {

	AudioSource::AudioSource()
	{
		SE_PROFILE_FUNCTION("AudioSource::AudioSource");

		m_Sound = std::make_unique<ma_sound>();
	}

	AudioSource::~AudioSource()
	{
		SE_PROFILE_FUNCTION("AudioSource::~AudioSource");

		if (!AudioEngine::ShuttingDownEngine())
		{
			if (IsPlaying())
			{
				if (ma_sound_stop(m_Sound.get()) != MA_SUCCESS)
				{
					SE_CORE_ERROR("Failed to stop playback device!");
					ma_sound_uninit(m_Sound.get());
				}

				ma_sound_uninit(m_Sound.get());
				m_Sound = nullptr;
			}
		}
	}

	void AudioSource::Play()
	{
		SE_PROFILE_FUNCTION("AudioSource::Play");

		if (m_Sound)
		{
			ma_sound_start(m_Sound.get());
		}
	}

	void AudioSource::Pause()
	{
		SE_PROFILE_FUNCTION("AudioSource::Pause");

		if (m_Sound)
		{
			ma_sound_stop(m_Sound.get());
		}
	}

	void AudioSource::UnPause()
	{
		SE_PROFILE_FUNCTION("AudioSource::UnPause");

		if (m_Sound)
		{
			ma_sound_start(m_Sound.get());
		}
	}

	void AudioSource::Stop()
	{
		SE_PROFILE_FUNCTION("AudioSource::Stop");

		if (m_Sound)
		{
			ma_sound_stop(m_Sound.get());
			ma_sound_seek_to_pcm_frame(m_Sound.get(), 0);

			m_CursorPos = 0;
		}
	}

	bool AudioSource::IsPlaying()
	{
		SE_PROFILE_FUNCTION("AudioSource::IsPlaying");

		if (m_Sound.get())
			return ma_sound_is_playing(m_Sound.get());

		return false;
	}

	uint64_t AudioSource::GetCursorPosition()
	{
		if (m_Sound.get())
		{
			//uint64_t cursorPos = 0;
			ma_sound_get_cursor_in_pcm_frames(m_Sound.get(), &m_CursorPos);
			return m_CursorPos;
		}

		return 0;
	}

	static ma_attenuation_model GetAttenuationModel(AttenuationModelType model)
	{
		SE_PROFILE_FUNCTION("ma_attenuation_model GetAttenuationModel");

		switch (model)
		{
		case AttenuationModelType::None:		return ma_attenuation_model_none;
		case AttenuationModelType::Inverse:		return ma_attenuation_model_inverse;
		case AttenuationModelType::Linear:		return ma_attenuation_model_linear;
		case AttenuationModelType::Exponential: return ma_attenuation_model_exponential;
		}

		return ma_attenuation_model_none;
	}

	void AudioSource::SetConfig(AudioSourceConfig& config)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetConfig");

		if (m_Sound)
		{
			ma_sound* sound = m_Sound.get();
			ma_sound_set_volume(sound, config.VolumeMultiplier);
			ma_sound_set_pitch(sound, config.PitchMultiplier);

			if (sound)
			{
				if (config.Looping)
					ma_sound_set_looping(sound, MA_TRUE);
				else
					ma_sound_set_looping(sound, MA_FALSE);
			}

			if (m_Spatialization != config.Spatialization)
			{
				m_Spatialization = config.Spatialization;
				ma_sound_set_spatialization_enabled(sound, config.Spatialization);
			}

			if (config.Spatialization)
			{
				ma_sound_set_attenuation_model(sound, GetAttenuationModel(config.AttenuationModel));
				ma_sound_set_rolloff(sound, config.RollOff);
				ma_sound_set_min_gain(sound, config.MinGain);
				ma_sound_set_max_gain(sound, config.MaxGain);
				ma_sound_set_min_distance(sound, config.MinDistance);
				ma_sound_set_max_distance(sound, config.MaxDistance);

				ma_sound_set_cone(sound, config.ConeInnerAngle, config.ConeOuterAngle, config.ConeOuterGain);
				ma_sound_set_doppler_factor(sound, std::max(config.DopplerFactor, 0.0f));
			}
			else
			{
				ma_sound_set_attenuation_model(sound, ma_attenuation_model_none);
			}
		}
	}

	void AudioSource::SetVolume(float volume)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetVolume");

		if (m_Sound)
		{
			ma_sound_set_volume(m_Sound.get(), volume);
		}
	}

	void AudioSource::SetPitch(float pitch)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetPitch");

		if (m_Sound)
		{
			ma_sound_set_pitch(m_Sound.get(), pitch);
		}
	}

	bool AudioSource::IsLooping()
	{
		if (m_Sound)
			return ma_sound_is_looping(m_Sound.get());

		return false;
	}

	void AudioSource::SetLooping(bool state)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetLooping");

		if (m_Sound)
		{
			if (state)
				ma_sound_set_looping(m_Sound.get(), MA_TRUE);
			else
				ma_sound_set_looping(m_Sound.get(), MA_FALSE);
		}
	}

	void AudioSource::SetSpatialization(bool state)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetSpatialization");

		m_Spatialization = state;
		if (m_Sound)
		{
			ma_sound_set_spatialization_enabled(m_Sound.get(), state);
		}
	}

	void AudioSource::SetAttenuationModel(AttenuationModelType type)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetAttenuationModel");

		if (m_Sound)
		{
			if (m_Spatialization)
				ma_sound_set_attenuation_model(m_Sound.get(), GetAttenuationModel(type));
			else
				ma_sound_set_attenuation_model(m_Sound.get(), GetAttenuationModel(AttenuationModelType::None));
		}
	}

	void AudioSource::SetRollOff(float rollOff)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound)
		{
			ma_sound_set_rolloff(m_Sound.get(), rollOff);
		}
	}

	void AudioSource::SetMinGain(float minGain)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound)
		{
			ma_sound_set_min_gain(m_Sound.get(), minGain);
		}
	}

	void AudioSource::SetMaxGain(float maxGain)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound)
		{
			ma_sound_set_max_gain(m_Sound.get(), maxGain);
		}
	}

	void AudioSource::SetMinDistance(float minDistance)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound)
		{
			ma_sound_set_min_distance(m_Sound.get(), minDistance);
		}
	}

	void AudioSource::SetMaxDistance(float maxDistance)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound)
		{
			ma_sound_set_max_distance(m_Sound.get(), maxDistance);
		}
	}

	void AudioSource::SetCone(float innerAngle, float outerAngle, float outerGain)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound)
		{
			ma_sound_set_cone(m_Sound.get(), innerAngle, outerAngle, outerGain);
		}
	}

	void AudioSource::SetDopplerFactor(float factor)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetDopplerFactor");

		if (m_Sound)
		{
			ma_sound_set_doppler_factor(m_Sound.get(), std::max(factor, 0.0f));
		}
	}

	void AudioSource::SetPosition(const glm::vec4& position)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetPosition");

		if (m_Sound)
		{
			ma_sound_set_position(m_Sound.get(), position.x, position.y, position.z);
		}
	}

	void AudioSource::SetDirection(const glm::vec3& forward)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetDirection");

		if (m_Sound)
		{
			ma_sound_set_direction(m_Sound.get(), forward.x, forward.y, forward.z);
		}
	}

	void AudioSource::SetVelocity(const glm::vec3& velocity)
	{
		SE_PROFILE_FUNCTION("AudioSource::SetVelocity");

		if (m_Sound)
		{
			ma_sound_set_velocity(m_Sound.get(), velocity.x, velocity.y, velocity.z);
		}
	}

}
