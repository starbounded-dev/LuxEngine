#include "lpch.h"
#include "DialogueDirector.h"
#include "AudioEngine.h"
#include <algorithm>
#include <cmath>

namespace Lux
{
	namespace
	{
		uint64_t s_NextDialogueHandle = 1;
		constexpr size_t kQueueCapacity = 64, kBarkCapacity = 32, kRecentBarkCapacity = 256;
		constexpr size_t kDispatchCapacity = 256, kVoiceCapacity = 128;
	}

	bool DialogueDirector::Configure(Ref<DialogueTable> table, const std::string& language,
		std::function<DialogueSpeaker(UUID)> resolver)
	{
		if (!table || !table->Validate() || !DialogueTable::ValidLanguage(language) || !resolver)
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue requires a valid table, language and speaker resolver");
			return false;
		}
		Clear();
		// Editor assets can be edited during Play; a scene uses a stable startup snapshot.
		m_Table = Ref<DialogueTable>::Create();
		m_Table->DefaultLanguage = table->DefaultLanguage;
		m_Table->Lines = table->Lines;
		m_Table->BarkCooldown = table->BarkCooldown;
		m_Table->BarkRadius = table->BarkRadius;
		m_Language = language;
		m_ResolveSpeaker = std::move(resolver);
		m_Generation = AudioEngine::GetEventGeneration();
		return true;
	}

	bool DialogueDirector::SetLanguage(const std::string& language)
	{
		if (!DialogueTable::ValidLanguage(language))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid dialogue language '{}'", language);
			return false;
		}
		m_Language = language;
		return true;
	}

	bool DialogueDirector::SetQueueMode(DialogueQueueMode mode)
	{
		if (mode > DialogueQueueMode::DropIfBusy)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid dialogue queue mode");
			return false;
		}
		m_Mode = mode;
		return true;
	}

	DialogueDirector::Voice DialogueDirector::Prepare(const std::string& key, UUID speaker)
	{
		Voice voice;
		if (m_Clearing || !m_Table || !m_ResolveSpeaker)
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue is unavailable: assign a Dialogue Table in Project Settings");
			return voice;
		}
		if (m_Queue.size() + m_Barks.size() + m_Retired.size() + (m_Current.Instance ? 1 : 0) >= kVoiceCapacity)
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue voice budget exhausted; '{}' rejected", key);
			return voice;
		}
		const auto line = m_Table->Lines.find(key);
		if (line == m_Table->Lines.end())
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue key '{}' is absent from the table", key);
			return voice;
		}
		auto translation = line->second.Translations.find(m_Language);
		if (translation == line->second.Translations.end())
			translation = line->second.Translations.find(m_Table->DefaultLanguage);
		const auto resolved = m_ResolveSpeaker(speaker);
		if (!resolved.Valid || translation == line->second.Translations.end())
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue '{}' has an unavailable speaker or default translation", key);
			return voice;
		}
		auto instance = AudioEventInstance::Create(line->second.Event.Guid);
		if (!instance)
			return voice;
		if (instance->IsSnapshot() || !instance->IsOneShot())
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue '{}' requires a finite, non-snapshot event", key);
			return voice;
		}
		const auto& text = translation->second;
		if (!instance->MonitorPlayback() || (!text.AudioKey.empty() && !instance->SetProgrammerSound(text.AudioKey)))
			return voice;
		if (!s_NextDialogueHandle)
		{
			LUX_CORE_ERROR_TAG("Audio", "Dialogue handle space exhausted");
			return voice;
		}
		voice.Subtitle.Handle = s_NextDialogueHandle++;
		voice.Subtitle.Key = key;
		voice.Subtitle.Language = translation->first;
		voice.Subtitle.Text = text.Text;
		voice.Subtitle.SpeakerName = text.SpeakerName.empty() ? resolved.Name : text.SpeakerName;
		voice.Subtitle.SpeakerEntity = speaker;
		voice.Subtitle.SpeakerPosition = resolved.Position;
		voice.Subtitle.IsOffScreen = resolved.IsOffScreen;
		voice.Instance = instance;
		voice.Priority = line->second.Priority;
		voice.Interruptible = line->second.Interruptible;
		voice.Programmer = !text.AudioKey.empty();
		return voice;
	}

	bool DialogueDirector::Start(Voice& voice)
	{
		const auto speaker = m_ResolveSpeaker(voice.Subtitle.SpeakerEntity);
		if (!speaker.Valid || !voice.Instance->IsValid())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot start dialogue '{}': speaker or event is no longer available", voice.Subtitle.Key);
			return false;
		}
		voice.Instance->SetScenePaused(m_Paused);
		voice.Instance->Set3DAttributes(speaker.Position, {}, { 0, 0, -1 }, { 0, 1, 0 });
		return voice.Instance->Start();
	}

	uint64_t DialogueDirector::Speak(const std::string& key, UUID speaker)
	{
		if (m_Current.Instance && m_Mode == DialogueQueueMode::DropIfBusy)
			return 0;
		auto voice = Prepare(key, speaker);
		if (!voice.Instance)
			return 0;
		const auto handle = voice.Subtitle.Handle;
		const bool interrupt = m_Current.Instance && m_Mode == DialogueQueueMode::Interrupt &&
			m_Current.Interruptible && voice.Priority >= m_Current.Priority;
		if (m_Current.Instance && !interrupt)
		{
			if (m_Queue.size() >= kQueueCapacity)
			{
				LUX_CORE_ERROR_TAG("Audio", "Dialogue queue is full; '{}' rejected", key);
				return 0;
			}
			const auto insertion = std::find_if(m_Queue.begin(), m_Queue.end(), [&](const Voice& queued) { return queued.Priority < voice.Priority; });
			m_Queue.insert(insertion, std::move(voice));
			return handle;
		}
		// Start must succeed before replacing a valid line. SDK callbacks are dispatched later.
		if (!Start(voice))
			return 0;
		Retire(m_Current, false);
		m_Current = std::move(voice);
		return handle;
	}

	uint64_t DialogueDirector::Bark(const std::string& key, UUID speaker)
	{
		if (!speaker || m_Barks.size() >= kBarkCapacity || IsSpeaking(speaker))
			return 0;
		auto voice = Prepare(key, speaker);
		if (!voice.Instance)
			return 0;
		if (!voice.Instance->Is3D() || !voice.Interruptible)
		{
			LUX_CORE_ERROR_TAG("Audio", "Bark '{}' requires an interruptible positional event", key);
			return 0;
		}
		for (const auto& recent : m_RecentBarks)
		{
			if (recent.Key == key && glm::distance(recent.Position, voice.Subtitle.SpeakerPosition) <= m_Table->BarkRadius)
				return 0;
		}
		if (m_RecentBarks.size() >= kRecentBarkCapacity)
			return 0; // Bounded cooldown history: do not forget and replay a recent bark.
		if (!Start(voice))
			return 0;
		const auto handle = voice.Subtitle.Handle;
		m_RecentBarks.push_back({ key, voice.Subtitle.SpeakerPosition, m_Table->BarkCooldown });
		m_Barks.push_back(std::move(voice));
		return handle;
	}

	void DialogueDirector::Retire(Voice& voice, bool fade)
	{
		if (!voice.Instance)
			return;
		voice.Instance->Stop(fade);
		if (fade && voice.Instance->IsValid())
			m_Retired.push_back(voice.Instance);
		if (voice.Subtitle.Shown)
		{
			voice.Subtitle.Shown = false;
			m_Notifications.push_back(voice.Subtitle);
		}
		voice = {};
	}

	void DialogueDirector::Stop(uint64_t handle, bool fade)
	{
		if (!handle)
			return;
		if (m_Current.Subtitle.Handle == handle)
			Retire(m_Current, fade);
		std::erase_if(m_Queue, [&](const Voice& voice) { return voice.Subtitle.Handle == handle; });
		for (auto& voice : m_Barks)
		{
			if (voice.Subtitle.Handle == handle)
				Retire(voice, fade);
		}
		std::erase_if(m_Barks, [](const Voice& voice) { return !voice.Instance; });
	}

	void DialogueDirector::StopAll()
	{
		m_Queue.clear();
		Retire(m_Current, false);
		for (auto& voice : m_Barks)
			Retire(voice, false);
		m_Barks.clear();
		m_Retired.clear();
	}

	void DialogueDirector::RemoveSpeaker(UUID speaker)
	{
		std::erase_if(m_Queue, [&](const Voice& voice) { return voice.Subtitle.SpeakerEntity == speaker; });
		if (m_Current.Instance && m_Current.Subtitle.SpeakerEntity == speaker)
			Retire(m_Current, false);
		for (auto& voice : m_Barks)
		{
			if (voice.Subtitle.SpeakerEntity == speaker)
				Retire(voice, false);
		}
		std::erase_if(m_Barks, [](const Voice& voice) { return !voice.Instance; });
	}

	void DialogueDirector::Clear()
	{
		m_Clearing = true;
		StopAll();
		m_RecentBarks.clear();
		m_Table = nullptr;
		m_ResolveSpeaker = {};
		m_Mode = DialogueQueueMode::Queue;
		Dispatch();
		m_Clearing = false;
	}

	bool DialogueDirector::IsSpeaking(UUID speaker) const
	{
		return (m_Current.Instance && m_Current.Subtitle.SpeakerEntity == speaker) ||
			std::any_of(m_Barks.begin(), m_Barks.end(), [&](const Voice& voice) { return voice.Subtitle.SpeakerEntity == speaker; });
	}

	bool DialogueDirector::IsActive(uint64_t handle) const
	{
		const auto matches = [&](const Voice& voice) { return voice.Subtitle.Handle == handle; };
		return handle && (matches(m_Current) || std::any_of(m_Queue.begin(), m_Queue.end(), matches) || std::any_of(m_Barks.begin(), m_Barks.end(), matches));
	}

	bool DialogueDirector::UpdateVoice(Voice& voice)
	{
		if (!voice.Instance)
			return false;
		const auto speaker = m_ResolveSpeaker(voice.Subtitle.SpeakerEntity);
		const auto status = voice.Instance->GetPlaybackStatus();
		if (!speaker.Valid || !voice.Instance->IsValid() || status.Error)
		{
			Retire(voice, false);
			return false;
		}
		voice.Instance->Set3DAttributes(speaker.Position, {}, { 0, 0, -1 }, { 0, 1, 0 });
		voice.Subtitle.SpeakerPosition = speaker.Position;
		voice.Subtitle.IsOffScreen = speaker.IsOffScreen;
		if (!voice.Subtitle.Shown && (voice.Programmer ? status.SoundStarted : status.Started))
		{
			voice.Subtitle.Shown = true;
			voice.Subtitle.Duration = status.Duration;
			m_Notifications.push_back(voice.Subtitle);
		}
		if (status.Stopped)
		{
			Retire(voice, false);
			return false;
		}
		return true;
	}

	void DialogueDirector::Update(float timestep, bool paused)
	{
		m_Paused = paused;
		if (m_Generation != AudioEngine::GetEventGeneration())
		{
			StopAll();
			m_RecentBarks.clear();
			m_Generation = AudioEngine::GetEventGeneration();
		}
		if (m_Current.Instance)
			m_Current.Instance->SetScenePaused(paused);
		for (auto& voice : m_Barks)
			voice.Instance->SetScenePaused(paused);
		for (auto& instance : m_Retired)
			instance->SetScenePaused(paused);
		std::erase_if(m_Retired, [](const auto& instance) { return !instance->IsPlaying(); });
		if (!paused && m_Table)
		{
			const float elapsed = std::isfinite(timestep) ? std::max(timestep, 0.0f) : 0.0f;
			for (auto& recent : m_RecentBarks)
				recent.Remaining -= elapsed;
			std::erase_if(m_RecentBarks, [](const RecentBark& recent) { return recent.Remaining <= 0.0f; });
			UpdateVoice(m_Current);
			std::erase_if(m_Barks, [&](Voice& voice) { return !UpdateVoice(voice); });
			while (!m_Current.Instance && !m_Queue.empty())
			{
				auto voice = std::move(m_Queue.front());
				m_Queue.erase(m_Queue.begin());
				if (Start(voice))
					m_Current = std::move(voice);
			}
		}
		// Mutations finish before user code; callbacks may safely stop, enqueue or clear dialogue.
		Dispatch();
	}

	void DialogueDirector::Dispatch()
	{
		if (m_Dispatching)
			return;
		m_Dispatching = true;
		size_t delivered = 0;
		while (!m_Notifications.empty() && delivered++ < kDispatchCapacity)
		{
			auto event = std::move(m_Notifications.front());
			m_Notifications.pop_front();
			auto native = m_SubtitleCallback;
			auto script = m_ScriptSubtitleCallback;
			try
			{
				if (native)
					native(event);
			}
			catch (const std::exception& error)
			{
				LUX_CORE_ERROR_TAG("Audio", "Subtitle listener failed: {}", error.what());
			}
			if (script)
				script(event);
		}
		m_Dispatching = false;
	}
}
