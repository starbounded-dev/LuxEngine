#include "lpch.h"
#include "AudioSourcePlayback.h"
#include "AudioEngine.h"
#include "Lux/Scene/Components.h"
#include <cmath>
#include <fmod_studio.hpp>
#include <fmod_errors.h>
namespace Lux
{
	namespace
	{
		FMOD::Studio::EventDescription* Parameter(const std::string& guid, const std::string& name,
			FMOD_STUDIO_PARAMETER_DESCRIPTION& parameter, bool& warned)
		{
			FMOD_GUID id{};
			FMOD::Studio::EventDescription* description = nullptr;
			auto* studio = AudioEngine::GetStudioSystem();
			if (studio && FMOD::Studio::parseID(guid.c_str(), &id) == FMOD_OK && studio->getEventByID(&id, &description) == FMOD_OK &&
				description->getParameterDescriptionByName(name.c_str(), &parameter) == FMOD_OK &&
				!(parameter.flags & (FMOD_STUDIO_PARAMETER_READONLY | FMOD_STUDIO_PARAMETER_AUTOMATIC | FMOD_STUDIO_PARAMETER_GLOBAL)))
				return description;
			if (!warned)
			{
				warned = true;
				LUX_CORE_ERROR_TAG("Audio", "Event {} has no writable local parameter '{}'; check the banks and use global controls for global parameters", guid, name);
			}
			return nullptr;
		}
	}
	bool AudioSourcePlayback::OutOfRange(const glm::vec3& position, float maximum, const AudioListener::States& listeners, bool wasCulled)
	{
		if (!std::isfinite(maximum) || maximum <= 0 || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
			return false;
		// Five percent hysteresis avoids allocate/release churn at the authored boundary.
		const double range = maximum * (wasCulled ? 0.95 : 1.0);
		bool active = false;
		for (const auto& listener : listeners)
		{
			if (!std::isfinite(listener.Weight) || listener.Weight <= 0)
				continue;
			const auto point = listener.UseAttenuationPosition ? listener.AttenuationPosition : listener.Position;
			if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
				return false;
			active = true;
			if (glm::length(glm::dvec3(position) - glm::dvec3(point)) <= range)
				return false;
		}
		return active;
	}
	void AudioSourcePlayback::Apply()
	{
		if (!m_Instance)
			return;
		m_Instance->Set3DAttributes(glm::vec3(m_Transform[3]), glm::vec3(0), -glm::vec3(m_Transform[2]), glm::vec3(m_Transform[1]));
		m_Instance->SetVolume(m_Volume);
		m_Instance->SetPitch(m_Pitch);

		m_Instance->SetScenePaused(m_ScenePaused);
		m_Instance->SetPaused(m_Paused);
	}
	void AudioSourcePlayback::Update(const AudioSourceComponent& source, const glm::mat4& transform,
		const AudioListener::States& listeners, bool paused, bool allowAwake)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const auto revision = AudioEngine::GetBankRevision();
		if (m_Guid != source.Event.Guid)
		{
			Stop(false);
			m_Instance = nullptr;
			m_Guid = source.Event.Guid;
			m_AwakeHandled = m_Metadata = m_Attempted = m_Culled = m_ParameterWarning = false;
			m_Parameters.clear();
			m_Labels.clear();
			for (const auto& [name, value] : source.ParameterOverrides)
				m_Parameters[name] = value;
			m_Timeline = 0;
		}
		if (revision != m_Revision || (m_Instance && !m_Instance->IsValid()))
		{
			m_Instance = nullptr;
			m_Attempted = m_Metadata = m_Culled = m_Started = m_ParameterWarning = false;
			m_Revision = revision;
		}
		m_Transform = transform;
		m_Volume = source.Config.VolumeMultiplier;
		m_Pitch = source.Config.PitchMultiplier;
		const bool priorityChanged = m_Priority != source.Priority;
		m_Priority = source.Priority;
		m_Paused = source.ScriptPaused;
		m_ScenePaused = paused;
		if (allowAwake && !m_AwakeHandled)
		{
			m_AwakeHandled = true;
			m_WantsPlayback = source.Config.PlayOnAwake;
		}
		if (m_Instance && m_Started && !m_Instance->IsPlaying())
		{
			m_WantsPlayback = m_Started = false;
		}
		bool created = false;
		if (!m_Metadata && !m_Attempted && !m_Guid.empty())
		{
			m_Attempted = true;
			m_Instance = AudioEventInstance::Create(m_Guid);
			if (m_Instance)
			{
				m_Metadata = true;
				m_Spatial = m_Instance->Is3D();
				m_OneShot = m_Instance->IsOneShot();
				m_Maximum = m_Instance->GetMaximumDistance();
				// Canonical names preserve FMOD's case-insensitive/path aliases while culled.
				auto parameters = std::move(m_Parameters);
				auto labels = std::move(m_Labels);
				m_Parameters.clear();
				m_Labels.clear();
				for (const auto& [name, value] : parameters)
					SetParameter(name, value);
				for (const auto& [name, value] : labels)
					SetParameterLabel(name, value);
				created = true;
			}
		}
		m_Culled = source.DistanceCulling && m_Metadata && m_Spatial && OutOfRange(glm::vec3(transform[3]), m_Maximum, listeners, m_Culled);
		if (m_Culled)
		{
			if (m_Instance)
			{
				m_Timeline = m_Instance->GetTimelinePosition();
				m_Instance->Stop(false);
				m_Instance = nullptr;
			}
			m_Started = false;
			if (m_OneShot)
				m_WantsPlayback = false; // A past inaudible one-shot must never fire on re-entry.
			return;
		}
		if (!m_Instance && m_Metadata)
		{
			m_Instance = AudioEventInstance::Create(m_Guid);
			created = true;
			if (!m_Instance)
				m_Metadata = false; // Retry only when banks/reference change.
		}
		if (!m_Instance)
			return;
		Apply();
		if (created || priorityChanged)
			m_Instance->SetPriority(m_Priority);
		if (created)
		{
			for (const auto& [name, value] : m_Parameters)
				m_Instance->SetParameter(name, value);
			for (const auto& [name, value] : m_Labels)
				m_Instance->SetParameterLabel(name, value);
			m_Instance->SetTimelinePosition(m_Timeline);
		}
		if (m_WantsPlayback && !m_Started)
		{
			m_Started = m_Instance->Start();
			if (m_Started && m_Timeline > 0)
				m_Instance->SetTimelinePosition(m_Timeline);
		}
	}
	bool AudioSourcePlayback::IsPlaying() const
	{
		return m_Culled ? m_WantsPlayback : m_Instance && m_Instance->IsPlaying();
	}
	void AudioSourcePlayback::Play()
	{
		m_AwakeHandled = true;
		m_WantsPlayback = !(m_Culled && m_OneShot);
		m_Paused = false;
		m_Timeline = 0;
		if (m_Instance)
		{
			m_Instance->SetPaused(false);
			m_Started = m_Instance->Start();
		}
	}
	void AudioSourcePlayback::Stop(bool fade)
	{
		m_AwakeHandled = true;
		m_WantsPlayback = m_Started = false;
		if (m_Instance)
			m_Instance->Stop(fade);
	}
	bool AudioSourcePlayback::SetParameter(const std::string& name, float value)
	{
		FMOD_STUDIO_PARAMETER_DESCRIPTION parameter{};
		if (!std::isfinite(value))
		{
			if (!m_ParameterWarning)
			{
				m_ParameterWarning = true;
				LUX_CORE_ERROR_TAG("Audio", "Event {} parameter '{}' must be finite", m_Guid, name);
			}
			return false;
		}
		if (!Parameter(m_Guid, name, parameter, m_ParameterWarning))
			return false;
		value = std::clamp(value, parameter.minimum, parameter.maximum);
		if (parameter.flags & (FMOD_STUDIO_PARAMETER_DISCRETE | FMOD_STUDIO_PARAMETER_LABELED))
			value = std::round(value);
		if (m_Instance && !m_Instance->SetParameter(name, value))
			return false;
		m_Parameters[parameter.name] = value;
		m_Labels.erase(parameter.name);
		return true;
	}
	float AudioSourcePlayback::GetParameter(const std::string& name) const
	{
		if (m_Instance)
			return m_Instance->GetParameter(name);
		FMOD_STUDIO_PARAMETER_DESCRIPTION parameter{};
		if (!Parameter(m_Guid, name, parameter, m_ParameterWarning))
			return 0.0f;
		const auto found = m_Parameters.find(parameter.name);
		return found != m_Parameters.end() ? found->second : parameter.defaultvalue;
	}
	bool AudioSourcePlayback::SetParameterLabel(const std::string& name, const std::string& value)
	{
		FMOD_STUDIO_PARAMETER_DESCRIPTION parameter{};
		auto* description = Parameter(m_Guid, name, parameter, m_ParameterWarning);
		if (!description)
			return false;
		if (!(parameter.flags & FMOD_STUDIO_PARAMETER_LABELED))
		{
			if (!m_ParameterWarning)
			{
				m_ParameterWarning = true;
				LUX_CORE_ERROR_TAG("Audio", "Event {} parameter '{}' does not have labels", m_Guid, name);
			}
			return false;
		}
		for (int index = 0; index <= parameter.maximum - parameter.minimum; ++index)
		{
			char label[1024]{};
			int required = 0;
			auto result = description->getParameterLabelByName(name.c_str(), index, label, sizeof(label), &required);
			std::string extended;
			if (result == FMOD_ERR_TRUNCATED && required > 0)
			{
				extended.resize(static_cast<size_t>(required));
				result = description->getParameterLabelByName(name.c_str(), index, extended.data(), required, nullptr);
			}
			if (result != FMOD_OK)
			{
				if (!m_ParameterWarning)
				{
					m_ParameterWarning = true;
					LUX_CORE_ERROR_TAG("Audio", "Cannot read parameter '{}' label: {}", name, FMOD_ErrorString(result));
				}
				return false;
			}
			if (value == (extended.empty() ? label : extended.c_str()))
			{
				if (m_Instance && !m_Instance->SetParameterLabel(name, value))
					return false;
				m_Labels[parameter.name] = value;
				m_Parameters[parameter.name] = parameter.minimum + index;
				return true;
			}
		}
		if (!m_ParameterWarning)
		{
			m_ParameterWarning = true;
			LUX_CORE_ERROR_TAG("Audio", "Unknown label '{}' for event {} parameter '{}'", value, m_Guid, name);
		}
		return false;
	}
	int AudioSourcePlayback::GetTimelinePosition() const
	{
		return m_Instance ? m_Instance->GetTimelinePosition() : m_Timeline;
	}
	void AudioSourcePlayback::SetTimelinePosition(int value)
	{
		value = std::max(0, value);
		m_Timeline = value;
		if (m_Instance)
			m_Instance->SetTimelinePosition(value);
	}
}
