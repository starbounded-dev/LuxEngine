// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include <mutex>
#include "AudioEventInstance.h"

#include "AudioEngine.h"
#include "AudioAccessibility.h"

#include <fmod.hpp>
#include <fmod_studio.hpp>
#include <fmod_errors.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace Lux {


	namespace {
		constexpr double kMinOrientationLengthSquared = 1.0e-8;
		constexpr double kParallelUpThreshold = 0.9;

		// The inverse of AudioEngine's GuidToString: scenes store the braced form fmodstudiocl
		// exports, and Studio resolves events from an FMOD_GUID.
		bool ParseGuid(const std::string& text, FMOD_GUID& outGuid)
		{
			if (text.size() != 38 || text.front() != '{' || text.back() != '}')
				return false;

			for (size_t i = 1; i < 37; ++i)
			{
				const bool separator = i == 9 || i == 14 || i == 19 || i == 24;
				if (separator ? text[i] != '-' : !std::isxdigit(static_cast<unsigned char>(text[i])))
					return false;
			}
			return FMOD::Studio::parseID(text.c_str(), &outGuid) == FMOD_OK;
		}

		FMOD_VECTOR ToFMOD(const glm::vec3& v)
		{
			return FMOD_VECTOR{ v.x, v.y, v.z };
		}

	}

	namespace
	{
		std::mutex s_NotificationMutex;
		std::vector<AudioEventNotification> s_Notifications;
		size_t s_DroppedNotifications = 0;
		FMOD_RESULT s_CallbackError = FMOD_OK;
		struct CallbackState
		{
			uint64_t ScriptHandle = 0;
			bool Timeline = false;
			std::string ProgrammerKey;
			AudioPlaybackStatus Playback;
			uint64_t Sequence = 0;
			std::vector<AudioTimelineNotification> Notifications;
		};
		std::unordered_map<uint64_t, CallbackState> s_CallbackStates;
		uint64_t s_NextCallbackToken = 1;
		constexpr size_t kNotificationCapacity = 4096;

		static FMOD_RESULT F_CALL AudioCallback(FMOD_STUDIO_EVENT_CALLBACK_TYPE type, FMOD_STUDIO_EVENTINSTANCE* raw, void* parameters)
		{
			// A released wrapper has no mailbox, but FMOD still owns the programmer sound.
			if (type == FMOD_STUDIO_EVENT_CALLBACK_DESTROY_PROGRAMMER_SOUND)
			{
				auto* sound = reinterpret_cast<FMOD::Sound*>(static_cast<FMOD_STUDIO_PROGRAMMER_SOUND_PROPERTIES*>(parameters)->sound);
				const auto released = sound ? sound->release() : FMOD_OK;
				if (released != FMOD_OK)
				{
					std::scoped_lock lock(s_NotificationMutex);
					s_CallbackError = released;
				}
				return released;
			}
			void* data = nullptr;
			const auto result = reinterpret_cast<FMOD::Studio::EventInstance*>(raw)->getUserData(&data);
			if (result != FMOD_OK)
				return result;
			if (!data)
				return FMOD_OK;
			if (type == FMOD_STUDIO_EVENT_CALLBACK_CREATE_PROGRAMMER_SOUND)
			{
				auto& properties = *static_cast<FMOD_STUDIO_PROGRAMMER_SOUND_PROPERTIES*>(parameters);
				properties.sound = nullptr;
				properties.subsoundIndex = -1;
				std::string key;
				try
				{
					std::scoped_lock lock(s_NotificationMutex);
					const auto found = s_CallbackStates.find(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(data)));
					if (found == s_CallbackStates.end())
						return FMOD_OK;
					key = found->second.ProgrammerKey;
				}
				catch (const std::bad_alloc&)
				{
					std::scoped_lock lock(s_NotificationMutex);
					s_CallbackError = FMOD_ERR_MEMORY;
					return FMOD_ERR_MEMORY;
				}
				FMOD::Studio::System* studio = nullptr;
				FMOD::System* core = nullptr;
				FMOD_STUDIO_SOUND_INFO info{};
				FMOD::Sound* sound = nullptr;
				auto error = reinterpret_cast<FMOD::Studio::EventInstance*>(raw)->getSystem(&studio);
				if (error == FMOD_OK)
					error = studio->getSoundInfo(key.c_str(), &info);
				if (error == FMOD_OK)
					error = studio->getCoreSystem(&core);
				if (error == FMOD_OK)
					error = core->createSound(info.name_or_data, info.mode | FMOD_LOOP_NORMAL | FMOD_CREATECOMPRESSEDSAMPLE | FMOD_NONBLOCKING, &info.exinfo, &sound);
				if (error == FMOD_OK)
				{
					properties.sound = reinterpret_cast<FMOD_SOUND*>(sound);
					properties.subsoundIndex = info.subsoundindex;
				}
				else
				{
					std::scoped_lock lock(s_NotificationMutex);
					s_CallbackError = error;
					if (auto found = s_CallbackStates.find(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(data))); found != s_CallbackStates.end())
						found->second.Playback.Error = error;
				}
				return error;
			}
			std::scoped_lock lock(s_NotificationMutex);
			const auto it = s_CallbackStates.find(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(data)));
			if (it == s_CallbackStates.end())
				return FMOD_OK;
			try
			{
				auto& state = it->second;
				if (type == FMOD_STUDIO_EVENT_CALLBACK_STARTED)
					state.Playback.Started = true;
				if (type == FMOD_STUDIO_EVENT_CALLBACK_START_FAILED)
					state.Playback.Error = FMOD_ERR_INTERNAL;
				if (type == FMOD_STUDIO_EVENT_CALLBACK_STOPPED)
					state.Playback.Stopped = true;
				if (type == FMOD_STUDIO_EVENT_CALLBACK_SOUND_PLAYED)
				{
					unsigned int milliseconds = 0;
					const auto lengthResult = static_cast<FMOD::Sound*>(parameters)->getLength(&milliseconds, FMOD_TIMEUNIT_MS);
					state.Playback.SoundStarted = true;
					if (lengthResult == FMOD_OK)
						state.Playback.Duration = milliseconds / 1000.0f;
					else
						s_CallbackError = lengthResult;
				}
				if (state.ScriptHandle && (type == FMOD_STUDIO_EVENT_CALLBACK_STOPPED || type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER))
				{
					if (s_Notifications.size() < kNotificationCapacity)
					{
						AudioEventNotification notification;
						notification.Handle = state.ScriptHandle;
						notification.Stopped = type == FMOD_STUDIO_EVENT_CALLBACK_STOPPED;
						if (type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER)
							notification.Marker = static_cast<FMOD_STUDIO_TIMELINE_MARKER_PROPERTIES*>(parameters)->name;
						s_Notifications.push_back(std::move(notification));
					}
					else
						++s_DroppedNotifications;
				}
				if (state.Timeline && (type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_BEAT || type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER))
				{
					if (state.Notifications.size() < kNotificationCapacity)
					{
						AudioTimelineNotification notification;
						notification.Sequence = ++state.Sequence;
						notification.IsBeat = type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_BEAT;
						if (notification.IsBeat)
						{
							const auto& beat = *static_cast<FMOD_STUDIO_TIMELINE_BEAT_PROPERTIES*>(parameters);
							notification.Bar = beat.bar;
							notification.Beat = beat.beat;
							notification.Position = beat.position;
							notification.Tempo = beat.tempo;
							notification.TimeSignatureUpper = beat.timesignatureupper;
							notification.TimeSignatureLower = beat.timesignaturelower;
						}
						else
						{
							const auto& marker = *static_cast<FMOD_STUDIO_TIMELINE_MARKER_PROPERTIES*>(parameters);
							notification.Marker = marker.name;
							notification.Position = marker.position;
						}
						state.Notifications.push_back(std::move(notification));
					}
					else
						++s_DroppedNotifications;
				}
			}
			catch (const std::bad_alloc&)
			{
				++s_DroppedNotifications; // Report on the main thread, never unwind through FMOD.
				return FMOD_ERR_MEMORY;
			}
			return FMOD_OK;
		}

	} // namespace

	std::vector<AudioEventNotification> AudioEventInstance::DrainNotifications()
	{
		std::vector<AudioEventNotification> result;
		std::scoped_lock lock(s_NotificationMutex);
		result.swap(s_Notifications);
		if (s_CallbackError != FMOD_OK)
		{
			LUX_CORE_ERROR_TAG("Audio", "FMOD callback failed: {}", FMOD_ErrorString(s_CallbackError));
			s_CallbackError = FMOD_OK;
		}
		if (s_DroppedNotifications)
		{
			LUX_CORE_ERROR_TAG("Audio", "Audio notification queue overflow: {0} notifications dropped", s_DroppedNotifications);
			s_DroppedNotifications = 0;
		}
		return result;
	}

	bool AudioEventInstance::ConfigureCallbacks(uint64_t scriptHandle, bool timeline)
	{
		if (!IsValid())
			return false;
		{
			std::scoped_lock lock(s_NotificationMutex);
			if (!m_CallbackToken)
			{
				if (!s_NextCallbackToken)
					return CheckResult(FMOD_ERR_INTERNAL, "callback token space exhausted");
				m_CallbackToken = s_NextCallbackToken++;
			}
			auto& state = s_CallbackStates[m_CallbackToken];
			if (scriptHandle)
				state.ScriptHandle = scriptHandle;
			state.Timeline |= timeline;
			timeline = state.Timeline;
		}
		return CheckResult(m_Instance->setUserData(reinterpret_cast<void*>(static_cast<uintptr_t>(m_CallbackToken))), "set callback token") &&
			CheckResult(m_Instance->setCallback(AudioCallback, FMOD_STUDIO_EVENT_CALLBACK_STOPPED | FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER |
				FMOD_STUDIO_EVENT_CALLBACK_STARTED | FMOD_STUDIO_EVENT_CALLBACK_START_FAILED | FMOD_STUDIO_EVENT_CALLBACK_SOUND_PLAYED |
				FMOD_STUDIO_EVENT_CALLBACK_CREATE_PROGRAMMER_SOUND | FMOD_STUDIO_EVENT_CALLBACK_DESTROY_PROGRAMMER_SOUND |
				(timeline ? FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_BEAT : 0)), "set callback");
	}

	bool AudioEventInstance::SetCallbackHandle(uint64_t handle)
	{
		return ConfigureCallbacks(handle, false);
	}

	bool AudioEventInstance::EnableTimelineNotifications()
	{
		return ConfigureCallbacks(0, true);
	}

	std::vector<AudioTimelineNotification> AudioEventInstance::DrainTimelineNotifications()
	{
		std::vector<AudioTimelineNotification> result;
		std::scoped_lock lock(s_NotificationMutex);
		if (auto it = s_CallbackStates.find(m_CallbackToken); it != s_CallbackStates.end())
			result.swap(it->second.Notifications);
		return result;
	}

	uint64_t AudioEventInstance::GetTimelineSequence() const
	{
		std::scoped_lock lock(s_NotificationMutex);
		const auto it = s_CallbackStates.find(m_CallbackToken);
		return it != s_CallbackStates.end() ? it->second.Sequence : 0;
	}

	bool AudioEventInstance::SetPriority(int priority)
	{
		if (priority < 0 || priority > 256)
			return CheckResult(FMOD_ERR_INVALID_PARAM, "priority must be in [0, 256]");
		return IsValid() &&
			CheckResult(m_Instance->setProperty(FMOD_STUDIO_EVENT_PROPERTY_CHANNELPRIORITY, static_cast<float>(priority)), "set priority");
	}
	int AudioEventInstance::GetPriority() const
	{
		float priority = 128;
		if (IsValid())
			CheckResult(m_Instance->getProperty(FMOD_STUDIO_EVENT_PROPERTY_CHANNELPRIORITY, &priority), "get priority");
		return static_cast<int>(priority);
	}
	float AudioEventInstance::GetMaximumDistance() const
	{
		float minimum = 0, maximum = 0;
		return IsValid() && CheckResult(m_Instance->getMinMaxDistance(&minimum, &maximum), "get distance range") ? maximum : 0;
	}
	bool AudioEventInstance::IsVirtual() const
	{
		bool result = false;
		return IsValid() && CheckResult(m_Instance->isVirtual(&result), "get virtualization state") && result;
	}

	bool AudioEventInstance::Is3D() const
	{
		FMOD::Studio::EventDescription* description = nullptr;
		bool spatial = false;
		return IsValid() && CheckResult(m_Instance->getDescription(&description), "get description") &&
			CheckResult(description->is3D(&spatial), "get spatial state") && spatial;
	}

	bool AudioEventInstance::IsOneShot() const
	{
		FMOD::Studio::EventDescription* description = nullptr;
		bool oneShot = false;
		return IsValid() && CheckResult(m_Instance->getDescription(&description), "get description") &&
			   CheckResult(description->isOneshot(&oneShot), "get one-shot state") && oneShot;
	}

	bool AudioEventInstance::IsSnapshot() const
	{
		FMOD::Studio::EventDescription* description = nullptr;
		bool snapshot = false;
		return IsValid() && CheckResult(m_Instance->getDescription(&description), "get description") &&
			CheckResult(description->isSnapshot(&snapshot), "get snapshot state") && snapshot;
	}

	bool AudioEventInstance::SetSnapshotIntensity(float intensity)
	{
		if (!IsValid())
			return false;
		if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 1.0f || (!m_SnapshotIntensityValidated && !IsSnapshot()))
			return CheckResult(FMOD_ERR_INVALID_PARAM, "snapshot intensity requires a snapshot and a value in [0, 1]");
		if (!m_SnapshotIntensityValidated)
		{
			FMOD::Studio::EventDescription* description = nullptr;
			FMOD_STUDIO_PARAMETER_DESCRIPTION parameter{};
			if (!CheckResult(m_Instance->getDescription(&description), "get snapshot description") ||
				!CheckResult(description->getParameterDescriptionByName("Intensity", &parameter),
					"expose the snapshot Intensity dial as a 0–100 parameter in FMOD Studio"))
				return false;
			if (parameter.minimum != 0.0f || parameter.maximum != 100.0f ||
				(parameter.flags & (FMOD_STUDIO_PARAMETER_READONLY | FMOD_STUDIO_PARAMETER_GLOBAL | FMOD_STUDIO_PARAMETER_DISCRETE | FMOD_STUDIO_PARAMETER_LABELED)))
				return CheckResult(FMOD_ERR_INVALID_PARAM, "snapshot Intensity must be a local continuous writable 0–100 parameter");
			m_SnapshotIntensityID[0] = parameter.id.data1;
			m_SnapshotIntensityID[1] = parameter.id.data2;
			m_SnapshotIntensityValidated = true;
		}
		const FMOD_STUDIO_PARAMETER_ID id{ m_SnapshotIntensityID[0], m_SnapshotIntensityID[1] };
		return CheckResult(m_Instance->setParameterByID(id, intensity * 100.0f, true), "set snapshot intensity");
	}

	float AudioEventInstance::GetParameter(const std::string& name) const
	{
		float value = 0.0f;
		if (IsValid())
			CheckResult(m_Instance->getParameterByName(name.c_str(), &value), ("get parameter: " + name).c_str());
		return value;
	}

	bool AudioEventInstance::SetParameterLabel(const std::string& name, const std::string& label)
	{
		return IsValid() && CheckResult(m_Instance->setParameterByNameWithLabel(name.c_str(), label.c_str()), "set parameter label");
	}

	int AudioEventInstance::GetTimelinePosition() const
	{
		int position = 0;
		if (IsValid())
			CheckResult(m_Instance->getTimelinePosition(&position), "get timeline position");
		return position;
	}

	int AudioEventInstance::GetLength() const
	{
		FMOD::Studio::EventDescription* description = nullptr;
		int length = 0;
		return IsValid() && CheckResult(m_Instance->getDescription(&description), "get description") &&
			CheckResult(description->getLength(&length), "get timeline length") ? length : 0;
	}

	void AudioEventInstance::SetTimelinePosition(int milliseconds)
	{
		if (IsValid())
			CheckResult(m_Instance->setTimelinePosition(std::max(0, milliseconds)), "set timeline position");
	}

	Ref<AudioEventInstance> AudioEventInstance::Create(const std::string& eventGuid)
	{
		FMOD::Studio::System* studio = AudioEngine::GetStudioSystem();
		if (eventGuid.empty())
			return nullptr;
		if (!studio)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot create event {0}: FMOD Studio is not initialized", eventGuid);
			return nullptr;
		}

		const std::string resolved = AudioEngine::ResolveEventReference(eventGuid);
		if (resolved.empty())
			return nullptr;
		FMOD_GUID guid{};
		if (!ParseGuid(resolved, guid))
		{
			LUX_CORE_ERROR_TAG("Audio", "'{0}' is not a valid event GUID", eventGuid);
			return nullptr;
		}

		FMOD::Studio::EventDescription* description = nullptr;
		if (studio->getEventByID(&guid, &description) != FMOD_OK || !description)
		{
			// Names the GUID rather than staying silent: the usual cause is a scene referencing an
			// event whose bank has not been built, and the GUID is what to search GUIDs.txt for.
			LUX_CORE_WARN_TAG("Audio", "Event {0} is not in any loaded bank - build the FMOD Studio project, or check the bank is loaded", eventGuid);
			return nullptr;
		}

		FMOD::Studio::EventInstance* instance = nullptr;
		const FMOD_RESULT result = description->createInstance(&instance);
		if (result != FMOD_OK || !instance)
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed to create event {0}: {1}", eventGuid, FMOD_ErrorString(result));
			return nullptr;
		}

		return Ref<AudioEventInstance>::Create(instance, resolved, AudioEngine::GetEventGeneration());
	}

	AudioEventInstance::~AudioEventInstance()
	{
		{
			std::scoped_lock lock(s_NotificationMutex);
			s_CallbackStates.erase(m_CallbackToken);
		}
		if (!IsValid())
			return;

		// Stop before release so a still-audible instance does not keep its resources alive past the
		// scene that owned it. FMOD_STUDIO_STOP_IMMEDIATE rather than ALLOWFADEOUT: this runs during
		// teardown, where a fade would outlive the thing being torn down.
		CheckResult(m_Instance->stop(FMOD_STUDIO_STOP_IMMEDIATE), "stop during destruction");
		CheckResult(m_Instance->release(), "release");
		m_Instance = nullptr;
	}

	bool AudioEventInstance::IsValid() const
	{
		// Do not call into a handle from a released system, even to ask FMOD whether it is valid.
		return m_Instance && AudioEngine::HasInitializedEngine()
			&& m_Generation == AudioEngine::GetEventGeneration() && m_Instance->isValid();
	}

	bool AudioEventInstance::CheckResult(int result, const char* operation) const
	{
		if (result == FMOD_OK)
			return true;
		if (m_ReportedErrors.emplace(operation).second)
			LUX_CORE_ERROR_TAG("Audio", "Event {0}: {1} failed: {2}", m_Guid, operation, FMOD_ErrorString(static_cast<FMOD_RESULT>(result)));
		return false;
	}

	bool AudioEventInstance::MonitorPlayback()
	{
		return ConfigureCallbacks(0, false);
	}

	bool AudioEventInstance::SetProgrammerSound(const std::string& key)
	{
		if (!IsValid() || IsPlaying() || key.empty() || key.size() > 512 || key.find('\0') != std::string::npos)
			return CheckResult(FMOD_ERR_INVALID_PARAM, "programmer sound requires an unstarted event and a valid audio-table key");
		FMOD_STUDIO_SOUND_INFO info{};
		if (!CheckResult(AudioEngine::GetStudioSystem()->getSoundInfo(key.c_str(), &info), "resolve programmer sound key") || !MonitorPlayback())
			return false;
		std::scoped_lock lock(s_NotificationMutex);
		s_CallbackStates.at(m_CallbackToken).ProgrammerKey = key;
		return true;
	}

	AudioPlaybackStatus AudioEventInstance::GetPlaybackStatus() const
	{
		AudioPlaybackStatus result;
		{
			std::scoped_lock lock(s_NotificationMutex);
			if (auto it = s_CallbackStates.find(m_CallbackToken); it != s_CallbackStates.end())
				result = it->second.Playback;
		}
		CheckResult(result.Error, "playback callback");
		return result;
	}

	bool AudioEventInstance::Start()
	{
		if (!IsValid())
			return false;
		AudioAccessibility::Track(this);
		{
			std::scoped_lock lock(s_NotificationMutex);
			if (auto it = s_CallbackStates.find(m_CallbackToken); it != s_CallbackStates.end())
				it->second.Playback = {};
		}
		const auto result = m_Instance->start();
		if (result != FMOD_OK)
		{
			std::scoped_lock lock(s_NotificationMutex);
			if (auto it = s_CallbackStates.find(m_CallbackToken); it != s_CallbackStates.end())
				it->second.Playback.Error = result;
		}
		return CheckResult(result, "start");
	}

	void AudioEventInstance::Stop(bool allowFadeOut)
	{
		if (IsValid())
			CheckResult(m_Instance->stop(allowFadeOut ? FMOD_STUDIO_STOP_ALLOWFADEOUT : FMOD_STUDIO_STOP_IMMEDIATE), "stop");
	}

	void AudioEventInstance::SetPaused(bool paused)
	{
		if (m_Paused == paused)
			return;
		m_Paused = paused;
		if (IsValid())
			CheckResult(m_Instance->setPaused(m_Paused || m_ScenePaused), "set pause");
	}

	void AudioEventInstance::SetScenePaused(bool paused)
	{
		if (m_ScenePaused == paused)
			return;
		m_ScenePaused = paused;
		if (IsValid())
			CheckResult(m_Instance->setPaused(m_Paused || m_ScenePaused), "set scene pause");
	}

	bool AudioEventInstance::IsPlaying() const
	{
		if (!IsValid())
			return false;

		FMOD_STUDIO_PLAYBACK_STATE state = FMOD_STUDIO_PLAYBACK_STOPPED;
		if (!CheckResult(m_Instance->getPlaybackState(&state), "query playback state"))
			return false;

		// SUSTAINING counts as playing: the event is holding on a sustain point waiting for a key-off,
		// which is audible even though it is not STARTING or PLAYING. STOPPING counts too: the fade-out
		// is still audible, and the directors that retire instances once !IsPlaying() (dialogue,
		// music stingers, physics voices) must not cut it short. SetProgrammerSound refusing a
		// fading event is intended - it needs one that has never started.
		return state == FMOD_STUDIO_PLAYBACK_PLAYING
			|| state == FMOD_STUDIO_PLAYBACK_STARTING
			|| state == FMOD_STUDIO_PLAYBACK_SUSTAINING
			|| state == FMOD_STUDIO_PLAYBACK_STOPPING;
	}

	void AudioEventInstance::Set3DAttributes(const glm::vec3& position, const glm::vec3& velocity,
		const glm::vec3& forward, const glm::vec3& up)
	{
		if (!IsValid())
			return;

		const auto finite = [](const glm::vec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		};
		if (!finite(position) || !finite(velocity) || !finite(forward) || !finite(up))
		{
			CheckResult(FMOD_ERR_INVALID_FLOAT, "set 3D attributes");
			return;
		}

		// Nonuniform parent scale can shear a transform; FMOD requires an orthonormal basis.
		// A zero-scale emitter still has a position, so use a neutral orientation for collapsed axes.
		const glm::dvec3 forwardDouble(forward), upDouble(up);
		const glm::dvec3 direction = glm::dot(forwardDouble, forwardDouble) > kMinOrientationLengthSquared
			? glm::normalize(forwardDouble) : glm::dvec3(0.0, 0.0, -1.0);
		glm::dvec3 normal = upDouble - direction * glm::dot(upDouble, direction);
		if (glm::dot(normal, normal) <= kMinOrientationLengthSquared)
		{
			const glm::dvec3 axis = std::abs(direction.y) < kParallelUpThreshold ? glm::dvec3(0.0, 1.0, 0.0) : glm::dvec3(1.0, 0.0, 0.0);
			normal = axis - direction * glm::dot(axis, direction);
		}

		FMOD_3D_ATTRIBUTES attributes{};
		attributes.position = ToFMOD(position);
		attributes.velocity = ToFMOD(velocity);
		attributes.forward = ToFMOD(glm::vec3(direction));
		attributes.up = ToFMOD(glm::vec3(glm::normalize(normal)));
		if (CheckResult(m_Instance->set3DAttributes(&attributes), "set 3D attributes"))
			m_Position = position;
	}

	void AudioEventInstance::SetVolume(float volume)
	{
		if (IsValid())
		{
			if (CheckResult(std::isfinite(volume) ? m_Instance->setVolume(std::max(volume, 0.0f)) : FMOD_ERR_INVALID_FLOAT, "set volume"))
				m_Volume = std::max(volume, 0.0f);
		}
	}

	void AudioEventInstance::SetPitch(float pitch)
	{
		if (IsValid())
			CheckResult(std::isfinite(pitch) ? m_Instance->setPitch(std::max(pitch, 0.0f)) : FMOD_ERR_INVALID_FLOAT, "set pitch");
	}

	bool AudioEventInstance::SetParameter(const std::string& name, float value)
	{
		if (!IsValid())
			return false;

		return CheckResult(std::isfinite(value) ? m_Instance->setParameterByName(name.c_str(), value) : FMOD_ERR_INVALID_FLOAT, name.c_str());
	}

	void AudioEventInstance::SetAcoustics(const AudioEventAcoustics& acoustics)
	{
		if (!IsValid())
			return;

		// The low band measures broadband transmission; Studio authors its audible effect.
		const float occlusion = std::clamp(1.0f - acoustics.OcclusionGainLF, 0.0f, 1.0f);
		// These parameters are opt-in; an absent one is expected. Other failures are reported.
		const auto setOptional = [&](const char* name, float value)
		{
			const FMOD_RESULT result = std::isfinite(value) ? m_Instance->setParameterByName(name, value) : FMOD_ERR_INVALID_FLOAT;
			if (result != FMOD_ERR_EVENT_NOTFOUND)
				CheckResult(result, name);
		};
		setOptional(kOcclusionParameter, occlusion);
		setOptional(kReverbSendParameter, std::clamp(acoustics.ReverbSend, 0.0f, 1.0f));
	}


}
