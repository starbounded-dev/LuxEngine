#pragma once

#include "Lux/Core/Ref.h"

#include <string>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>

namespace FMOD { namespace Studio { class EventInstance; } }

namespace Lux {

	// Ray-traced measurements consumed by authored Studio parameters.
	struct AudioEventAcoustics
	{
		float OcclusionGainLF = 1.0f;
		float ReverbSend = 0.0f;
	};

	// Studio owns spatialization, looping, randomization and DSP. Gameplay places instances
	// and drives the parameters authored on the event.
	struct AudioEventNotification
	{
		uint64_t Handle = 0;
		bool Stopped = false;
		std::string Marker;
	};

	class AudioEventInstance : public RefCounted
	{
	public:
		~AudioEventInstance();
		AudioEventInstance(const AudioEventInstance&) = delete;
		AudioEventInstance& operator=(const AudioEventInstance&) = delete;

		// Resolves the event by GUID and creates an instance of it. Returns null when the event is
		// not in any loaded bank, which is the normal outcome for a scene saved against banks that
		// have not been built yet.
		static Ref<AudioEventInstance> Create(const std::string& eventGuid);

		const std::string& GetReference() const { return m_Guid; }
		bool IsOneShot() const;
		bool IsPaused() const { return m_Paused || m_ScenePaused; }
		float GetParameter(const std::string& name) const;
		bool SetParameterLabel(const std::string& name, const std::string& label);
		int GetTimelinePosition() const;
		void SetTimelinePosition(int milliseconds);
		bool SetCallbackHandle(uint64_t handle);
		static std::vector<AudioEventNotification> DrainNotifications();
		void Start();
		void Stop(bool allowFadeOut = true);
		void SetPaused(bool paused);
		// Scene pause is layered over the caller's pause state, so resuming the editor does not
		// unpause an event explicitly paused by gameplay.
		void SetScenePaused(bool paused);
		bool IsPlaying() const;
		bool IsValid() const;

		// Position and orientation of the emitter in world space. Studio takes all four together.
		void Set3DAttributes(const glm::vec3& position, const glm::vec3& velocity,
			const glm::vec3& forward, const glm::vec3& up);

		void SetVolume(float volume);
		void SetPitch(float pitch);

		// Sets an author-defined continuous parameter. Reports a rejected name/value once per instance.
		bool SetParameter(const std::string& name, float value);

		// Feeds ray-traced acoustics into the two named parameters below, leaving the sound
		// designer to decide what occlusion actually *does* to this particular sound. An event that
		// declares neither is simply unaffected.
		void SetAcoustics(const AudioEventAcoustics& acoustics);

		// The parameter names the engine writes acoustics into. Authoring an event with these names
		// is what opts it into ray-traced occlusion.
		static constexpr const char* kOcclusionParameter = "Occlusion";
		static constexpr const char* kReverbSendParameter = "ReverbSend";

	private:
		friend class Ref<AudioEventInstance>;
		AudioEventInstance(FMOD::Studio::EventInstance* instance, const std::string& guid, uint64_t generation)
			: m_Instance(instance), m_Guid(guid), m_Generation(generation) {}
		bool CheckResult(int result, const char* operation) const;

		FMOD::Studio::EventInstance* m_Instance = nullptr;
		std::string m_Guid;
		uint64_t m_Generation = 0;
		mutable std::unordered_set<std::string> m_ReportedErrors;
		bool m_Paused = false;
		bool m_ScenePaused = false;
	};

}
