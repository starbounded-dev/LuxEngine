// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Audio/AudioMixerSettings.h"
#include "Lux/Editor/EditorPanel.h"

#include <functional>
#include <string>

namespace Lux
{
	class Scene;

	// View → Audio Mixer: edits the project's native mixer (ProjectAudioSettings::Mixer). Volume and
	// mute apply live on the native backend; adding, renaming or removing buses and the reverb
	// return's settings apply when the audio engine restarts ("Apply"), which is refused during Play.
	class AudioMixerPanel : public EditorPanel
	{
	public:
		using UndoPushFn = std::function<void(const std::string& label, std::function<void()> undo, std::function<void()> redo)>;

		void OnImGuiRender(bool& isOpen) override;
		void OnProjectChanged(const Ref<Project>& project) override;
		void SetSceneContext(const Ref<Scene>& context) override { m_Context = context; }
		void SetUndoCallback(UndoPushFn callback) { m_PushUndo = std::move(callback); }

	private:
		AudioMixerSettings* GetMixer();
		// Replaces the mixer (undo/redo), applying volume and mute live.
		void SetMixer(const AudioMixerSettings& mixer);
		// After an edit completes (no widget active): one undo step and the dirty flag.
		void CommitEdits();
		void ApplyLive(const AudioMixerBus& bus) const;

		void UI_Toolbar(AudioMixerSettings& mixer);
		void UI_Buses(AudioMixerSettings& mixer);
		void UI_SelectedBus(AudioMixerSettings& mixer);
		void UI_Reverb(AudioMixerSettings& mixer);

		Ref<Project> m_Project;
		Ref<Scene> m_Context;
		UndoPushFn m_PushUndo;
		AudioMixerSettings m_Committed;     // the mixer after the last completed edit
		std::string m_Selected = "bus:/";   // the bus new buses are added under, and that renames apply to
		char m_NameBuffer[64] = {};
		std::string m_Error;
		bool m_Dirty = false;               // unsaved project changes
		bool m_NeedsRestart = false;        // structural changes the running engine does not have yet
	};
}
