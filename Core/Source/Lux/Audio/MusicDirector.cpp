// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MusicDirector.h"
#include "AudioEngine.h"
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr size_t kMaxStingers = 32;
		constexpr const char* kSectionPrefix = "Section:";
	}

	Ref<AudioEventInstance> MusicDirector::Prepare(const std::string& reference, bool oneShot)
	{
		auto instance = AudioEventInstance::Create(reference);
		if (!instance)
			return nullptr;
		if (instance->IsSnapshot() || instance->Is3D() || instance->IsOneShot() != oneShot)
		{
			const char* reason = instance->IsSnapshot() ? "it is a snapshot"
				: instance->Is3D() ? "it is 3D (remove its spatializer in FMOD Studio)"
				: oneShot ? "it is not a one-shot" : "it is a one-shot";
			LUX_CORE_ERROR_TAG("Audio", "Music event {} must be a 2D {} event, but {}", reference, oneShot ? "one-shot stinger" : "continuous music bed", reason);
			return nullptr;
		}
		instance->SetScenePaused(m_Paused);
		if (!oneShot && (!instance->EnableTimelineNotifications() || !ApplyParameters(instance)))
			return nullptr;
		return instance;
	}

	bool MusicDirector::ApplyParameters(Ref<AudioEventInstance> instance)
	{
		if (!m_State.empty() && !instance->SetParameterLabel("State", m_State))
			return false;
		if (m_HasIntensity && !instance->SetParameter("Intensity", m_Intensity))
			return false;
		for (const auto& [layer, enabled] : m_Layers)
		{
			if (!instance->SetParameter("Layer_" + layer, enabled ? 1.0f : 0.0f))
				return false;
		}
		return true;
	}

	bool MusicDirector::Activate(Ref<AudioEventInstance> instance)
	{
		if (!instance || !instance->IsValid())
			return false;
		// Commands are ordered: the old bed stops before the new one starts, with no overlapping fade.
		if (m_Bed)
			m_Bed->Stop(false);
		m_Bed = std::move(instance);
		m_Reference = m_Bed->GetReference();
		m_Stopping = false;
		m_RetryRevision = AudioEngine::GetBankRevision();
		m_Beat = m_Bar = 0;
		if (!m_Bed->Start())
		{
			Stop(false);
			return false;
		}
		return true;
	}

	bool MusicDirector::Play(const std::string& reference)
	{
		auto instance = Prepare(reference, false);
		if (!instance)
			return false;
		m_Pending = nullptr;
		return Activate(std::move(instance));
	}

	void MusicDirector::Stop(bool allowFadeOut)
	{
		m_Pending = nullptr;
		for (auto& stinger : m_Stingers)
			stinger->Stop(allowFadeOut);
		if (!allowFadeOut)
			m_Stingers.clear();
		m_Reference.clear();
		m_Stopping = true;
		m_Beat = m_Bar = 0;
		if (m_Bed)
			m_Bed->Stop(allowFadeOut);
		if (!allowFadeOut)
			m_Bed = nullptr;
	}

	void MusicDirector::Clear()
	{
		Stop(false);
		m_Stingers.clear();
		m_State.clear();
		m_Layers.clear();
		m_Intensity = 0.0f;
		m_HasIntensity = m_Paused = false;
		m_TempoCallback = {};
		m_MarkerCallback = {};
		m_ScriptTempoCallback = {};
		m_ScriptMarkerCallback = {};
	}

	bool MusicDirector::SetState(const std::string& state)
	{
		if (state.empty())
		{
			LUX_CORE_ERROR_TAG("Audio", "Music state must name an authored State parameter label");
			return false;
		}
		if (m_Bed && m_Bed->IsValid() && !m_Bed->SetParameterLabel("State", state))
			return false;
		m_State = state;
		// A pending bed must also accept new gameplay state before it can replace the current bed.
		if (m_Pending && !m_Pending->SetParameterLabel("State", state))
			m_Pending = nullptr;
		return true;
	}

	bool MusicDirector::SetIntensity(float intensity)
	{
		if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 1.0f)
		{
			LUX_CORE_ERROR_TAG("Audio", "Music intensity must be finite and in [0, 1]");
			return false;
		}
		if (m_Bed && m_Bed->IsValid() && !m_Bed->SetParameter("Intensity", intensity))
			return false;
		m_Intensity = intensity;
		m_HasIntensity = true;
		if (m_Pending && !m_Pending->SetParameter("Intensity", intensity))
			m_Pending = nullptr;
		return true;
	}

	bool MusicDirector::SetLayerEnabled(const std::string& layer, bool enabled)
	{
		if (layer.empty())
		{
			LUX_CORE_ERROR_TAG("Audio", "Music layer must have an authored Layer_<name> parameter");
			return false;
		}
		if (m_Bed && m_Bed->IsValid() && !m_Bed->SetParameter("Layer_" + layer, enabled ? 1.0f : 0.0f))
			return false;
		m_Layers[layer] = enabled;
		if (m_Pending && !m_Pending->SetParameter("Layer_" + layer, enabled ? 1.0f : 0.0f))
			m_Pending = nullptr;
		return true;
	}

	bool MusicDirector::PlayStinger(const std::string& reference)
	{
		if (m_Stingers.size() >= kMaxStingers)
		{
			LUX_CORE_ERROR_TAG("Audio", "Music stinger limit ({}) reached", kMaxStingers);
			return false;
		}
		auto instance = Prepare(reference, true);
		if (!instance || !instance->Start())
			return false;
		m_Stingers.push_back(std::move(instance));
		return true;
	}

	bool MusicDirector::QueueTransition(const std::string& reference, MusicSync sync)
	{
		if (sync < MusicSync::Immediate || sync > MusicSync::NextSection)
		{
			LUX_CORE_ERROR_TAG("Audio", "Unknown music synchronization mode");
			return false;
		}
		if (sync == MusicSync::Immediate)
			return Play(reference);
		if (!m_Bed || !m_Bed->IsValid() || !m_Bed->IsPlaying() || m_Stopping)
		{
			LUX_CORE_ERROR_TAG("Audio", "A synchronized music transition requires a playing bed");
			return false;
		}
		auto instance = Prepare(reference, false);
		if (!instance)
			return false;
		m_Pending = std::move(instance);
		m_Sync = sync;
		m_TransitionAfterSequence = m_Bed->GetTimelineSequence();
		return true;
	}

	void MusicDirector::Update(bool paused)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Paused = paused;
		for (auto& stinger : m_Stingers)
			stinger->SetScenePaused(paused);
		std::erase_if(m_Stingers, [](const auto& instance) { return !instance->IsValid() || !instance->IsPlaying(); });
		if (m_Pending)
		{
			if (!m_Pending->IsValid())
			{
				LUX_CORE_WARN_TAG("Audio", "Cancelled queued music transition after bank unload");
				m_Pending = nullptr;
			}
			else
				m_Pending->SetScenePaused(paused);
		}
		if (m_Bed && !m_Bed->IsValid())
		{
			m_Bed = nullptr;
			m_Beat = m_Bar = 0;
		}
		if (!m_Bed && !m_Reference.empty() && m_RetryRevision != AudioEngine::GetBankRevision())
		{
			m_RetryRevision = AudioEngine::GetBankRevision();
			if (auto restored = Prepare(m_Reference, false))
				Activate(std::move(restored));
		}
		if (!m_Bed)
			return;
		m_Bed->SetScenePaused(paused);
		if (m_Stopping)
		{
			m_Bed->DrainTimelineNotifications();
			if (!m_Bed->IsPlaying())
				m_Bed = nullptr;
			return;
		}
		if (!m_Bed->IsPlaying())
		{
			Stop(false);
			return;
		}
		if (paused)
			return;
		// Hold the source alive and ignore its remaining batch if a gameplay callback changes music.
		auto source = m_Bed;
		for (const auto& notification : source->DrainTimelineNotifications())
		{
			if (m_Bed != source || m_Stopping)
				break;
			if (notification.IsBeat)
			{
				m_Bar = notification.Bar;
				m_Beat = notification.Beat;
				if (auto callback = m_TempoCallback)
					callback(m_Bar, m_Beat);
				if (m_Bed == source && !m_Stopping)
				{
					if (auto callback = m_ScriptTempoCallback)
						callback(m_Bar, m_Beat);
				}
			}
			else
			{
				if (auto callback = m_MarkerCallback)
					callback(notification.Marker);
				if (m_Bed == source && !m_Stopping)
				{
					if (auto callback = m_ScriptMarkerCallback)
						callback(notification.Marker);
				}
			}
			if (m_Bed != source || m_Stopping)
				break;
			const bool boundary = (notification.IsBeat && (m_Sync == MusicSync::NextBeat || (m_Sync == MusicSync::NextBar && notification.Beat == 1))) ||
				(!notification.IsBeat && (m_Sync == MusicSync::NextMarker || (m_Sync == MusicSync::NextSection && notification.Marker.starts_with(kSectionPrefix))));
			if (m_Pending && boundary && notification.Sequence > m_TransitionAfterSequence)
			{
				auto next = std::move(m_Pending);
				Activate(std::move(next));
				break;
			}
		}
	}
}
