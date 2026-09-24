// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AudioAccessibilitySettings.h"
#include "DialogueDirector.h"
#include <filesystem>
#include <functional>

namespace Lux
{
	struct AccessibleSubtitle
	{
		SubtitleEvent Event;
		glm::vec4 SpeakerColor{ 1.0f };
		float Age = 0.0f, Remaining = 0.0f;
		bool Finished = false;
	};

	struct SoundEvent
	{
		uint64_t Handle = 0;
		bool Active = false;
		AudioCategory Category = AudioCategory::SFX;
		glm::vec3 Position{ 0.0f }, Direction{ 0.0f };
		float Intensity = 0.0f;
	};

	struct AudioAccessibilityView
	{
		glm::vec3 Position{ 0.0f }, Forward{ 0, 0, -1 }, Up{ 0, 1, 0 };
		glm::mat4 ViewProjection{ 1.0f };
		bool HasCamera = false;
	};

	// One active runtime scene, main thread only. Does not own event instances or Scene objects.
	class AudioAccessibility
	{
	public:
		static bool BeginScene(const void* owner, const AudioAccessibilityConfig& config, const std::filesystem::path& preferences,
			std::function<void(const SubtitleEvent&)> captionSink);
		static void EndScene(const void* owner);
		static void ReleaseMixer();
		static void Track(AudioEventInstance* instance);
		static void RefreshSubtitle(const SubtitleEvent& event);
		static void SetDescribing(bool describing);
		static void OnSubtitle(const SubtitleEvent& event);
		static void Update(const void* owner, float timestep, bool paused, const std::string& language,
			const AudioAccessibilityView& view, bool describing);
		static const AudioAccessibilityConfig& GetConfig();
		static const AudioAccessibilityPreferences& GetPreferences();
		static bool ApplyPreferences(const AudioAccessibilityPreferences& preferences);
		static bool SavePreferences();
		static bool LoadPreferences();
		static bool HasBus(AudioCategory category);
		static bool IsActive();
		static const std::vector<AccessibleSubtitle>& GetSubtitles();
		static const std::vector<SoundEvent>& GetSoundCues();
		static glm::vec3 DirectionTo(const glm::vec3& position);
		static void SetSoundCallback(std::function<void(const SoundEvent&)> callback);
	private:
		friend class AudioScriptBindings;
		static void SetScriptSoundCallback(std::function<void(const SoundEvent&)> callback);
	};
}
