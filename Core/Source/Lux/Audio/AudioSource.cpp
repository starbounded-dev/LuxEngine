#include "lpch.h"
#include "AudioSource.h"

#include "AudioEngine.h"
#include "Lux/Project/Project.h"

namespace Lux {

	AudioSource::AudioSource()
	{
		LUX_PROFILE_FUNCTION("AudioSource::AudioSource");

		m_Sound = std::make_unique<ma_sound>();
	}

	AudioSource::~AudioSource()
	{
		LUX_PROFILE_FUNCTION("AudioSource::~AudioSource");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_stop(m_Sound.get());
			ma_sound_uninit(m_Sound.get());
			m_IsLoaded = false;
		}
	}

	bool AudioSource::LoadFromFile(const std::filesystem::path& filepath)
	{
		LUX_PROFILE_FUNCTION("AudioSource::LoadFromFile");

		auto* engine = AudioEngine::GetEngine();
		if (!engine)
			return false;

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_stop(m_Sound.get());
			ma_sound_uninit(m_Sound.get());
			m_IsLoaded = false;
		}

		const ma_result result = ma_sound_init_from_file(engine, filepath.string().c_str(), MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, m_Sound.get());
		if (result != MA_SUCCESS)
		{
			LUX_CORE_ERROR("Failed to initialize sound: {}", filepath.string());
			return false;
		}

		m_FilePath = filepath;
		m_IsLoaded = true;
		m_CursorPos = 0;
		return true;
	}

	void AudioSource::Play()
	{
		LUX_PROFILE_FUNCTION("AudioSource::Play");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_start(m_Sound.get());
		}
	}

	void AudioSource::Pause()
	{
		LUX_PROFILE_FUNCTION("AudioSource::Pause");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_stop(m_Sound.get());
		}
	}

	void AudioSource::UnPause()
	{
		LUX_PROFILE_FUNCTION("AudioSource::UnPause");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_start(m_Sound.get());
		}
	}

	void AudioSource::Stop()
	{
		LUX_PROFILE_FUNCTION("AudioSource::Stop");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_stop(m_Sound.get());
			ma_sound_seek_to_pcm_frame(m_Sound.get(), 0);

			m_CursorPos = 0;
		}
	}

	bool AudioSource::IsPlaying()
	{
		LUX_PROFILE_FUNCTION("AudioSource::IsPlaying");

		if (m_Sound.get() && m_IsLoaded)
			return ma_sound_is_playing(m_Sound.get());

		return false;
	}

	uint64_t AudioSource::GetCursorPosition()
	{
		if (m_Sound.get() && m_IsLoaded)
		{
			//uint64_t cursorPos = 0;
			ma_sound_get_cursor_in_pcm_frames(m_Sound.get(), &m_CursorPos);
			return m_CursorPos;
		}

		return 0;
	}

	static ma_attenuation_model GetAttenuationModel(AttenuationModelType model)
	{
		LUX_PROFILE_FUNCTION("ma_attenuation_model GetAttenuationModel");

		switch (model)
		{
		case AttenuationModelType::None:		return ma_attenuation_model_none;
		case AttenuationModelType::Inverse:		return ma_attenuation_model_inverse;
		case AttenuationModelType::Linear:		return ma_attenuation_model_linear;
		case AttenuationModelType::Exponential: return ma_attenuation_model_exponential;
		}

		return ma_attenuation_model_none;
	}

	void AudioSource::SetConfig(const AudioSourceConfig& config)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetConfig");

		if (m_Sound && m_IsLoaded)
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
		LUX_PROFILE_FUNCTION("AudioSource::SetVolume");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_volume(m_Sound.get(), volume);
		}
	}

	void AudioSource::SetPitch(float pitch)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetPitch");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_pitch(m_Sound.get(), pitch);
		}
	}

	bool AudioSource::IsLooping()
	{
		if (m_Sound && m_IsLoaded)
			return ma_sound_is_looping(m_Sound.get());

		return false;
	}

	void AudioSource::SetLooping(bool state)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetLooping");

		if (m_Sound && m_IsLoaded)
		{
			if (state)
				ma_sound_set_looping(m_Sound.get(), MA_TRUE);
			else
				ma_sound_set_looping(m_Sound.get(), MA_FALSE);
		}
	}

	void AudioSource::SetSpatialization(bool state)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetSpatialization");

		m_Spatialization = state;
		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_spatialization_enabled(m_Sound.get(), state);
		}
	}

	void AudioSource::SetAttenuationModel(AttenuationModelType type)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetAttenuationModel");

		if (m_Sound && m_IsLoaded)
		{
			if (m_Spatialization)
				ma_sound_set_attenuation_model(m_Sound.get(), GetAttenuationModel(type));
			else
				ma_sound_set_attenuation_model(m_Sound.get(), GetAttenuationModel(AttenuationModelType::None));
		}
	}

	void AudioSource::SetRollOff(float rollOff)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_rolloff(m_Sound.get(), rollOff);
		}
	}

	void AudioSource::SetMinGain(float minGain)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_min_gain(m_Sound.get(), minGain);
		}
	}

	void AudioSource::SetMaxGain(float maxGain)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_max_gain(m_Sound.get(), maxGain);
		}
	}

	void AudioSource::SetMinDistance(float minDistance)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_min_distance(m_Sound.get(), minDistance);
		}
	}

	void AudioSource::SetMaxDistance(float maxDistance)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_max_distance(m_Sound.get(), maxDistance);
		}
	}

	void AudioSource::SetCone(float innerAngle, float outerAngle, float outerGain)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetRollOff");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_cone(m_Sound.get(), innerAngle, outerAngle, outerGain);
		}
	}

	void AudioSource::SetDopplerFactor(float factor)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetDopplerFactor");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_doppler_factor(m_Sound.get(), std::max(factor, 0.0f));
		}
	}

	void AudioSource::SetPosition(const glm::vec4& position)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetPosition");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_position(m_Sound.get(), position.x, position.y, position.z);
		}
	}

	void AudioSource::SetDirection(const glm::vec3& forward)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetDirection");

		if (m_Sound && m_IsLoaded)
		{
			ma_sound_set_direction(m_Sound.get(), forward.x, forward.y, forward.z);
		}
	}

	void AudioSource::SetVelocity(const glm::vec3& velocity)
	{
		LUX_PROFILE_FUNCTION("AudioSource::SetVelocity");

		if (m_Sound)
		{
			ma_sound_set_velocity(m_Sound.get(), velocity.x, velocity.y, velocity.z);
		}
	}

}
