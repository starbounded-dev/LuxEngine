// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SoundEditorPanel.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Input.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Lux/Audio/SoundBank.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Project/Project.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <optional>

namespace Lux
{
	namespace
	{
		constexpr const char* k_Sections[] = {"Node", "Sound", "Parameters", "Banks"};
		constexpr float kWaveformHeight = 72.0f, kCurveHeight = 120.0f;
		constexpr float kHandleGrabPixels = 6.0f, kPointRadius = 4.0f;
		constexpr uint32_t kWaveformBuckets = 512;

		// Not constexpr: PropertyDropdown takes a mutable const char** list. Ordered as the enums.
		const char* s_NodeTypeNames[] = { "Clip", "Random", "Sequence", "Layer", "Switch" };
		const char* s_TargetNames[] = { "Volume", "Pitch", "Low-pass", "High-pass", "Reverb Send" };
		const char* s_AttenuationNames[] = { "None", "Inverse", "Linear" };
		const char* s_StealNames[] = { "Oldest", "Quietest", "None" };

		void BeginSoundPropertyGrid()
		{
			const float fontSize = ImGui::GetFontSize();
			ImGuiEx::BeginPropertyGrid(2, fontSize * 14.0f, fontSize * 24.0f);
		}

		// Peaks as bars around the middle of [min, max].
		void DrawPeaks(ImDrawList* drawList, ImVec2 min, ImVec2 max, const std::vector<float>& peaks)
		{
			const float width = max.x - min.x;
			const float middle = (min.y + max.y) * 0.5f, halfHeight = (max.y - min.y) * 0.5f - 2.0f;
			for (size_t bucket = 0; bucket < peaks.size(); ++bucket)
			{
				const float x0 = min.x + width * static_cast<float>(bucket) / static_cast<float>(peaks.size());
				const float x1 = min.x + width * static_cast<float>(bucket + 1) / static_cast<float>(peaks.size());
				const float extent = std::max(0.5f, peaks[bucket] * halfHeight);
				drawList->AddRectFilled(ImVec2(x0, middle - extent), ImVec2(std::max(x0 + 1.0f, x1), middle + extent), Colors::Theme::textDarker);
			}
		}

		const char* NodeIcon(SoundNodeType type)
		{
			switch (type)
			{
				case SoundNodeType::Clip:     return LUX_ICON_FILE_AUDIO_O;
				case SoundNodeType::Random:   return LUX_ICON_RANDOM;
				case SoundNodeType::Sequence: return LUX_ICON_LIST_OL;
				case SoundNodeType::Layer:    return LUX_ICON_CLONE;
				case SoundNodeType::Switch:   return LUX_ICON_CODE_FORK;
			}
			return LUX_ICON_FILE_AUDIO_O;
		}

		std::string AssetName(AssetHandle handle, const char* fallback)
		{
			if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			{
				const AssetMetadata metadata = editorAssetManager->GetMetadata(handle);
				if (metadata.IsValid())
					return Utils::PathToUTF8(metadata.FilePath.stem());
			}
			return fallback;
		}

		// What a curve target defaults to, and the range it may take (Sound::Validate's bounds).
		float TargetDefault(SoundCurveTarget target)
		{
			switch (target)
			{
				case SoundCurveTarget::Volume:
				case SoundCurveTarget::Pitch:    return 1.0f;
				case SoundCurveTarget::LowPass:  return 20000.0f;
				case SoundCurveTarget::HighPass: return 20.0f;
				case SoundCurveTarget::Send:     return 0.0f;
			}
			return 1.0f;
		}

		glm::vec2 TargetLimits(SoundCurveTarget target)
		{
			switch (target)
			{
				case SoundCurveTarget::Volume:   return { 0.0f, 4.0f };
				case SoundCurveTarget::Pitch:    return { 0.01f, 4.0f };
				case SoundCurveTarget::LowPass:
				case SoundCurveTarget::HighPass: return { 10.0f, 24000.0f };
				case SoundCurveTarget::Send:     return { 0.0f, 1.0f };
			}
			return { 0.0f, 1.0f };
		}

		bool IsFilter(SoundCurveTarget target)
		{
			return target == SoundCurveTarget::LowPass || target == SoundCurveTarget::HighPass;
		}

		// The range a parameter's curves are drawn over.
		glm::vec2 ParameterRange(const Sound& sound, const std::string& name)
		{
			const SoundParameter* parameter = sound.FindParameter(name);
			if (!parameter || parameter->Maximum <= parameter->Minimum)
				return { 0.0f, 1.0f };
			return { parameter->Minimum, parameter->Maximum };
		}

		// Keeps a labelled parameter's range on its label indices, as Validate requires.
		void FitLabels(SoundParameter& parameter)
		{
			parameter.Minimum = 0.0f;
			parameter.Maximum = static_cast<float>(parameter.Labels.size() - 1);
			parameter.Default = std::clamp(std::round(parameter.Default), 0.0f, parameter.Maximum);
		}

		std::string UniqueParameterName(const Sound& sound)
		{
			for (uint32_t number = 1;; ++number)
			{
				std::string name = std::format("Parameter {}", number);
				if (!sound.FindParameter(name))
					return name;
			}
		}

		const std::string& FirstLabelledParameter(const Sound& sound)
		{
			static const std::string s_None;
			for (const auto& parameter : sound.Parameters)
			{
				if (parameter.IsLabeled())
					return parameter.Name;
			}
			return s_None;
		}
	}

	SoundEditorPanel::SoundEditorPanel() = default;

	SoundEditorPanel::~SoundEditorPanel()
	{
		StopAudition();
	}

	// -- SoundState ------------------------------------------------------------------------------

	SoundEditorPanel::SoundState SoundEditorPanel::SoundState::Capture(const Sound& sound)
	{
		SoundState state;
		state.Nodes = sound.Nodes;
		state.Parameters = sound.Parameters;
		state.Bus = sound.Bus;
		state.Volume = sound.Volume;
		state.Pitch = sound.Pitch;
		state.Spatial = sound.Spatial;
		state.InstanceLimit = sound.InstanceLimit;
		state.Steal = sound.Steal;
		state.Cooldown = sound.Cooldown;
		state.Stream = sound.Stream;
		state.Tempo = sound.Tempo;
		state.Markers = sound.Markers;
		return state;
	}

	void SoundEditorPanel::SoundState::Apply(Sound& sound) const
	{
		sound.Nodes = Nodes;
		sound.Parameters = Parameters;
		sound.Bus = Bus;
		sound.Volume = Volume;
		sound.Pitch = Pitch;
		sound.Spatial = Spatial;
		sound.InstanceLimit = InstanceLimit;
		sound.Steal = Steal;
		sound.Cooldown = Cooldown;
		sound.Stream = Stream;
		sound.Tempo = Tempo;
		sound.Markers = Markers;
	}

	// -- Documents -------------------------------------------------------------------------------

	void SoundEditorPanel::OpenSound(AssetHandle handle)
	{
		if (!handle || AssetManager::GetAssetType(handle) != AssetType::Sound)
		{
			LUX_CORE_WARN_TAG("Editor", "Sound Editor: asset {} is not a Sound", static_cast<uint64_t>(handle));
			return;
		}
		if (auto* active = GetActiveDocument(); active && active->Handle != handle)
		{
			if (auto sound = AssetManager::GetAsset<Sound>(active->Handle))
				TrackEdit(*active, *sound, false, true);
			StopAudition();
		}
		m_RequestFocus = true;
		for (size_t i = 0; i < m_Documents.size(); ++i)
		{
			if (m_Documents[i].Handle == handle)
			{
				m_ActiveDocument = static_cast<int>(i);
				m_SelectActiveTab = true;
				return;
			}
		}
		const Ref<Sound> sound = AssetManager::GetAsset<Sound>(handle);
		if (!sound)
		{
			LUX_CORE_ERROR_TAG("Editor", "Sound Editor: failed to load Sound {}", static_cast<uint64_t>(handle));
			return;
		}
		Document document;
		document.Handle = handle;
		document.Saved = document.Current = SoundState::Capture(*sound);
		m_Documents.push_back(std::move(document));
		m_ActiveDocument = static_cast<int>(m_Documents.size() - 1);
		m_SelectActiveTab = true;
	}

	void SoundEditorPanel::OnClose()
	{
		for (auto& document : m_Documents)
			if (auto sound = AssetManager::GetAsset<Sound>(document.Handle))
				TrackEdit(document, *sound, false, true);
		m_KeyboardFocus = false;
		StopAudition();
		ImGuiEx::ClosePopupByID(m_ClosePopupID);
		m_OpenCloseConfirm = m_PendingCloseIndex >= 0;
		m_DragPoint = m_DragClipHandle = m_DragMarker = -1;
	}

	void SoundEditorPanel::OnProjectChanged(const Ref<Project>&)
	{
		// The new project is already active: do not resolve the previous project's handles here.
		StopAudition();
		ImGuiEx::ClosePopupByID(m_ClosePopupID);
		m_ClosePopupID = 0;
		m_Documents.clear();
		m_ActiveDocument = m_PendingCloseIndex = -1;
		m_RequestFocus = m_KeyboardFocus = m_SelectActiveTab = m_OpenCloseConfirm = false;
		m_SelectNodePage = false;
		m_DragPoint = m_DragClipHandle = m_DragMarker = -1;
		m_Waveforms.clear();
		m_HoldingBanks.clear();
		m_OtherBanks.clear();
		m_BanksDirty = true;
		m_BankCatalogSound = 0;
		m_BankError.clear();
	}

	void SoundEditorPanel::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "Section" << Yaml::Value << k_Sections[m_Section];
		out << Yaml::Key << "TreeFraction" << Yaml::Value << m_TreeFraction;
		out << Yaml::Key << "ShowTree" << Yaml::Value << m_ShowTree;
	}

	void SoundEditorPanel::LoadState(const Yaml::Node& state)
	{
		m_ResetSectionScroll = true;
		const auto section = state["Section"].as<std::string>("");
		for (uint32_t index = 0; index < std::size(k_Sections); ++index)
			if (section == k_Sections[index])
				m_Section = index;
		const float fraction = state["TreeFraction"].as<float>(m_TreeFraction);
		if (std::isfinite(fraction))
			m_TreeFraction = std::clamp(fraction, 0.15f, 0.65f);
		m_ShowTree = state["ShowTree"].as<bool>(m_ShowTree);
	}

	SoundEditorPanel::Document* SoundEditorPanel::GetActiveDocument()
	{
		if (m_ActiveDocument < 0 || m_ActiveDocument >= static_cast<int>(m_Documents.size()))
			return nullptr;
		return &m_Documents[static_cast<size_t>(m_ActiveDocument)];
	}

	SoundEditorPanel::Document* SoundEditorPanel::FindDocument(AssetHandle handle)
	{
		const auto it = std::find_if(m_Documents.begin(), m_Documents.end(), [&](const Document& document) { return document.Handle == handle; });
		return it != m_Documents.end() ? &*it : nullptr;
	}

	void SoundEditorPanel::CloseDocument(size_t index)
	{
		if (index >= m_Documents.size())
			return;
		if (auto sound = AssetManager::GetAsset<Sound>(m_Documents[index].Handle))
			TrackEdit(m_Documents[index], *sound, false, true);
		if (m_AuditionSound == m_Documents[index].Handle)
			StopAudition();
		m_Documents.erase(m_Documents.begin() + static_cast<ptrdiff_t>(index));
		if (m_ActiveDocument > static_cast<int>(index))
			--m_ActiveDocument;
		if (m_ActiveDocument >= static_cast<int>(m_Documents.size()))
			m_ActiveDocument = static_cast<int>(m_Documents.size()) - 1;
		m_SelectActiveTab = true;
	}

	bool SoundEditorPanel::SaveDocument(Document& document, Sound& sound)
	{
		TrackEdit(document, sound, false, true);
		std::string error;
		if (!sound.Validate(&error))
		{
			document.Error = error;
			LUX_CORE_ERROR_TAG("Editor", "Sound Editor: cannot save '{}': {}", AssetName(document.Handle, "Sound"), error);
			return false;
		}
		if (sound.ToYAML().size() > Sound::MaxFileBytes)
		{
			document.Error = "Sound exceeds its 64 KiB file limit. Edits are retained.";
			LUX_CORE_ERROR_TAG("Editor", "Sound Editor: cannot save '{}': it is larger than the 64 KiB a Sound file may be", AssetName(document.Handle, "Sound"));
			return false;
		}
		const Ref<Sound> asset = AssetManager::GetAsset<Sound>(document.Handle);
		if (!asset)
		{
			document.Error = "Sound is unavailable. Edits are retained; see Log.";
			LUX_CORE_ERROR_TAG("Editor", "Sound Editor: cannot save Sound {}: it is no longer loaded", static_cast<uint64_t>(document.Handle));
			return false;
		}
		if (!AssetImporter::TrySerialize(asset.As<Asset>()))
		{
			document.Error = "Save failed. Edits are retained; see Log, then retry Save.";
			return false;
		}
		document.Error.clear();
		AudioEngine::ApplySoundEdit(*asset); // playing instances (Play, previews) hear the saved Sound
		document.Saved = SoundState::Capture(sound);
		return true;
	}

	// Undo and redo land here: the asset takes the state, and the document follows it.
	void SoundEditorPanel::ApplyState(AssetHandle handle, const SoundState& state)
	{
		if (Ref<Sound> sound = AssetManager::GetAsset<Sound>(handle))
			state.Apply(*sound);
		if (Document* document = FindDocument(handle))
		{
			document->Current = state;
			document->EditInProgress = false;
			if (document->SelectedNode >= state.Nodes.size())
				document->SelectedNode = 0;
		}
	}

	// Widgets change the Sound directly. The state before the first change is the document's
	// Current; once no widget is active the whole edit becomes one undo step, so a drag is one step.
	void SoundEditorPanel::TrackEdit(Document& document, Sound& sound, bool edited, bool finish)
	{
		if (edited)
			document.EditInProgress = true;
		if ((!document.EditInProgress && !finish) || (!finish && ImGui::IsAnyItemActive()))
			return;
		document.EditInProgress = false;
		const SoundState before = document.Current;
		const SoundState after = SoundState::Capture(sound);
		document.Current = after;
		if (before == after || !m_PushUndo)
			return;
		const AssetHandle handle = document.Handle;
		m_PushUndo(std::format("Edit Sound '{}'", AssetName(handle, "Sound")),
			[this, handle, before]() { ApplyState(handle, before); },
			[this, handle, after]() { ApplyState(handle, after); });
	}

	// -- Audition --------------------------------------------------------------------------------

	void SoundEditorPanel::StartAudition(Document& document, const Sound& sound)
	{
		StopAudition();
		// A private copy, heard as authored but in 2D: there is no scene listener to place it near.
		// Handle 0 keeps it out of the asset's instance limit, cooldown and sequence step.
		auto copy = Ref<Sound>::Create(sound);
		copy->Handle = 0;
		copy->Spatial.Enabled = false;
		copy->InstanceLimit = 0;
		copy->Cooldown = 0.0f;
		m_Audition = AudioEventInstance::Create(copy);
		if (!m_Audition)
		{
			m_AuditionError = "Cannot create this audition. See Log, then retry Play.";
			return;
		}
		for (const auto& [name, value] : document.AuditionValues)
		{
			if (const SoundParameter* parameter = copy->FindParameter(name))
			{
				// A global parameter has one engine-wide value; the audition sets that.
				if (parameter->Global)
					AudioEngine::SetGlobalParameter(name, value);
				else
					m_Audition->SetParameter(name, value);
			}
		}
		m_AuditionSound = document.Handle;
		if (!m_Audition->Start())
		{
			StopAudition();
			m_AuditionError = "Cannot start this audition. Check its clips in Log, then retry Play.";
		}
	}

	void SoundEditorPanel::StopAudition()
	{
		if (m_Audition && m_Audition->IsValid())
			m_Audition->Stop(false);
		m_Audition = nullptr;
		m_AuditionSound = 0;
		m_AuditionError.clear();
	}

	void SoundEditorPanel::UpdateAudition()
	{
		if (!m_Audition)
			return;
		if (!m_Audition->IsValid())
		{
			// The audio engine restarted under it.
			m_Audition = nullptr;
			m_AuditionSound = 0;
			return;
		}
		// Each restart chooses through the tree again, so a looped audition walks the variations.
		if (m_LoopAudition && !m_Audition->IsPlaying() && !m_Audition->Start())
		{
			StopAudition();
			m_AuditionError = "Audition could not restart. See Log, then retry Play.";
		}
	}

	void SoundEditorPanel::SetAuditionParameter(Document& document, const std::string& name, float value)
	{
		document.AuditionValues[name] = value;
		const Ref<Sound> sound = AssetManager::GetAsset<Sound>(document.Handle);
		const SoundParameter* parameter = sound ? sound->FindParameter(name) : nullptr;
		if (parameter && parameter->Global)
			AudioEngine::SetGlobalParameter(name, value);
		else if (m_Audition && m_AuditionSound == document.Handle && m_Audition->IsValid())
			m_Audition->SetParameter(name, value);
	}

	const SoundEditorPanel::Waveform& SoundEditorPanel::GetWaveform(AssetHandle clip)
	{
		const auto generation = AssetManager::GetDataGeneration();
		if (m_WaveformGeneration != generation)
		{
			m_Waveforms.clear();
			m_WaveformGeneration = generation;
		}
		if (const auto it = m_Waveforms.find(clip); it != m_Waveforms.end())
			return it->second;
		Waveform waveform;
		Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
		if (editorAssetManager && AssetManager::GetAssetType(clip) == AssetType::Audio)
		{
			const auto path = editorAssetManager->GetFileSystemPath(clip);
			if (auto info = AudioFileUtils::GetFileInfo(path))
			{
				waveform.Duration = static_cast<float>(info->Duration);
				if (auto peaks = AudioFileUtils::GetWaveform(path, kWaveformBuckets))
					waveform.Peaks = std::move(*peaks);
			}
		}
		// Kept even when empty, so a file that cannot be read is not decoded again every frame.
		return m_Waveforms.emplace(clip, std::move(waveform)).first->second;
	}

	// -- UI ----------------------------------------------------------------------------------------

	void SoundEditorPanel::OnImGuiRender(bool& isOpen)
	{
		m_KeyboardFocus = false;
		if (!isOpen)
			return;
		UpdateAudition();
		ImGui::SetNextWindowSize(ImVec2(960.0f, 620.0f), ImGuiCond_FirstUseEver);
		if (m_RequestFocus)
		{
			ImGui::SetNextWindowFocus();
			m_RequestFocus = false;
		}
		if (!ImGui::Begin("Sound Editor", &isOpen))
		{
			ImGui::End();
			return;
		}
		m_KeyboardFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!Project::GetActive() || m_Documents.empty())
			ImGuiEx::PanelEmptyState("##sound_empty", "No Sound open",
									 "Open a project, then double-click a Sound in Content Browser or use "
									 "Edit Sound in the Inspector.");
		else
		{
			const AssetHandle previous = GetActiveDocument() ? GetActiveDocument()->Handle : AssetHandle(0);
			UI_Tabs();
			if (GetActiveDocument() && GetActiveDocument()->Handle != previous)
			{
				if (auto* old = FindDocument(previous))
					if (auto asset = AssetManager::GetAsset<Sound>(previous))
						TrackEdit(*old, *asset, false, true);
				StopAudition();
			}
			UI_CloseConfirm();
			Document* document = GetActiveDocument();
			Ref<Sound> sound = document ? AssetManager::GetAsset<Sound>(document->Handle) : nullptr;
			if (!document || !sound)
				ImGuiEx::PanelMessage("Sound unavailable", "This Sound could not be loaded. See Log.", true);
			else if (!HandleShortcuts(*document, *sound))
			{
				if (document->SelectedNode >= sound->Nodes.size())
					document->SelectedNode = 0;
				bool edited = false;
				const float font = ImGui::GetFontSize();
				const auto split = ImGuiEx::PanelSplitWidths(ImGui::GetContentRegionAvail().x, m_TreeFraction,
															 font * 14.0f, font * 25.0f);
				m_Compact = !split.SideBySide;
				UI_Toolbar(*document, *sound, edited);
				if (m_SelectNodePage)
				{
					m_Section = 0;
					m_ResetSectionScroll = true;
					m_NarrowTree = false;
					m_SelectNodePage = false;
				}
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::BeginCombo("##sound_section", k_Sections[m_Section]))
				{
					for (uint32_t index = 0; index < std::size(k_Sections); ++index)
					{
						if (ImGui::Selectable(k_Sections[index], index == m_Section))
						{
							m_Section = index;
							m_ResetSectionScroll = true;
							m_NarrowTree = false;
						}
						if (index == m_Section)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				if (sound->Nodes.empty())
					ImGuiEx::PanelMessage("Sound has no root", "Revert to restore the saved tree.", true);
				else if (split.SideBySide && m_ShowTree)
				{
					if (ImGui::BeginChild("##sound_tree_column", {split.First, 0.0f},
										  ImGuiChildFlags_Borders))
						UI_Tree(*document, *sound, edited);
					ImGui::EndChild();
					ImGui::SameLine();
					ImGuiEx::PanelSplitHandle("##sound_split", split, m_TreeFraction);
					ImGui::SameLine();
					if (ImGui::BeginChild("##sound_properties_column"))
						UI_Section(*document, *sound, edited);
					ImGui::EndChild();
				}
				else
				{
					if (ImGui::BeginChild("##sound_properties_column"))
					{
						if (m_Compact && m_NarrowTree)
							UI_Tree(*document, *sound, edited);
						else
							UI_Section(*document, *sound, edited);
					}
					ImGui::EndChild();
				}
				TrackEdit(*document, *sound, edited);
			}
		}
		ImGui::End();
		if (!isOpen)
			OnClose();
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			m_DragPoint = m_DragClipHandle = m_DragMarker = -1;
	}

	void SoundEditorPanel::UI_Section(Document& document, Sound& sound, bool& edited)
	{
		if (m_ResetSectionScroll)
		{
			ImGui::SetScrollY(0.0f);
			m_ResetSectionScroll = false;
		}
		switch (m_Section)
		{
		case 0:
			UI_Node(document, sound, edited);
			break;
		case 1:
			UI_SoundProperties(sound, edited);
			break;
		case 2:
			UI_Parameters(document, sound, edited);
			break;
		case 3:
			UI_Banks(document.Handle);
			break;
		}
	}

	void SoundEditorPanel::OnEvent(Event& event)
	{
		if (!m_KeyboardFocus || !event.IsInCategory(EventCategoryKeyboard))
			return;
		if (event.GetEventType() == EventType::KeyPressed)
		{
			const auto& key = static_cast<const KeyPressedEvent&>(event);
			const bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
			if (control && !ImGui::GetIO().WantTextInput && GImGui->OpenPopupStack.empty() &&
				(key.GetKeyCode() == Key::Z || key.GetKeyCode() == Key::Y))
				return;
		}
		event.Handled = true;
	}

	bool SoundEditorPanel::HandleShortcuts(Document& document, Sound& sound)
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (!m_KeyboardFocus || io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
			return false;
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
			SaveDocument(document, sound);
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false))
		{
			RequestCloseDocument(static_cast<size_t>(m_ActiveDocument));
			return true;
		}
		if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		{
			if (m_Audition && m_AuditionSound == document.Handle && m_Audition->IsPlaying())
				StopAudition();
			else if (AudioEngine::HasInitializedEngine() && sound.Validate())
				StartAudition(document, sound);
		}
		return false;
	}

	void SoundEditorPanel::RequestCloseDocument(size_t index)
	{
		if (index >= m_Documents.size())
			return;
		Document& closing = m_Documents[index];
		if (auto sound = AssetManager::GetAsset<Sound>(closing.Handle))
			TrackEdit(closing, *sound, false, true);
		if (closing.Current == closing.Saved && !closing.EditInProgress)
			CloseDocument(index);
		else
		{
			m_PendingCloseIndex = static_cast<int>(index);
			m_OpenCloseConfirm = true;
		}
	}

	void SoundEditorPanel::UI_Tabs()
	{
		ImGuiEx::ScopedColourStack tabColours(
			ImGuiCol_Tab, Colors::Theme::backgroundDark,
			ImGuiCol_TabHovered, Colors::Theme::backgroundPopup,
			ImGuiCol_TabSelected, Colors::Theme::groupHeader,
			ImGuiCol_TabDimmed, Colors::Theme::backgroundDark,
			ImGuiCol_TabDimmedSelected, Colors::Theme::groupHeader);
		int closeRequest = -1;
		if (ImGui::BeginTabBar("##sound_editor_tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
		{
			for (size_t i = 0; i < m_Documents.size(); ++i)
			{
				const Document& document = m_Documents[i];
				bool open = true;
				ImGuiTabItemFlags flags = document.Current == document.Saved && !document.EditInProgress ? ImGuiTabItemFlags_None : ImGuiTabItemFlags_UnsavedDocument;
				if (m_SelectActiveTab && static_cast<int>(i) == m_ActiveDocument)
					flags |= ImGuiTabItemFlags_SetSelected;
				// ### keeps the tab ID stable while the displayed name changes.
				const std::string label = std::format("{}###sound_tab_{}", AssetName(document.Handle, "Sound"), static_cast<uint64_t>(document.Handle));
				ImGui::SetNextItemWidth(
					std::min(ImGui::GetFontSize() * 16.0f,
							 std::max(ImGui::GetFontSize() * 4.0f, ImGui::GetContentRegionAvail().x * 0.5f)));
				if (ImGui::BeginTabItem(label.c_str(), &open, flags))
				{
					if (!m_SelectActiveTab)
						m_ActiveDocument = static_cast<int>(i);
					ImGui::EndTabItem();
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(
						"%s", Utils::PathToUTF8(
								  Project::GetEditorAssetManager()->GetMetadata(document.Handle).FilePath)
								  .c_str());
				ImGuiEx::ScopedID tabID(document.Handle);
				if (ImGui::BeginPopupContextItem("##sound_tab_actions"))
				{
					if (ImGui::MenuItem("Copy asset path"))
						ImGui::SetClipboardText(
							Utils::PathToUTF8(
								Project::GetEditorAssetManager()->GetMetadata(document.Handle).FilePath)
								.c_str());
					if (ImGui::MenuItem("Close", "Ctrl+W"))
						open = false;
					ImGui::EndPopup();
				}
				if (!open)
					closeRequest = static_cast<int>(i);
			}
			ImGui::EndTabBar();
		}
		m_SelectActiveTab = false;

		if (closeRequest < 0)
			return;
		RequestCloseDocument(static_cast<size_t>(closeRequest));
	}

	void SoundEditorPanel::UI_CloseConfirm()
	{
		m_ClosePopupID = ImGui::GetID("Unsaved Sound##sound_close");
		if (m_OpenCloseConfirm)
		{
			ImGui::OpenPopup("Unsaved Sound##sound_close");
			m_OpenCloseConfirm = false;
		}
		ImGui::SetNextWindowSize(
			{std::min(ImGui::GetFontSize() * 32.0f, ImGui::GetMainViewport()->WorkSize.x * 0.85f), 0.0f});
		if (!ImGui::BeginPopupModal("Unsaved Sound##sound_close", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		const bool valid = m_PendingCloseIndex >= 0 && m_PendingCloseIndex < static_cast<int>(m_Documents.size());
		Ref<Sound> sound =
			valid ? AssetManager::GetAsset<Sound>(m_Documents[m_PendingCloseIndex].Handle) : nullptr;
		if (valid)
		{
			const auto& document = m_Documents[m_PendingCloseIndex];
			ImGui::TextWrapped("'%s' has unsaved changes.", AssetName(document.Handle, "Sound").c_str());
			if (!document.Error.empty())
				ImGuiEx::PanelMessage("Sound was not saved", document.Error.c_str(), true);
		}
		const ImGuiEx::PanelAction actions[] = {
			{0, "Save", LUX_ICON_FLOPPY_O, nullptr, valid && sound && sound->Validate(), false, true},
			{1, "Discard", nullptr, nullptr, valid},
			{2, "Cancel"}};
		const int action = ImGuiEx::PanelActionBar("##close_sound_actions", actions);
		if (action == 0 && SaveDocument(m_Documents[m_PendingCloseIndex], *sound))
		{
			CloseDocument(m_PendingCloseIndex);
			m_PendingCloseIndex = -1;
			ImGui::CloseCurrentPopup();
		}
		else if (action == 1)
		{
			const auto saved = m_Documents[m_PendingCloseIndex].Saved;
			if (sound)
			{
				saved.Apply(*sound);
				TrackEdit(m_Documents[m_PendingCloseIndex], *sound, true, true);
			}
			CloseDocument(m_PendingCloseIndex);
			m_PendingCloseIndex = -1;
			ImGui::CloseCurrentPopup();
		}
		else if (action == 2 || !valid || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_PendingCloseIndex = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void SoundEditorPanel::UI_Toolbar(Document& document, Sound& sound, bool& edited)
	{
		std::string error;
		const bool valid = sound.Validate(&error);
		const bool dirty = document.Current != document.Saved || document.EditInProgress;
		const bool native = AudioEngine::HasInitializedEngine();
		const bool playing = m_Audition && m_AuditionSound == document.Handle && m_Audition->IsValid() &&
							 m_Audition->IsPlaying();
		const ImGuiEx::PanelAction actions[] = {
			{0, "Save", LUX_ICON_FLOPPY_O, "Ctrl+S", dirty && valid, false, true},
			{1, "Revert", LUX_ICON_UNDO, nullptr, dirty},
			{2, playing ? "Stop" : "Play", playing ? LUX_ICON_STOP : LUX_ICON_PLAY, "Space",
			 native && (valid || playing)},
			{3, "Loop", LUX_ICON_REPEAT, nullptr, native, m_LoopAudition},
			{4, "Tree", LUX_ICON_SITEMAP, nullptr, true, m_Compact ? m_NarrowTree : m_ShowTree}};
		switch (ImGuiEx::PanelActionBar("##sound_actions", actions))
		{
		case 0:
			SaveDocument(document, sound);
			break;
		case 1:
			document.Saved.Apply(sound);
			document.Error.clear();
			edited = true;
			break;
		case 2:
			if (playing)
				StopAudition();
			else
				StartAudition(document, sound);
			break;
		case 3:
			m_LoopAudition = !m_LoopAudition;
			break;
		case 4:
			m_ResetSectionScroll = true;
			if (m_Compact)
				m_NarrowTree = !m_NarrowTree;
			else
				m_ShowTree = !m_ShowTree;
			break;
		}
		if (!document.Error.empty())
			ImGuiEx::PanelMessage("Sound was not saved", document.Error.c_str(), true);
		else if (!valid)
			ImGuiEx::PanelMessage("Invalid Sound", error.c_str(), true);
		else
			ImGui::TextWrapped("%s", dirty ? "Unsaved changes" : "Saved");
		if (!native)
			ImGui::TextWrapped("Audition needs the audio engine running.");
		if (!m_AuditionError.empty())
			ImGuiEx::PanelMessage("Audition unavailable", m_AuditionError.c_str(), true);
	}

	void SoundEditorPanel::UI_Tree(Document& document, Sound& sound, bool& edited)
	{
		const std::vector<int> parents = sound.GetParents();
		const uint32_t selected = document.SelectedNode;
		const SoundNodeType selectedType = sound.Nodes[selected].Type;
		// New nodes go under the selection, or beside it when it is a clip.
		const int addParent = selectedType != SoundNodeType::Clip ? static_cast<int>(selected) : parents[selected];

		const auto* initialSiblings =
			parents[selected] >= 0 ? &sound.Nodes[parents[selected]].Children : nullptr;
		const auto initialPosition =
			initialSiblings
				? static_cast<size_t>(std::find(initialSiblings->begin(), initialSiblings->end(), selected) -
									  initialSiblings->begin())
				: 0;
		const ImGuiEx::PanelAction actions[] = {
			{0, "Add", LUX_ICON_PLUS},
			{1, "Up", LUX_ICON_ARROW_UP, nullptr, parents[selected] >= 0 && initialPosition > 0},
			{2, "Down", LUX_ICON_ARROW_DOWN, nullptr,
			 parents[selected] >= 0 && initialSiblings && initialPosition + 1 < initialSiblings->size()},
			{3, "Remove node", LUX_ICON_TRASH, nullptr, selected != 0}};
		const int action = ImGuiEx::PanelActionBar("##tree_actions", actions);
		if (action == 0)
			ImGui::OpenPopup("##sound_tree_add");
		if (ImGui::BeginPopup("##sound_tree_add"))
		{
			for (int type = 0; type < static_cast<int>(std::size(s_NodeTypeNames)); ++type)
			{
				ImGui::PushID(type);
				if (ImGui::MenuItem(std::format("{}  {}", NodeIcon(static_cast<SoundNodeType>(type)), s_NodeTypeNames[type]).c_str(), nullptr, false, addParent >= 0))
				{
					document.SelectedNode = sound.AddNode(static_cast<uint32_t>(addParent), static_cast<SoundNodeType>(type));
					if (static_cast<SoundNodeType>(type) == SoundNodeType::Switch)
						sound.Nodes[document.SelectedNode].SwitchParameter = FirstLabelledParameter(sound);
					edited = true;
				}
				ImGui::PopID();
			}
			if (addParent < 0)
				ImGui::TextDisabled("Wrap the clip in a container first.");
			ImGui::Separator();
			if (ImGui::BeginMenu("Wrap in"))
			{
				for (int type = 1; type < static_cast<int>(std::size(s_NodeTypeNames)); ++type)
				{
					ImGui::PushID(type);
					if (ImGui::MenuItem(std::format("{}  {}", NodeIcon(static_cast<SoundNodeType>(type)), s_NodeTypeNames[type]).c_str()))
					{
						document.SelectedNode = sound.WrapNode(selected, static_cast<SoundNodeType>(type));
						if (static_cast<SoundNodeType>(type) == SoundNodeType::Switch)
							sound.Nodes[document.SelectedNode].SwitchParameter = FirstLabelledParameter(sound);
						edited = true;
					}
					ImGui::PopID();
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		// Reorder the selection among its siblings, or remove it (the root stays). The menu above may
		// have changed the tree and the selection.
		const uint32_t current = document.SelectedNode;
		const int parent = sound.GetParents()[current];
		std::vector<uint32_t>* siblings = parent >= 0 ? &sound.Nodes[static_cast<size_t>(parent)].Children : nullptr;
		const auto position = siblings ? static_cast<size_t>(std::find(siblings->begin(), siblings->end(), current) - siblings->begin()) : 0;
		if (action == 1 && siblings && position > 0)
		{
			std::swap((*siblings)[position], (*siblings)[position - 1]);
			edited = true;
		}
		if (action == 2 && siblings && position + 1 < siblings->size())
		{
			std::swap((*siblings)[position], (*siblings)[position + 1]);
			edited = true;
		}
		if (action == 3 && current != 0)
		{
			sound.RemoveNode(current);
			document.SelectedNode = 0;
			edited = true;
		}

		ImGui::Separator();
		UI_TreeNode(document, sound, 0, 1);
	}

	void SoundEditorPanel::UI_TreeNode(Document& document, const Sound& sound, uint32_t index, uint32_t depth)
	{
		// Validate keeps the tree acyclic and at most MaxDepth deep; this guards an edit in progress.
		if (depth > Sound::MaxDepth || index >= sound.Nodes.size())
			return;
		ImGuiEx::ScopedStyle indent(
			ImGuiStyleVar_IndentSpacing,
			std::min(ImGui::GetStyle().IndentSpacing,
					 std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 6.0f)));
		const SoundNode& node = sound.Nodes[index];
		std::string label = node.Name.empty()
			? (node.Type == SoundNodeType::Clip ? (node.Clip ? AssetName(node.Clip, "Clip") : std::string("(no clip)")) : s_NodeTypeNames[static_cast<int>(node.Type)])
			: node.Name;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
		if (node.Children.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;
		if (document.SelectedNode == index)
			flags |= ImGuiTreeNodeFlags_Selected;

		ImGui::PushID(static_cast<int>(index));
		const bool looping = node.Loop && node.Type == SoundNodeType::Clip;
		const bool open = ImGui::TreeNodeEx("##node", flags);
		const auto row = ImGui::GetItemRectMin();
		const std::string title = std::format("{}  {}{}", NodeIcon(node.Type), label,
											  looping ? std::string("  ") + LUX_ICON_REPEAT : "");
		const float start = row.x + ImGui::GetTreeNodeToLabelSpacing();
		const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), {start, row.y},
								  {std::max(start, right), row.y + ImGui::GetTextLineHeight()},
								  std::max(start, right), title.data(), title.data() + title.size(), nullptr);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			if (ImGui::BeginTooltip())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
				ImGui::TextWrapped("%s", title.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			document.SelectedNode = index;
			m_SelectNodePage = true;
		}
		if (open)
		{
			// A Switch's children are its labels, in order.
			const SoundParameter* switchParameter = node.Type == SoundNodeType::Switch ? sound.FindParameter(node.SwitchParameter) : nullptr;
			for (size_t child = 0; child < node.Children.size(); ++child)
			{
				if (switchParameter && child < switchParameter->Labels.size())
					ImGui::TextWrapped("%s", switchParameter->Labels[child].c_str());
				UI_TreeNode(document, sound, node.Children[child], depth + 1);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SoundEditorPanel::UI_SoundProperties(Sound& sound, bool& edited)
	{
		// doPushUndo = false on every widget: these edit an asset, not the scene. TrackEdit records
		// them on the editor undo stack instead.
		if (ImGuiEx::BoundedCollapsingHeader("Sound", ImGuiTreeNodeFlags_DefaultOpen))
		{
			BeginSoundPropertyGrid();
			edited |= ImGuiEx::Property("Volume", sound.Volume, 0.01f, 0.0f, 4.0f, "Linear gain; 1 plays the clips as recorded.", false, SoundNode{}.Volume);
			edited |= ImGuiEx::Property("Pitch", sound.Pitch, 0.01f, 0.01f, 4.0f, "Playback rate; 2 is an octave up and twice as fast.", false, SoundNode{}.Pitch);
			edited |= ImGuiEx::Property("Bus", sound.Bus, "The mixer bus it plays into (bus:/..., View > Audio Mixer). A bus the mixer lacks plays through the master.", false);
			edited |= ImGuiEx::Property("Stream", sound.Stream, "Read from disk while playing instead of decoding into memory first. For music and long ambience.", false);
			edited |= ImGuiEx::Property("Instance Limit", sound.InstanceLimit, 0u, Sound::MaxInstanceLimit, "Most instances playing at once; 0 is unlimited.", false, 0u);
			if (sound.InstanceLimit > 0)
				edited |= ImGuiEx::PropertyDropdown("Steal", s_StealNames, static_cast<int32_t>(std::size(s_StealNames)), sound.Steal,
					"At the limit, stop the oldest or quietest instance for the new one, or refuse the new one.", false);
			edited |= ImGuiEx::Property("Cooldown", sound.Cooldown, 0.01f, 0.0f, 60.0f, "Seconds after a start before the Sound may start again.", false, 0.0f);
			ImGuiEx::EndPropertyGrid();
		}

		if (ImGuiEx::BoundedCollapsingHeader("Spatial", ImGuiTreeNodeFlags_DefaultOpen))
		{
			BeginSoundPropertyGrid();
			edited |= ImGuiEx::Property("3D", sound.Spatial.Enabled, "Panned and attenuated from its position. Off plays it in 2D.", false);
			if (sound.Spatial.Enabled)
			{
				edited |= ImGuiEx::PropertyDropdown("Attenuation", s_AttenuationNames, static_cast<int32_t>(std::size(s_AttenuationNames)), sound.Spatial.Attenuation,
					"Inverse falls off like a real source; Linear reaches silence at the maximum distance.", false);
				edited |= ImGuiEx::Property("Min Distance", sound.Spatial.MinDistance, 0.1f, 0.01f, std::max(0.01f, sound.Spatial.MaxDistance), "Metres; full volume up to here.", false, SoundSpatialSettings{}.MinDistance);
				edited |= ImGuiEx::Property("Max Distance", sound.Spatial.MaxDistance, 0.1f, std::max(0.01f, sound.Spatial.MinDistance), 10000.0f, "Metres; attenuation stops here, and the source is culled beyond it.", false, SoundSpatialSettings{}.MaxDistance);
				edited |= ImGuiEx::Property("Rolloff", sound.Spatial.Rolloff, 0.01f, 0.0f, 10.0f, "How quickly it falls off; 1 is natural.", false, SoundSpatialSettings{}.Rolloff);
			}
			ImGuiEx::EndPropertyGrid();
		}

		// The beat grid and markers the music director syncs transitions to (MusicSync).
		if (ImGuiEx::BoundedCollapsingHeader("Music"))
		{
			BeginSoundPropertyGrid();
			edited |= ImGuiEx::Property("Tempo (BPM)", sound.Tempo.BPM, 0.1f, 0.0f, 400.0f, "Beats per minute of the first clip; 0 reports no beats. Music beds use 20-400.", false, SoundTempo{}.BPM);
			if (sound.Tempo.BPM > 0.0f)
			{
				edited |= ImGuiEx::Property("Beats per Bar", sound.Tempo.BeatsPerBar, 1u, 32u, "The time signature's upper number.", false, SoundTempo{}.BeatsPerBar);
				edited |= ImGuiEx::Property("Beat Unit", sound.Tempo.BeatUnit, 1u, 16u, "The time signature's lower number: 1, 2, 4, 8 or 16.", false, SoundTempo{}.BeatUnit);
				edited |= ImGuiEx::Property("First Beat (s)", sound.Tempo.Offset, 0.001f, 0.0f, 60.0f, "Seconds into the clip where beat 1 of bar 1 falls.", false, SoundTempo{}.Offset);
			}
			ImGuiEx::EndPropertyGrid();
			UI_MusicTimeline(sound, edited);
			std::optional<size_t> removed;
			for (size_t index = 0; index < sound.Markers.size(); ++index)
			{
				SoundMarker& marker = sound.Markers[index];
				ImGui::PushID(static_cast<int>(index));
				BeginSoundPropertyGrid();
				edited |= ImGuiEx::Property("Marker", marker.Name,
											"Marker name; Section: marks a musical section.", false);
				edited |= ImGuiEx::Property("Time (s)", marker.Seconds, 0.01f, 0.0f, 3600.0f,
											"Seconds into the clip.", false, 0.0f);
				ImGuiEx::EndPropertyGrid();
				const ImGuiEx::PanelAction actions[] = {{0, "Remove marker", LUX_ICON_TIMES}};
				if (ImGuiEx::PanelActionBar("##marker_actions", actions) == 0)
					removed = index;
				ImGui::PopID();
			}
			if (removed)
			{
				sound.Markers.erase(sound.Markers.begin() + static_cast<std::ptrdiff_t>(*removed));
				edited = true;
			}
			const ImGuiEx::PanelAction addMarker[] = {
				{0, "Add marker", LUX_ICON_PLUS, nullptr, sound.Markers.size() < 256}};
			if (ImGuiEx::PanelActionBar("##add_marker", addMarker) == 0)
			{
				sound.Markers.push_back({ "Section:New", 0.0f });
				edited = true;
			}
			ImGui::SetItemTooltip("NextMarker transitions sync to any marker, NextSection to names starting \"Section:\".");
		}
	}

	void SoundEditorPanel::UI_Banks(AssetHandle soundHandle)
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Sound Banks", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		const ImGuiEx::PanelAction refresh[] = {{0, "Refresh banks", LUX_ICON_REFRESH}};
		if (ImGuiEx::PanelActionBar("##sound_bank_refresh", refresh) == 0)
			m_BanksDirty = true;
		const auto manager = Project::GetEditorAssetManager();
		if (!manager)
			return;
		const auto revision = manager->GetAssetRegistry().GetRevision();
		const auto generation = AssetManager::GetDataGeneration();
		if (m_BanksDirty || m_BankCatalogSound != soundHandle || m_BankRegistryRevision != revision ||
			m_BankDataGeneration != generation)
		{
			if (m_BankCatalogSound != soundHandle)
				m_BankError.clear();
			m_HoldingBanks.clear();
			m_OtherBanks.clear();
			for (const auto handle : AssetManager::GetAllAssetsWithType<SoundBank>())
			{
				const Ref<SoundBank> bank = AssetManager::GetAsset<SoundBank>(handle);
				if (!bank)
					continue;
				const auto path = Utils::PathToUTF8(manager->GetMetadata(handle).FilePath);
				(bank->Contains(soundHandle) ? m_HoldingBanks : m_OtherBanks).emplace_back(path, handle);
			}
			std::ranges::sort(m_HoldingBanks);
			std::ranges::sort(m_OtherBanks);
			m_BankCatalogSound = soundHandle;
			m_BankRegistryRevision = revision;
			m_BankDataGeneration = generation;
			m_BanksDirty = false;
		}
		if (!m_BankError.empty())
			ImGuiEx::PanelMessage("Sound Bank was not saved", m_BankError.c_str(), true);
		if (m_HoldingBanks.empty())
			ImGui::TextWrapped(
				"In no readable Sound Bank: it may load when it first plays, which can stall that frame.");
		for (const auto& [path, handle] : m_HoldingBanks)
			ImGui::TextWrapped("%s", path.c_str());
		ImGuiEx::ScopedDisable disabled(m_OtherBanks.empty());
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##add_to_bank", "Add to bank..."))
		{
			for (const auto& [path, handle] : m_OtherBanks)
			{
				ImGuiEx::ScopedID id(handle);
				if (!ImGuiEx::PanelListItem("##bank", path))
					continue;
				AddToBank(handle, soundHandle);
			}
			ImGui::EndCombo();
		}
		if (m_OtherBanks.empty() && m_HoldingBanks.empty())
			ImGui::TextWrapped("Make one in Content Browser: New > Sound Bank.");
		ImGui::TextWrapped("Refresh banks after changing membership elsewhere.");
	}

	bool SoundEditorPanel::AddToBank(AssetHandle handle, AssetHandle soundHandle)
	{
		// Commit candidate membership before publishing it, including undo/redo.
		const auto change = [handle, soundHandle](bool add)
		{
			if (AssetManager::GetAssetType(handle) != AssetType::SoundBank)
			{
				LUX_CORE_ERROR_TAG("Editor", "Sound: bank {} is no longer available",
								   static_cast<uint64_t>(handle));
				return false;
			}
			Ref<SoundBank> bank = AssetManager::GetAsset<SoundBank>(handle);
			if (!bank)
			{
				LUX_CORE_ERROR_TAG("Editor", "Sound: cannot load bank {}", static_cast<uint64_t>(handle));
				return false;
			}
			auto candidate = Ref<SoundBank>::Create(*bank);
			if (!(add ? candidate->Add(soundHandle) : candidate->Remove(soundHandle)))
			{
				LUX_CORE_ERROR_TAG(
					"Editor", "Cannot change Sound {} membership in bank {}: already changed or bank is full",
					static_cast<uint64_t>(soundHandle), static_cast<uint64_t>(handle));
				return false;
			}
			if (!AssetImporter::TrySerialize(candidate.As<Asset>()))
				return false;
			bank->Sounds = candidate->Sounds;
			return true;
		};
		if (change(true))
		{
			m_BankError.clear();
			m_BanksDirty = true;
			if (m_PushUndo)
				m_PushUndo("Sound: add to bank", [change] { change(false); }, [change] { change(true); });
			return true;
		}
		else
			m_BankError = "Change not applied. See Log, then retry adding this Sound.";
		return false;
	}

	void SoundEditorPanel::UI_Parameters(Document& document, Sound& sound, bool& edited)
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		int removeParameter = -1;
		for (size_t index = 0; index < sound.Parameters.size(); ++index)
		{
			SoundParameter& parameter = sound.Parameters[index];
			ImGui::PushID(static_cast<int>(index));
			BeginSoundPropertyGrid();
			std::string name = parameter.Name;
			if (ImGuiEx::Property("Name", name, "Game code sets it by this name (any case).", false))
			{
				sound.RenameParameterUses(parameter.Name, name);
				if (const auto value = document.AuditionValues.find(parameter.Name); value != document.AuditionValues.end())
				{
					const float kept = value->second;
					document.AuditionValues.erase(value);
					document.AuditionValues[name] = kept;
				}
				parameter.Name = name;
				edited = true;
			}
			edited |= ImGuiEx::Property("Global", parameter.Global, "One value shared by every Sound with a global parameter of this name, set by game code with Audio.SetGlobalParameter (such as time of day). Instances cannot set it.", false);
			bool labelled = parameter.IsLabeled();
			if (ImGuiEx::Property("Labelled", labelled, "Labelled parameters take one of a list of names, such as surfaces; Switch nodes pick children by them.", false))
			{
				parameter.Labels = labelled ? std::vector<std::string>{ "First", "Second" } : std::vector<std::string>{};
				if (labelled)
					FitLabels(parameter);
				else
				{
					parameter.Minimum = 0.0f;
					parameter.Maximum = 1.0f;
					parameter.Default = 0.0f;
					for (auto& node : sound.Nodes)
					{
						if (Utils::String::EqualsIgnoreCase(node.SwitchParameter, parameter.Name))
							node.SwitchParameter.clear();
					}
				}
				edited = true;
			}
			if (parameter.IsLabeled())
			{
				for (size_t label = 0; label < parameter.Labels.size(); ++label)
				{
					ImGui::PushID(static_cast<int>(label));
					edited |= ImGuiEx::Property(std::format("Label {}", label).c_str(), parameter.Labels[label], "", false);
					ImGui::PopID();
				}
				int32_t defaultLabel = static_cast<int32_t>(parameter.Default);
				if (ImGuiEx::PropertyDropdown("Default", parameter.Labels, static_cast<int32_t>(parameter.Labels.size()), &defaultLabel, "", false))
				{
					parameter.Default = static_cast<float>(defaultLabel);
					edited = true;
				}
			}
			else
			{
				edited |= ImGuiEx::Property("Minimum", parameter.Minimum, 0.01f, 0.0f, 0.0f, "", false, SoundParameter{}.Minimum);
				edited |= ImGuiEx::Property("Maximum", parameter.Maximum, 0.01f, 0.0f, 0.0f, "", false, SoundParameter{}.Maximum);
				edited |= ImGuiEx::Property("Default", parameter.Default, 0.01f, parameter.Minimum, parameter.Maximum, "", false, SoundParameter{}.Default);
			}

			// Not an edit of the Sound: the value the audition plays with.
			const auto audition = document.AuditionValues.find(parameter.Name);
			float value = audition != document.AuditionValues.end() ? audition->second : parameter.Default;
			if (parameter.IsLabeled())
			{
				int32_t selected = std::clamp(static_cast<int32_t>(value), 0, static_cast<int32_t>(parameter.Labels.size()) - 1);
				if (ImGuiEx::PropertyDropdown("Audition", parameter.Labels, static_cast<int32_t>(parameter.Labels.size()), &selected, "The value the Play button uses; changes are heard live.", false))
					SetAuditionParameter(document, parameter.Name, static_cast<float>(selected));
			}
			else if (parameter.Minimum < parameter.Maximum
				&& ImGuiEx::PropertySlider("Audition", value, parameter.Minimum, parameter.Maximum, "The value the Play button uses; changes are heard live.", false, parameter.Default))
				SetAuditionParameter(document, parameter.Name, value);
			ImGuiEx::EndPropertyGrid();

			const ImGuiEx::PanelAction parameterActions[] = {
				{0, "Add label", LUX_ICON_PLUS, nullptr, parameter.IsLabeled()},
				{1, "Remove label", LUX_ICON_MINUS, nullptr, parameter.Labels.size() > 1},
				{2, "Remove parameter", LUX_ICON_TRASH}};
			switch (ImGuiEx::PanelActionBar("##parameter_actions", parameterActions))
			{
			case 0:
				parameter.Labels.push_back(std::format("Label {}", parameter.Labels.size()));
				FitLabels(parameter);
				edited = true;
				break;
			case 1:
				parameter.Labels.pop_back();
				FitLabels(parameter);
				edited = true;
				break;
			case 2:
				removeParameter = static_cast<int>(index);
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (removeParameter >= 0)
		{
			const std::string name = sound.Parameters[static_cast<size_t>(removeParameter)].Name;
			sound.RemoveParameterUses(name);
			document.AuditionValues.erase(name);
			sound.Parameters.erase(sound.Parameters.begin() + removeParameter);
			edited = true;
		}

		const ImGuiEx::PanelAction addParameter[] = {
			{0, "Add parameter", LUX_ICON_PLUS, nullptr, sound.Parameters.size() < Sound::MaxParameters}};
		if (ImGuiEx::PanelActionBar("##add_parameter", addParameter) == 0)
		{
			sound.Parameters.push_back({ UniqueParameterName(sound), 0.0f, 1.0f, 0.0f, {} });
			edited = true;
		}
	}

	void SoundEditorPanel::UI_Node(Document& document, Sound& sound, bool& edited)
	{
		const uint32_t index = document.SelectedNode;
		SoundNode& node = sound.Nodes[index];
		const std::vector<int> parents = sound.GetParents();
		const int parent = parents[index];
		// One header name whatever the node, so it stays open across selections.
		if (!ImGuiEx::BoundedCollapsingHeader("Selected Node", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGui::PushID(static_cast<int>(index));
		BeginSoundPropertyGrid();
		edited |= ImGuiEx::Property("Name", node.Name, "Optional; shown in the tree.", false);

		SoundNodeType type = node.Type;
		if (ImGuiEx::PropertyDropdown("Type", s_NodeTypeNames, static_cast<int32_t>(std::size(s_NodeTypeNames)), type,
			"Clip plays a file. Random plays one child by weight, Sequence the next child each start, Layer every child, Switch the child for a labelled parameter's value.", false))
		{
			// A clip has no children, so a container with children cannot become one.
			if (type != SoundNodeType::Clip || node.Children.empty())
			{
				node.Type = type;
				if (type == SoundNodeType::Switch && node.SwitchParameter.empty())
					node.SwitchParameter = FirstLabelledParameter(sound);
				edited = true;
			}
		}
		if (parent >= 0 && sound.Nodes[static_cast<size_t>(parent)].Type == SoundNodeType::Random)
			edited |= ImGuiEx::Property("Weight", node.Weight, 0.01f, 0.0f, 1000.0f, "Chance of being chosen, relative to its siblings. 0 is never.", false, SoundNode{}.Weight);
		if (node.Type == SoundNodeType::Random)
			edited |= ImGuiEx::Property("Avoid Repeat", node.AvoidRepeat, "Never choose the same child twice in a row.", false);
		if (node.Type == SoundNodeType::Switch)
		{
			std::vector<std::string> names{ "(none)" };
			int32_t selected = 0;
			for (const auto& parameter : sound.Parameters)
			{
				if (!parameter.IsLabeled())
					continue;
				if (Utils::String::EqualsIgnoreCase(parameter.Name, node.SwitchParameter))
					selected = static_cast<int32_t>(names.size());
				names.push_back(parameter.Name);
			}
			if (ImGuiEx::PropertyDropdown("Switch On", names, static_cast<int32_t>(names.size()), &selected,
				"The labelled parameter whose value picks the child: the first child for the first label, and so on.", false))
			{
				node.SwitchParameter = selected > 0 ? names[static_cast<size_t>(selected)] : std::string{};
				edited = true;
			}
		}
		edited |= ImGuiEx::Property("Volume", node.Volume, 0.01f, 0.0f, 4.0f, "Multiplies everything below this node.", false, SoundNode{}.Volume);
		edited |= ImGuiEx::Property("Pitch", node.Pitch, 0.01f, 0.01f, 4.0f, "Multiplies everything below this node.", false, SoundNode{}.Pitch);
		edited |= ImGuiEx::Property("Volume Variation", node.VolumeVariation, 0.1f, 0.0f, 24.0f, "dB; each start lowers the volume by a random amount up to this.", false, SoundNode{}.VolumeVariation);
		edited |= ImGuiEx::Property("Pitch Variation", node.PitchVariation, 0.05f, 0.0f, 12.0f, "Semitones; each start shifts the pitch by a random amount up to this, either way.", false, SoundNode{}.PitchVariation);
		ImGuiEx::EndPropertyGrid();

		if (node.Type == SoundNodeType::Clip)
			UI_Clip(node, edited);
		UI_Curves(document, sound, node, edited);
		ImGui::PopID();
	}

	// The first clip (the one the engine counts beats on) under its bar and beat lines and markers.
	// Dragging a marker moves it; TrackEdit makes the drag one undo step.
	void SoundEditorPanel::UI_MusicTimeline(Sound& sound, bool& edited)
	{
		const auto clipNode = std::find_if(sound.Nodes.begin(), sound.Nodes.end(), [](const SoundNode& node) { return node.Type == SoundNodeType::Clip && node.Clip; });
		if (clipNode == sound.Nodes.end())
		{
			ImGui::TextWrapped("Assign a clip to see the music timeline.");
			return;
		}
		const Waveform& waveform = GetWaveform(clipNode->Clip);
		const float duration = waveform.Duration;
		if (waveform.Peaks.empty() || duration <= 0.0f)
		{
			ImGui::TextWrapped("The first clip's waveform could not be read.");
			return;
		}

		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		ImGui::InvisibleButton("##music_timeline",
							   ImVec2(width, kWaveformHeight * ImGui::GetStyle()._MainScale));
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(min, max, true);
		drawList->AddRectFilled(min, max, Colors::Theme::backgroundDark, 4.0f);
		DrawPeaks(drawList, min, max, waveform.Peaks);
		const auto timeToX = [&](float seconds) { return min.x + width * std::clamp(seconds / duration, 0.0f, 1.0f); };
		const auto xToTime = [&](float x) { return std::clamp((x - min.x) / width, 0.0f, 1.0f) * duration; };

		// Beats from the first-beat offset, every 60/BPM seconds; a bar line every Beats per Bar.
		// Too dense to read (under 4 px a beat) draws bars only.
		if (sound.Tempo.BPM > 0.0f)
		{
			const double spacing = 60.0 / sound.Tempo.BPM;
			const bool beats = width * spacing / duration >= 4.0;
			const uint32_t perBar = std::max(1u, sound.Tempo.BeatsPerBar);
			uint32_t bar = 1;
			for (uint64_t beat = 0; sound.Tempo.Offset + beat * spacing <= duration && beat < 100000; ++beat)
			{
				const float x = timeToX(static_cast<float>(sound.Tempo.Offset + beat * spacing));
				if (beat % perBar == 0)
				{
					drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 255, 255, 90), 1.0f);
					if (width * spacing * perBar / duration >= 24.0)
						drawList->AddText(ImVec2(x + 2.0f, min.y + 1.0f), Colors::Theme::textDarker, std::to_string(bar).c_str());
					++bar;
				}
				else if (beats)
					drawList->AddLine(ImVec2(x, max.y - 10.0f), ImVec2(x, max.y), IM_COL32(255, 255, 255, 50), 1.0f);
			}
		}

		const float mouseX = ImGui::GetIO().MousePos.x;
		int hovered = -1;
		float nearest = kHandleGrabPixels;
		for (size_t index = 0; index < sound.Markers.size(); ++index)
		{
			const float distance = std::abs(timeToX(sound.Markers[index].Seconds) - mouseX);
			if (ImGui::IsItemHovered() && distance <= nearest)
			{
				nearest = distance;
				hovered = static_cast<int>(index);
			}
		}
		if (ImGui::IsItemActivated())
			m_DragMarker = hovered;
		if (ImGui::IsItemActive() && m_DragMarker >= 0 && m_DragMarker < static_cast<int>(sound.Markers.size())
			&& ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			sound.Markers[static_cast<size_t>(m_DragMarker)].Seconds = xToTime(mouseX);
			edited = true;
		}
		for (size_t index = 0; index < sound.Markers.size(); ++index)
		{
			const SoundMarker& marker = sound.Markers[index];
			const float x = timeToX(marker.Seconds);
			const bool active = static_cast<int>(index) == hovered || static_cast<int>(index) == m_DragMarker;
			drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), Colors::Theme::accent, active ? 3.0f : 1.5f);
			drawList->AddText(ImVec2(x + 3.0f, max.y - ImGui::GetTextLineHeight() - 12.0f), Colors::Theme::accent, marker.Name.c_str());
		}
		drawList->PopClipRect();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%.3f s of %.3f s\nDrag a marker to move it.", xToTime(mouseX), duration);
	}

	void SoundEditorPanel::UI_Clip(SoundNode& node, bool& edited)
	{
		BeginSoundPropertyGrid();
		AssetHandle clip = node.Clip;
		if (ImGuiEx::PropertyAssetReference<AudioFile>("Clip", clip, "The audio file this node plays.", nullptr, {}, false))
		{
			node.Clip = clip;
			edited = true;
		}
		const Waveform* waveform = node.Clip ? &GetWaveform(node.Clip) : nullptr;
		const float duration = waveform ? waveform->Duration : 0.0f;
		const float maxTime = duration > 0.0f ? duration : 3600.0f;
		edited |= ImGuiEx::Property("Start Offset", node.StartOffset, 0.01f, 0.0f, maxTime, "Seconds into the clip where playback starts.", false, SoundNode{}.StartOffset);
		edited |= ImGuiEx::Property("Loop", node.Loop, "Repeat until stopped. A Sound with any looping clip is continuous.", false);
		if (node.Loop)
		{
			bool region = node.HasLoopRegion;
			if (ImGuiEx::Property("Loop Region", region, "Loop only part of the clip, after playing up to it once.", false))
			{
				node.HasLoopRegion = region;
				if (region && node.LoopEnd <= node.LoopStart)
				{
					node.LoopStart = 0.0f;
					node.LoopEnd = duration > 0.0f ? duration : 1.0f;
				}
				edited = true;
			}
			if (node.HasLoopRegion)
			{
				edited |= ImGuiEx::Property("Loop Start", node.LoopStart, 0.01f, 0.0f, maxTime, "Seconds.", false, 0.0f);
				edited |= ImGuiEx::Property("Loop End", node.LoopEnd, 0.01f, 0.0f, maxTime, "Seconds.", false, maxTime);
			}
		}
		ImGuiEx::EndPropertyGrid();

		if (!waveform)
			return;
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		ImGui::InvisibleButton("##sound_waveform",
							   ImVec2(width, kWaveformHeight * ImGui::GetStyle()._MainScale));
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(min, max, true);
		drawList->AddRectFilled(min, max, Colors::Theme::backgroundDark, 4.0f);
		if (waveform->Peaks.empty() || duration <= 0.0f)
		{
			drawList->AddText(ImVec2(min.x + 8.0f, min.y + 8.0f), Colors::Theme::textDarker,
				"The waveform could not be read.");
			drawList->PopClipRect();
			return;
		}
		DrawPeaks(drawList, min, max, waveform->Peaks);
		const auto timeToX = [&](float seconds) { return min.x + width * std::clamp(seconds / duration, 0.0f, 1.0f); };
		const auto xToTime = [&](float x) { return std::clamp((x - min.x) / width, 0.0f, 1.0f) * duration; };
		if (node.Loop && node.HasLoopRegion)
			drawList->AddRectFilled(ImVec2(timeToX(node.LoopStart), min.y), ImVec2(timeToX(node.LoopEnd), max.y), IM_COL32(200, 255, 77, 40));

		// Handles: 0 start offset, 1 loop start, 2 loop end.
		std::vector<std::pair<int, float>> handles{ { 0, node.StartOffset } };
		if (node.Loop && node.HasLoopRegion)
		{
			handles.push_back({ 1, node.LoopStart });
			handles.push_back({ 2, node.LoopEnd });
		}
		const float mouseX = ImGui::GetIO().MousePos.x;
		int hovered = -1;
		float nearest = kHandleGrabPixels;
		for (const auto& [handle, seconds] : handles)
		{
			const float distance = std::abs(timeToX(seconds) - mouseX);
			if (ImGui::IsItemHovered() && distance <= nearest)
			{
				nearest = distance;
				hovered = handle;
			}
		}
		if (ImGui::IsItemActivated())
		{
			m_DragClipHandle = hovered;
			if (hovered < 0)
			{
				// A click away from the handles moves the start offset there.
				node.StartOffset = xToTime(mouseX);
				m_DragClipHandle = 0;
				edited = true;
			}
		}
		if (ImGui::IsItemActive() && m_DragClipHandle >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			const float seconds = xToTime(mouseX);
			const float gap = duration * 0.001f;
			if (m_DragClipHandle == 0)
				node.StartOffset = seconds;
			else if (m_DragClipHandle == 1)
				node.LoopStart = std::min(seconds, node.LoopEnd - gap);
			else
				node.LoopEnd = std::max(seconds, node.LoopStart + gap);
			node.LoopStart = std::max(node.LoopStart, 0.0f);
			edited = true;
		}
		for (const auto& [handle, seconds] : handles)
		{
			const float x = timeToX(seconds);
			const ImU32 color = handle == 0 ? Colors::Theme::accent : Colors::Theme::highlight;
			drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), color, handle == hovered ? 3.0f : 1.5f);
		}
		drawList->PopClipRect();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%.3f s of %.3f s\nClick or drag to set the start offset; drag the loop markers to set the region.", xToTime(mouseX), duration);
	}

	void SoundEditorPanel::UI_Curves(Document& document, Sound& sound, SoundNode& node, bool& edited)
	{
		ImGui::Spacing();
		ImGui::TextWrapped("Parameter Curves");
		ImGui::Separator();
		int removeCurve = -1;
		std::vector<std::string> parameterNames;
		for (const auto& parameter : sound.Parameters)
			parameterNames.push_back(parameter.Name);
		for (size_t index = 0; index < node.Curves.size(); ++index)
		{
			SoundCurve& curve = node.Curves[index];
			ImGui::PushID(static_cast<int>(index));
			BeginSoundPropertyGrid();
			int32_t parameter = 0;
			for (size_t name = 0; name < parameterNames.size(); ++name)
			{
				if (Utils::String::EqualsIgnoreCase(parameterNames[name], curve.Parameter))
					parameter = static_cast<int32_t>(name);
			}
			if (!parameterNames.empty()
				&& ImGuiEx::PropertyDropdown("Parameter", parameterNames, static_cast<int32_t>(parameterNames.size()), &parameter, "", false))
			{
				curve.Parameter = parameterNames[static_cast<size_t>(parameter)];
				edited = true;
			}
			SoundCurveTarget target = curve.Target;
			if (ImGuiEx::PropertyDropdown("Drives", s_TargetNames, static_cast<int32_t>(std::size(s_TargetNames)), target,
				"Volume and pitch multiply down the tree; the lowest low-pass and highest high-pass on a path win; the send feeds the reverb.", false))
			{
				// Values mean something else for the new target: start it flat at its neutral value.
				curve.Target = target;
				for (auto& point : curve.Points)
					point.y = TargetDefault(target);
				edited = true;
			}
			ImGuiEx::EndPropertyGrid();
			const uint32_t id = document.SelectedNode * 4096u + static_cast<uint32_t>(index);
			edited |= UI_CurveEditor(document, sound, curve, id);
			const ImGuiEx::PanelAction removeCurveAction[] = {{0, "Remove curve", LUX_ICON_TRASH}};
			if (ImGuiEx::PanelActionBar("##remove_curve", removeCurveAction) == 0)
				removeCurve = static_cast<int>(index);
			ImGui::Separator();
			ImGui::PopID();
		}
		if (removeCurve >= 0)
		{
			node.Curves.erase(node.Curves.begin() + removeCurve);
			edited = true;
		}
		const ImGuiEx::PanelAction addCurve[] = {
			{0, "Add curve", LUX_ICON_PLUS, nullptr, !sound.Parameters.empty()}};
		if (ImGuiEx::PanelActionBar("##add_curve", addCurve) == 0)
		{
			const auto& parameter = sound.Parameters.front();
			SoundCurve curve{ parameter.Name, SoundCurveTarget::Volume, {} };
			curve.Points.push_back({ parameter.Minimum, 1.0f });
			if (parameter.Maximum > parameter.Minimum)
				curve.Points.push_back({ parameter.Maximum, 1.0f });
			node.Curves.push_back(std::move(curve));
			edited = true;
		}
		if (sound.Parameters.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Add a parameter first.");
	}

	bool SoundEditorPanel::UI_CurveEditor(const Document& document, const Sound& sound, SoundCurve& curve, uint32_t id)
	{
		if (curve.Points.empty())
			return false;
		bool changed = false;
		const glm::vec2 xRange = ParameterRange(sound, curve.Parameter);
		const glm::vec2 limits = TargetLimits(curve.Target);
		const bool logarithmic = IsFilter(curve.Target);
		// Filters are drawn over their whole range on a log scale; the send over 0 to 1; volume and
		// pitch to 2, or to their highest point.
		glm::vec2 yRange = logarithmic ? limits : glm::vec2(0.0f, curve.Target == SoundCurveTarget::Send ? 1.0f : 2.0f);
		if (!logarithmic)
		{
			for (const auto& point : curve.Points)
				yRange.y = std::max(yRange.y, point.y);
		}

		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		ImGui::InvisibleButton("##sound_curve", ImVec2(width, kCurveHeight * ImGui::GetStyle()._MainScale));
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		const auto toY = [&](float value)
		{
			const float t = logarithmic ? (std::log(value) - std::log(yRange.x)) / (std::log(yRange.y) - std::log(yRange.x)) : (value - yRange.x) / (yRange.y - yRange.x);
			return max.y - std::clamp(t, 0.0f, 1.0f) * (max.y - min.y);
		};
		const auto fromY = [&](float y)
		{
			const float t = std::clamp((max.y - y) / (max.y - min.y), 0.0f, 1.0f);
			return logarithmic ? std::exp(std::log(yRange.x) + t * (std::log(yRange.y) - std::log(yRange.x))) : yRange.x + t * (yRange.y - yRange.x);
		};
		const auto toX = [&](float value) { return min.x + std::clamp((value - xRange.x) / (xRange.y - xRange.x), 0.0f, 1.0f) * (max.x - min.x); };
		const auto fromX = [&](float x) { return xRange.x + std::clamp((x - min.x) / (max.x - min.x), 0.0f, 1.0f) * (xRange.y - xRange.x); };

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(min, max, true);
		drawList->AddRectFilled(min, max, Colors::Theme::backgroundDark, 4.0f);
		if (!logarithmic && yRange.x <= 1.0f && 1.0f <= yRange.y && curve.Target != SoundCurveTarget::Send)
			drawList->AddLine(ImVec2(min.x, toY(1.0f)), ImVec2(max.x, toY(1.0f)), IM_COL32(255, 255, 255, 30)); // unity

		// The curve is flat beyond its first and last points.
		std::vector<ImVec2> line{ ImVec2(min.x, toY(curve.Points.front().y)) };
		for (const auto& point : curve.Points)
			line.emplace_back(toX(point.x), toY(point.y));
		line.emplace_back(max.x, toY(curve.Points.back().y));
		drawList->AddPolyline(line.data(), static_cast<int>(line.size()), Colors::Theme::accent, ImDrawFlags_None, 2.0f);

		const ImVec2 mouse = ImGui::GetIO().MousePos;
		int hovered = -1;
		float nearest = kHandleGrabPixels * kHandleGrabPixels;
		if (ImGui::IsItemHovered())
		{
			for (size_t point = 0; point < curve.Points.size(); ++point)
			{
				const float dx = toX(curve.Points[point].x) - mouse.x, dy = toY(curve.Points[point].y) - mouse.y;
				if (dx * dx + dy * dy <= nearest)
				{
					nearest = dx * dx + dy * dy;
					hovered = static_cast<int>(point);
				}
			}
		}

		if (ImGui::IsItemActivated() && hovered >= 0)
		{
			m_DragPoint = hovered;
			m_DragCurve = id;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered < 0)
		{
			// Insert keeping x strictly increasing; a click on an existing x adds nothing.
			const glm::vec2 point{ fromX(mouse.x), std::clamp(fromY(mouse.y), limits.x, limits.y) };
			const auto position = std::lower_bound(curve.Points.begin(), curve.Points.end(), point.x, [](const glm::vec2& p, float x) { return p.x < x; });
			if ((position == curve.Points.end() || position->x != point.x) && curve.Points.size() < 64)
			{
				curve.Points.insert(position, point);
				changed = true;
			}
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered >= 0 && curve.Points.size() > 1)
		{
			curve.Points.erase(curve.Points.begin() + hovered);
			hovered = -1;
			changed = true;
		}
		if (ImGui::IsItemActive() && m_DragCurve == id && m_DragPoint >= 0 && m_DragPoint < static_cast<int>(curve.Points.size())
			&& ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			auto& point = curve.Points[static_cast<size_t>(m_DragPoint)];
			const float gap = (xRange.y - xRange.x) * 0.001f;
			float lower = m_DragPoint > 0 ? curve.Points[static_cast<size_t>(m_DragPoint) - 1].x + gap : -std::numeric_limits<float>::max();
			float upper = m_DragPoint + 1 < static_cast<int>(curve.Points.size()) ? curve.Points[static_cast<size_t>(m_DragPoint) + 1].x - gap : std::numeric_limits<float>::max();
			const glm::vec2 moved{ lower <= upper ? std::clamp(fromX(mouse.x), lower, upper) : point.x, std::clamp(fromY(mouse.y), limits.x, limits.y) };
			if (moved != point)
			{
				point = moved;
				changed = true;
			}
		}

		for (size_t point = 0; point < curve.Points.size(); ++point)
		{
			const bool active = static_cast<int>(point) == hovered || (m_DragCurve == id && m_DragPoint == static_cast<int>(point));
			drawList->AddCircleFilled(ImVec2(toX(curve.Points[point].x), toY(curve.Points[point].y)), active ? kPointRadius + 2.0f : kPointRadius,
				active ? Colors::Theme::highlight : Colors::Theme::accent);
		}

		// Where the audition's value sits on the curve.
		const auto audition = document.AuditionValues.find(curve.Parameter);
		const SoundParameter* parameter = sound.FindParameter(curve.Parameter);
		const float value = audition != document.AuditionValues.end() ? audition->second : parameter ? parameter->Default : xRange.x;
		drawList->AddLine(ImVec2(toX(value), min.y), ImVec2(toX(value), max.y), IM_COL32(255, 255, 255, 80));
		const std::string reading = std::format("{} {:.2f} -> {:.2f}", curve.Parameter, value, curve.Evaluate(value));
		drawList->AddText(ImVec2(min.x + 6.0f, min.y + 4.0f), Colors::Theme::textDarker, reading.c_str());

		drawList->PopClipRect();
		if (ImGui::IsItemHovered())
		{
			if (hovered >= 0)
				ImGui::SetTooltip("%.3f -> %.3f\nDrag to move, right-click to remove.", curve.Points[static_cast<size_t>(hovered)].x, curve.Points[static_cast<size_t>(hovered)].y);
			else
				ImGui::SetTooltip("Double-click to add a point.");
		}
		return changed;
	}
}
