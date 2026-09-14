#pragma once

#include "AudioEventInstance.h"
#include <functional>
#include <map>

namespace Lux
{
	enum class MusicSync { Immediate, NextBeat, NextBar, NextMarker, NextSection };

	// Scene-owned, main-thread service. Studio authors the score; the director owns its lifetime.
	class MusicDirector
	{
	public:
		MusicDirector() = default;
		MusicDirector(const MusicDirector&) = delete;
		MusicDirector& operator=(const MusicDirector&) = delete;
		bool Play(const std::string& eventReference);
		void Stop(bool allowFadeOut = true);
		void Clear();
		bool SetState(const std::string& state);
		bool SetIntensity(float intensity);
		bool SetLayerEnabled(const std::string& layer, bool enabled);
		bool PlayStinger(const std::string& eventReference);
		bool QueueTransition(const std::string& eventReference, MusicSync sync);
		void Update(bool paused);
		int GetCurrentBeat() const { return m_Beat; }
		int GetCurrentBar() const { return m_Bar; }
		float GetIntensity() const { return m_Intensity; }
		bool IsPlaying() const { return m_Bed && m_Bed->IsPlaying(); }
		const std::string& GetReference() const { return m_Reference; }
		void SetTempoCallback(std::function<void(int, int)> callback) { m_TempoCallback = std::move(callback); }
		void SetMarkerCallback(std::function<void(const std::string&)> callback) { m_MarkerCallback = std::move(callback); }

	private:
		friend class AudioScriptBindings;
		Ref<AudioEventInstance> Prepare(const std::string& reference, bool oneShot);
		bool ApplyParameters(Ref<AudioEventInstance> instance);
		bool Activate(Ref<AudioEventInstance> instance);
		Ref<AudioEventInstance> m_Bed, m_Pending;
		std::vector<Ref<AudioEventInstance>> m_Stingers;
		std::string m_Reference, m_State;
		std::map<std::string, bool> m_Layers;
		MusicSync m_Sync = MusicSync::Immediate;
		float m_Intensity = 0.0f;
		bool m_HasIntensity = false, m_Paused = false, m_Stopping = false;
		int m_Beat = 0, m_Bar = 0;
		uint64_t m_RetryRevision = 0, m_TransitionAfterSequence = 0;
		std::function<void(int, int)> m_TempoCallback, m_ScriptTempoCallback;
		std::function<void(const std::string&)> m_MarkerCallback, m_ScriptMarkerCallback;
	};
}
