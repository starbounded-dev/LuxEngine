// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Audio/AudioEventInstance.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Editor/EditorPanel.h"

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lux
{
	// Edits Sound assets (.lsound): one tab per open Sound, the node tree, clip waveforms with start
	// offset and loop-region handles, parameters and their curves, and an audition voice owned by the
	// panel so it plays in edit mode. Edits change the loaded asset, go on the editor's undo stack,
	// and are written by Save. Main thread only.
	class SoundEditorPanel : public EditorPanel
	{
	public:
		using UndoPushFn = std::function<void(const std::string& label, std::function<void()> undo, std::function<void()> redo)>;

		SoundEditorPanel();
		~SoundEditorPanel() override;

		void OnImGuiRender(bool& isOpen) override;
		void OnEvent(Event& event) override;
		void OnClose() override { m_KeyboardFocus = false; StopAudition(); }
		void OnProjectChanged(const Ref<Project>& project) override;

		void OpenSound(AssetHandle handle);
		void SetUndoCallback(UndoPushFn callback) { m_PushUndo = std::move(callback); }
#ifndef LUX_DIST
		static bool RunPersistenceSelfTests(std::vector<std::string>& failures);
#endif

	private:
		// Every authored field of a Sound, so an edit can be captured, compared, undone and reverted
		// in one place, valid or not (an edit in progress may be briefly invalid).
		struct SoundState
		{
			std::vector<SoundNode> Nodes;
			std::vector<SoundParameter> Parameters;
			std::string Bus;
			float Volume = 1.0f, Pitch = 1.0f;
			SoundSpatialSettings Spatial;
			uint32_t InstanceLimit = 0;
			SoundStealPolicy Steal = SoundStealPolicy::Oldest;
			float Cooldown = 0.0f;
			bool Stream = false;
			SoundTempo Tempo;
			std::vector<SoundMarker> Markers;

			bool operator==(const SoundState&) const = default;

			static SoundState Capture(const Sound& sound);
			void Apply(Sound& sound) const;
		};

		struct Document
		{
			AssetHandle Handle = 0;
			SoundState Saved;     // on disk (captured at open and on save)
			SoundState Current;   // after the last completed edit
			bool EditInProgress = false;
			uint32_t SelectedNode = 0;
			std::map<std::string, float> AuditionValues;   // parameter values for the audition, by name
		};

		struct Waveform
		{
			std::vector<float> Peaks;   // empty when the clip cannot be read
			float Duration = 0.0f;      // seconds
		};

		Document* GetActiveDocument();
		Document* FindDocument(AssetHandle handle);
		void CloseDocument(size_t index);
		bool SaveDocument(Document& document, Sound& sound);
		void ApplyState(AssetHandle handle, const SoundState& state);

		bool HandleShortcuts(Document& document, Sound& sound);
		void RequestCloseDocument(size_t index);
		void UI_Tabs();
		void UI_CloseConfirm();
		void UI_Toolbar(Document& document, Sound& sound, bool& edited);
		void UI_Tree(Document& document, Sound& sound, bool& edited);
		void UI_TreeNode(Document& document, const Sound& sound, uint32_t index, uint32_t depth);
		void UI_SoundProperties(Sound& sound, bool& edited);
		// The Sound Banks that hold the Sound, and adding it to one (edits and saves the bank).
		void UI_Banks(AssetHandle sound);
		void UI_Parameters(Document& document, Sound& sound, bool& edited);
		void UI_Node(Document& document, Sound& sound, bool& edited);
		void UI_Clip(SoundNode& node, bool& edited);
		void UI_MusicTimeline(Sound& sound, bool& edited);
		void UI_Curves(Document& document, Sound& sound, SoundNode& node, bool& edited);
		bool UI_CurveEditor(const Document& document, const Sound& sound, SoundCurve& curve, uint32_t id);
		void TrackEdit(Document& document, Sound& sound, bool edited);

		void StartAudition(Document& document, const Sound& sound);
		void StopAudition();
		void UpdateAudition();
		void SetAuditionParameter(Document& document, const std::string& name, float value);

		const Waveform& GetWaveform(AssetHandle clip);

	private:
		std::vector<Document> m_Documents;
		int m_ActiveDocument = -1;
		bool m_SelectActiveTab = false;
		bool m_RequestFocus = false;
		bool m_KeyboardFocus = false;
		bool m_SelectNodePage = false;
		int m_PendingCloseIndex = -1;
		bool m_OpenCloseConfirm = false;

		Ref<AudioEventInstance> m_Audition;
		AssetHandle m_AuditionSound = 0;
		bool m_LoopAudition = false;

		// The curve point or clip handle being dragged, by id; cleared when the mouse is released.
		int m_DragPoint = -1;
		uint32_t m_DragCurve = 0;
		int m_DragClipHandle = -1;
		int m_DragMarker = -1;      // the music timeline's marker being dragged

		std::unordered_map<AssetHandle, Waveform> m_Waveforms;
		UndoPushFn m_PushUndo;
	};
}
