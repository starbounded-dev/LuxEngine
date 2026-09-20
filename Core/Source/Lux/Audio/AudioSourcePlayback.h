// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "AudioEventInstance.h"
#include "AudioListener.h"
#include <map>
namespace Lux
{
	struct AudioSourceComponent;
	// Main-thread component playback intent survives releasing an inaudible Studio instance.
	class AudioSourcePlayback
	{
	public:
		void Update(const AudioSourceComponent& source, const glm::mat4& transform, const AudioListener::States& listeners, bool paused, bool allowAwake = true);
		Ref<AudioEventInstance> GetInstance() const { return m_Instance; }
		bool IsCulled() const { return m_Culled; }
		bool IsPlaying() const;
		void Play();
		void Stop(bool fade = true);
		bool SetParameter(const std::string& name, float value);
		float GetParameter(const std::string& name) const;
		bool SetParameterLabel(const std::string& name, const std::string& value);
		int GetTimelinePosition() const;
		void SetTimelinePosition(int value);
		static bool OutOfRange(const glm::vec3& position, float maximum, const AudioListener::States& listeners, bool wasCulled);
	private:
		void Apply();
		Ref<AudioEventInstance> m_Instance;
		std::string m_Guid;
		uint64_t m_Revision = 0;
		bool m_Attempted = false, m_Metadata = false, m_Spatial = false, m_OneShot = false;
		bool m_Culled = false, m_AwakeHandled = false, m_WantsPlayback = false, m_Started = false;
		bool m_Paused = false, m_ScenePaused = false;
		mutable bool m_ParameterWarning = false;
		int m_Priority = 128, m_Timeline = 0;
		float m_Maximum = 0, m_Volume = 1, m_Pitch = 1;
		glm::mat4 m_Transform{ 1.0f };
		std::map<std::string, float> m_Parameters;
		std::map<std::string, std::string> m_Labels;
	};
}
