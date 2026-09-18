#pragma once

#include "DialogueTable.h"
#include "AudioEventInstance.h"
#include <functional>
#include <deque>

namespace Lux
{
	struct DialogueSpeaker
	{
		bool Valid = false, IsOffScreen = false;
		std::string Name;
		glm::vec3 Position{ 0.0f };
	};

	struct SubtitleEvent
	{
		uint64_t Handle = 0;
		bool Shown = false;
		bool IsCaption = false, IsDescription = false;
		std::string Key, Language, Text, SpeakerName;
		UUID SpeakerEntity = 0;
		glm::vec3 SpeakerPosition{ 0.0f };
		float Duration = 0.0f;
		bool IsOffScreen = false;
	};

	// Main-thread scene service. FMOD callback threads never access this object or its resolver.
	class DialogueDirector
	{
	public:
		DialogueDirector() = default;
		DialogueDirector(const DialogueDirector&) = delete;
		DialogueDirector& operator=(const DialogueDirector&) = delete;
		bool Configure(Ref<DialogueTable> table, const std::string& language,
			std::function<DialogueSpeaker(UUID)> resolver);
		bool SetLanguage(const std::string& language);
		const std::string& GetLanguage() const { return m_Language; }
		bool SetQueueMode(DialogueQueueMode mode);
		uint64_t Speak(const std::string& key, UUID speaker = 0);
		uint64_t Describe(const std::string& key);
		bool IsDescribing() const;
		void PublishCaption(const SubtitleEvent& event);
		uint64_t Bark(const std::string& key, UUID speaker);
		void Stop(uint64_t handle, bool allowFadeOut = true);
		void StopAll();
		void RemoveSpeaker(UUID speaker);
		void Clear();
		void Update(float timestep, bool paused);
		bool IsSpeaking(UUID speaker) const;
		bool IsActive(uint64_t handle) const;
		size_t GetQueueLength() const { return m_Queue.size(); }
		void SetSubtitleCallback(std::function<void(const SubtitleEvent&)> callback) { m_SubtitleCallback = std::move(callback); }

	private:
		friend class AudioScriptBindings;
		struct Voice
		{
			SubtitleEvent Subtitle;
			Ref<AudioEventInstance> Instance;
			DialoguePriority Priority = DialoguePriority::Normal;
			bool Interruptible = true, Programmer = false;
		};
		struct RecentBark { std::string Key; glm::vec3 Position; float Remaining; };
		uint64_t SpeakImpl(const std::string& key, UUID speaker, bool description);
		Voice Prepare(const std::string& key, UUID speaker);
		bool Start(Voice& voice);
		bool UpdateVoice(Voice& voice);
		void Retire(Voice& voice, bool fade);
		void Dispatch();
		Ref<DialogueTable> m_Table;
		std::string m_Language = "en";
		DialogueQueueMode m_Mode = DialogueQueueMode::Queue;
		Voice m_Current;
		std::vector<Voice> m_Queue, m_Barks;
		std::vector<Ref<AudioEventInstance>> m_Retired, m_DescriptionFades;
		std::vector<RecentBark> m_RecentBarks;
		std::deque<SubtitleEvent> m_Notifications;
		std::function<DialogueSpeaker(UUID)> m_ResolveSpeaker;
		std::function<void(const SubtitleEvent&)> m_SubtitleCallback, m_ScriptSubtitleCallback;
		uint64_t m_Generation = 0;
		bool m_Paused = false, m_Dispatching = false, m_Clearing = false;
	};
}
