// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioAccessibility.h"
#include "AudioAccessibilityMixer.h"
#include "AudioEngine.h"
#include "Lux/Utilities/FileSystem.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr size_t kMaxSources = 512, kMaxSubtitles = 128, kMaxPreferenceBytes = 65536;
		constexpr uint64_t kCaptionBit = uint64_t{ 1 } << 63;
		struct Source
		{
			WeakRef<AudioEventInstance> Instance;
			uint64_t Token = 0;
			AudioEventAccessibility Metadata;
			SubtitleEvent Caption;
			SoundEvent Cue;
			bool Started = false, Spatial = false;
		};
		const void* s_Owner = nullptr;
		AudioAccessibilityConfig s_Config;
		AudioAccessibilityPreferences s_Preferences;
		std::filesystem::path s_PreferencePath;
		AudioAccessibilityMixer s_Mixer;
		uint64_t s_MixerRevision = 0;
		bool s_Describing = false;
		AudioAccessibilityView s_View;
		std::vector<Source> s_Sources;
		std::vector<AccessibleSubtitle> s_Subtitles;
		std::vector<SoundEvent> s_Cues, s_Notifications;
		std::function<void(const SubtitleEvent&)> s_CaptionSink;
		std::function<void(const SoundEvent&)> s_SoundCallback, s_ScriptSoundCallback;

		bool OffScreen(const glm::vec3& position)
		{
			if (!s_View.HasCamera)
				return false;
			const auto clip = s_View.ViewProjection * glm::vec4(position, 1.0f);
			return clip.w <= 0 || std::abs(clip.x) > clip.w || std::abs(clip.y) > clip.w || clip.z < 0 || clip.z > clip.w;
		}
		void EndSource(Source& source)
		{
			if (!source.Started)
				return;
			if (source.Caption.Shown && s_CaptionSink)
			{
				source.Caption.Shown = false;
				s_CaptionSink(source.Caption);
			}
			if (source.Cue.Active)
			{
				source.Cue.Active = false;
				s_Notifications.push_back(source.Cue);
			}
		}
	}

	bool AudioAccessibility::BeginScene(const void* owner, const AudioAccessibilityConfig& config,
		const std::filesystem::path& preferences, std::function<void(const SubtitleEvent&)> captionSink)
	{
		if (!owner || !config.Validate())
			return false;
		const bool sameProject = !s_PreferencePath.empty() && s_PreferencePath == preferences;
		EndScene(s_Owner);
		s_Owner = owner;
		s_Config = config;
		s_PreferencePath = preferences;
		s_CaptionSink = std::move(captionSink);
		if (!sameProject)
		{
			s_Preferences = config.Defaults;
			LoadPreferences(); // Missing file uses defaults; malformed files report and retain defaults.
		}
		s_Sources.reserve(kMaxSources);
		s_Subtitles.reserve(kMaxSubtitles);
		s_Cues.reserve(kMaxSources);
		s_Notifications.reserve(kMaxSources * 2);
		s_MixerRevision = AudioEngine::GetBankRevision();
		const bool configured = s_Mixer.Configure(config);
		return s_Mixer.Apply(s_Preferences, false) && configured;
	}

	void AudioAccessibility::EndScene(const void* owner)
	{
		if (owner != s_Owner)
			return;
		// Queue caption hides before DialogueDirector drains its final notifications.
		for (auto& source : s_Sources)
			EndSource(source);
		auto ended = std::move(s_Notifications);
		auto native = s_SoundCallback;
		auto script = s_ScriptSoundCallback;
		s_Sources.clear();
		s_Subtitles.clear();
		s_Cues.clear();
		s_Notifications.clear();
		s_CaptionSink = {};
		s_ScriptSoundCallback = {};
		s_SoundCallback = {};
		s_Owner = nullptr;
		s_Describing = false;
		ReleaseMixer();
		for (const auto& event : ended)
		{
			if (event.Active)
				continue;
			try
			{
				if (native)
					native(event);
			}
			catch (const std::exception& error)
			{
				LUX_CORE_ERROR_TAG("Audio", "Sound cue teardown listener failed: {}", error.what());
			}
			if (script)
				script(event);
		}
	}

	void AudioAccessibility::ReleaseMixer()
	{
		s_Mixer.Reset();
		s_MixerRevision = 0;
	}

	void AudioAccessibility::Track(AudioEventInstance* instance)
	{
		if (!s_Owner || instance->IsAccessibilitySuppressed())
			return;
		const auto metadata = s_Config.Events.find(instance->GetReference());
		if (metadata == s_Config.Events.end() || (metadata->second.Captions.empty() && !metadata->second.VisualCue))
			return;
		if (!instance->MonitorPlayback())
			return;
		const uint64_t token = instance->GetPlaybackToken();
		std::erase_if(s_Sources, [&](Source& source)
		{
			if (source.Token != token)
				return false;
			EndSource(source);
			return true;
		});
		if (s_Sources.size() >= kMaxSources || token >= kCaptionBit)
		{
			LUX_CORE_ERROR_TAG("Audio", "Audio caption/cue tracking capacity exhausted");
			return;
		}
		Source source;
		source.Instance = instance;
		source.Token = token;
		source.Metadata = metadata->second;
		source.Spatial = instance->Is3D();
		source.Caption.Handle = token | kCaptionBit;
		source.Caption.IsCaption = true;
		source.Caption.Key = instance->GetReference();
		source.Cue.Handle = source.Caption.Handle;
		source.Cue.Category = metadata->second.Category;
		s_Sources.push_back(std::move(source));
	}

	void AudioAccessibility::RefreshSubtitle(const SubtitleEvent& event)
	{
		for (auto& subtitle : s_Subtitles)
		{
			if (subtitle.Event.Handle != event.Handle)
				continue;
			subtitle.Event.SpeakerPosition = event.SpeakerPosition;
			subtitle.Event.IsOffScreen = event.IsOffScreen;
			break;
		}
	}

	void AudioAccessibility::SetDescribing(bool describing)
	{
		if (!s_Owner || describing == s_Describing)
			return;
		s_Describing = describing;
		s_Mixer.Apply(s_Preferences, describing);
	}

	void AudioAccessibility::OnSubtitle(const SubtitleEvent& event)
	{
		if (!s_Owner)
			return;
		auto found = std::find_if(s_Subtitles.begin(), s_Subtitles.end(), [&](const auto& subtitle) { return subtitle.Event.Handle == event.Handle; });
		if (!event.Shown)
		{
			if (found != s_Subtitles.end())
			{
				found->Finished = true;
				found->Remaining = std::max(0.0f, found->Age * (s_Preferences.DurationMultiplier - 1.0f));
			}
			return;
		}
		if ((event.IsCaption ? !s_Preferences.Captions : !s_Preferences.Subtitles))
			return;
		if (found != s_Subtitles.end())
			s_Subtitles.erase(found);
		if (s_Subtitles.size() >= kMaxSubtitles)
			s_Subtitles.erase(s_Subtitles.begin());
		AccessibleSubtitle subtitle;
		subtitle.Event = event;
		if (const auto color = s_Config.SpeakerColors.find(event.SpeakerName); color != s_Config.SpeakerColors.end())
			subtitle.SpeakerColor = color->second;
		s_Subtitles.push_back(std::move(subtitle));
	}

	glm::vec3 AudioAccessibility::DirectionTo(const glm::vec3& position)
	{
		const auto delta = position - s_View.Position;
		if (glm::dot(delta, delta) < 0.000001f)
			return {};
		const auto direction = glm::normalize(delta);
		const auto right = glm::cross(s_View.Forward, s_View.Up);
		return { glm::dot(direction, right), glm::dot(direction, s_View.Up), glm::dot(direction, s_View.Forward) };
	}

	void AudioAccessibility::Update(const void* owner, float timestep, bool paused, const std::string& language,
		const AudioAccessibilityView& view, bool describing)
	{
		if (owner != s_Owner || !s_Owner)
			return;
		LUX_PROFILE_FUNCTION_AUTO;
		s_View = view;
		if (s_MixerRevision != AudioEngine::GetBankRevision())
		{
			s_MixerRevision = AudioEngine::GetBankRevision();
			s_Mixer.Configure(s_Config);
			s_Mixer.Apply(s_Preferences, describing);
		}
		else if (s_Describing != describing)
			s_Mixer.Apply(s_Preferences, describing);
		s_Describing = describing;
		const float elapsed = !paused && std::isfinite(timestep) ? std::max(timestep, 0.0f) : 0.0f;
		for (auto& subtitle : s_Subtitles)
		{
			subtitle.Age += elapsed;
			if (subtitle.Finished)
				subtitle.Remaining -= elapsed;
		}
		std::erase_if(s_Subtitles, [&](const auto& subtitle)
		{
			return (subtitle.Event.IsCaption ? !s_Preferences.Captions : !s_Preferences.Subtitles) ||
				(subtitle.Finished && subtitle.Remaining <= 0) ||
				(s_Preferences.DurationMultiplier < 1 && subtitle.Event.Duration > 0 && subtitle.Age >= subtitle.Event.Duration * s_Preferences.DurationMultiplier);
		});
		s_Cues.clear();
		std::erase_if(s_Sources, [&](Source& source)
		{
			if (!source.Instance.IsValid() || source.Instance->GetPlaybackToken() != source.Token || !source.Instance->IsValid())
			{
				EndSource(source);
				return true;
			}
			const auto status = source.Instance->GetPlaybackStatus();
			const auto position = source.Instance->GetPosition();
			source.Caption.SpeakerPosition = position;
			source.Caption.IsOffScreen = source.Spatial && OffScreen(position);
			RefreshSubtitle(source.Caption);
			source.Cue.Position = position;
			source.Cue.Direction = source.Spatial ? DirectionTo(position) : glm::vec3(0.0f);
			const float distance = source.Spatial ? glm::distance(position, view.Position) : 0.0f;
			source.Cue.Intensity = source.Metadata.Intensity * std::clamp(source.Instance->GetVolume(), 0.0f, 1.0f) * std::clamp(1.0f - distance / source.Metadata.MaxDistance, 0.0f, 1.0f);
			if (!paused && status.Started && !status.Error && !source.Started)
			{
				source.Started = true;
				auto caption = source.Metadata.Captions.find(language);
				if (caption == source.Metadata.Captions.end())
					caption = source.Metadata.Captions.find(s_Config.DefaultLanguage);
				if (caption != source.Metadata.Captions.end() && !caption->second.empty() && distance <= source.Metadata.MaxDistance && s_CaptionSink)
				{
					source.Caption.Text = caption->second;
					source.Caption.Language = caption->first;
					source.Caption.Duration = status.Duration;
					source.Caption.Shown = true;
					s_CaptionSink(source.Caption);
				}
				if (source.Metadata.VisualCue)
				{
					source.Cue.Active = true;
					s_Notifications.push_back(source.Cue);
				}
			}
			if (status.Error || status.Stopped)
			{
				EndSource(source);
				return true;
			}
			if (source.Cue.Active && s_Preferences.VisualCues && source.Cue.Intensity > 0)
				s_Cues.push_back(source.Cue);
			return false;
		});
		// All source mutations finish before gameplay can stop or create sounds from callbacks.
		if (s_Notifications.empty())
			return;
		auto notifications = std::move(s_Notifications);
		s_Notifications.clear();
		for (const auto& event : notifications)
		{
			if (s_Owner != owner)
				break;
			auto native = s_SoundCallback;
			auto script = s_ScriptSoundCallback;
			try
			{
				if (native)
					native(event);
			}
			catch (const std::exception& error)
			{
				LUX_CORE_ERROR_TAG("Audio", "Sound cue listener failed: {}", error.what());
			}
			if (script)
				script(event);
		}
	}

	const AudioAccessibilityConfig& AudioAccessibility::GetConfig()

	{

		return s_Config;

	}
	const AudioAccessibilityPreferences& AudioAccessibility::GetPreferences()
	{
		return s_Preferences;
	}
	const std::vector<AccessibleSubtitle>& AudioAccessibility::GetSubtitles()
	{
		return s_Subtitles;
	}
	const std::vector<SoundEvent>& AudioAccessibility::GetSoundCues()
	{
		return s_Cues;
	}
	bool AudioAccessibility::HasBus(AudioCategory category)
	{
		return s_Mixer.HasBus(category);
	}
	bool AudioAccessibility::IsActive()
	{
		return s_Owner != nullptr;
	}
	void AudioAccessibility::SetSoundCallback(std::function<void(const SoundEvent&)> callback)
	{
		s_SoundCallback = std::move(callback);
	}
	void AudioAccessibility::SetScriptSoundCallback(std::function<void(const SoundEvent&)> callback)
	{
		s_ScriptSoundCallback = std::move(callback);
	}

	bool AudioAccessibility::ApplyPreferences(const AudioAccessibilityPreferences& preferences)
	{
		if (!s_Owner || !preferences.Validate())
			return false;
		if (!s_Mixer.Apply(preferences, s_Describing))
		{
			s_Mixer.Apply(s_Preferences, s_Describing);
			return false;
		}
		s_Preferences = preferences;
		return true;
	}

	bool AudioAccessibility::LoadPreferences()
	{
		if (s_PreferencePath.empty() || !FileSystem::Exists(s_PreferencePath))
			return true;
		std::ifstream file(s_PreferencePath, std::ios::binary | std::ios::ate);
		if (!file || file.tellg() < 0 || static_cast<uint64_t>(file.tellg()) > kMaxPreferenceBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot read audio preferences '{}'", s_PreferencePath.string());
			return false;
		}
		std::string text(static_cast<size_t>(file.tellg()), '\0');
		file.seekg(0);
		if (!file.read(text.data(), text.size()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot read audio preferences payload");
			return false;
		}
		try
		{
			AudioAccessibilityPreferences preferences;
			if (!preferences.DeserializeYAML(YAML::Load(text)))
				return false;
			if (s_MixerRevision != 0)
				return ApplyPreferences(preferences);
			s_Preferences = preferences;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot parse audio preferences '{}': {}", s_PreferencePath.string(), error.what());
			return false;
		}
	}

	bool AudioAccessibility::SavePreferences()
	{
		if (!s_Owner || s_PreferencePath.empty())
			return false;
		std::error_code error;
		std::filesystem::create_directories(s_PreferencePath.parent_path(), error);
		if (error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot create preferences directory: {}", error.message());
			return false;
		}
		YAML::Emitter out;
		s_Preferences.SerializeYAML(out);
		const auto temporary = std::filesystem::path(s_PreferencePath.string() + ".tmp");
		std::ofstream file(temporary);
		file << out.c_str();
		file.close();
		if (!file)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot write audio preferences '{}'", temporary.string());
			return false;
		}
		if (!FileSystem::ReplaceFileAtomically(temporary, s_PreferencePath))
			return false;
		return true;
	}
}
