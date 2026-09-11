#include "lpch.h"
#include <mutex>
#include "AudioEventInstance.h"

#include "AudioEngine.h"

#include <fmod.hpp>
#include <fmod_studio.hpp>
#include <fmod_errors.h>

#include <algorithm>
#include <cctype>
#include <cmath>

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

		static FMOD_RESULT F_CALL AudioCallback(FMOD_STUDIO_EVENT_CALLBACK_TYPE type, FMOD_STUDIO_EVENTINSTANCE* raw, void* parameters)
		{
			void* data = nullptr;
			const auto result = reinterpret_cast<FMOD::Studio::EventInstance*>(raw)->getUserData(&data);
			if (result != FMOD_OK)
				return result;
			if (!data)
				return FMOD_OK;
			std::scoped_lock lock(s_NotificationMutex);
			if (s_Notifications.size() >= 4096)
			{
				++s_DroppedNotifications;
				return FMOD_OK;
			}
			try
			{
				AudioEventNotification notification;
				notification.Handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(data));
				notification.Stopped = type == FMOD_STUDIO_EVENT_CALLBACK_STOPPED;
				if (type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER)
					notification.Marker = static_cast<FMOD_STUDIO_TIMELINE_MARKER_PROPERTIES*>(parameters)->name;
				s_Notifications.push_back(std::move(notification));
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
		if (s_DroppedNotifications)
		{
			LUX_CORE_ERROR_TAG("Audio", "Audio notification queue overflow: {0} notifications dropped", s_DroppedNotifications);
			s_DroppedNotifications = 0;
		}
		return result;
	}

	bool AudioEventInstance::SetCallbackHandle(uint64_t handle)
	{
		return IsValid() && CheckResult(m_Instance->setUserData(reinterpret_cast<void*>(static_cast<uintptr_t>(handle))), "set callback handle") &&
			   CheckResult(m_Instance->setCallback(AudioCallback, FMOD_STUDIO_EVENT_CALLBACK_STOPPED | FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER),
						   "set callback");
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

	void AudioEventInstance::Start()
	{
		if (IsValid())
			CheckResult(m_Instance->start(), "start");
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
		// which is audible even though it is not STARTING or PLAYING.
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
		CheckResult(m_Instance->set3DAttributes(&attributes), "set 3D attributes");
	}

	void AudioEventInstance::SetVolume(float volume)
	{
		if (IsValid())
			CheckResult(std::isfinite(volume) ? m_Instance->setVolume(std::max(volume, 0.0f)) : FMOD_ERR_INVALID_FLOAT, "set volume");
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
