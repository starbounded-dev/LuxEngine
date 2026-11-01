// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SoundEditorPanel.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>

namespace Lux
{
	namespace
	{
		constexpr float kTreeMinWidth = 200.0f, kTreeMaxWidth = 380.0f, kTreeWidthFraction = 0.32f;
		constexpr float kWaveformHeight = 72.0f, kCurveHeight = 120.0f;
		constexpr float kHandleGrabPixels = 6.0f, kPointRadius = 4.0f;
		constexpr uint32_t kWaveformBuckets = 512;

		// Not constexpr: PropertyDropdown takes a mutable const char** list. Ordered as the enums.
		const char* s_NodeTypeNames[] = { "Clip", "Random", "Sequence", "Layer", "Switch" };
		const char* s_TargetNames[] = { "Volume", "Pitch", "Low-pass", "High-pass", "Reverb Send" };
		const char* s_AttenuationNames[] = { "None", "Inverse", "Linear" };
		const char* s_StealNames[] = { "Oldest", "Quietest", "None" };

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
					return metadata.FilePath.stem().string();
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
	}

	// -- Documents -------------------------------------------------------------------------------

	void SoundEditorPanel::OpenSound(AssetHandle handle)
	{
		if (!handle || AssetManager::GetAssetType(handle) != AssetType::Sound)
		{
			LUX_CORE_WARN_TAG("Editor", "Sound Editor: asset {} is not a Sound", static_cast<uint64_t>(handle));
			return;
		}
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

	void SoundEditorPanel::OnProjectChanged(const Ref<Project>&)
	{
		// Handles belong to the previous project.
		StopAudition();
		m_Documents.clear();
		m_ActiveDocument = -1;
		m_PendingCloseIndex = -1;
		m_Waveforms.clear();
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
		if (m_AuditionSound == m_Documents[index].Handle)
			StopAudition();
		m_Documents.erase(m_Documents.begin() + static_cast<ptrdiff_t>(index));
		if (m_ActiveDocument >= static_cast<int>(m_Documents.size()))
			m_ActiveDocument = static_cast<int>(m_Documents.size()) - 1;
		m_SelectActiveTab = true;
	}

	bool SoundEditorPanel::SaveDocument(Document& document, Sound& sound)
	{
		std::string error;
		if (!sound.Validate(&error))
		{
			LUX_CORE_ERROR_TAG("Editor", "Sound Editor: cannot save '{}': {}", AssetName(document.Handle, "Sound"), error);
			return false;
		}
		if (sound.ToYAML().size() > Sound::MaxFileBytes)
		{
			LUX_CORE_ERROR_TAG("Editor", "Sound Editor: cannot save '{}': it is larger than the 64 KiB a Sound file may be", AssetName(document.Handle, "Sound"));
			return false;
		}
		const Ref<Sound> asset = AssetManager::GetAsset<Sound>(document.Handle);
		if (!asset)
		{
			LUX_CORE_ERROR_TAG("Editor", "Sound Editor: cannot save Sound {}: it is no longer loaded", static_cast<uint64_t>(document.Handle));
			return false;
		}
		AssetImporter::Serialize(asset.As<Asset>());
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
	void SoundEditorPanel::TrackEdit(Document& document, Sound& sound, bool edited)
	{
		if (edited)
			document.EditInProgress = true;
		if (!document.EditInProgress || ImGui::IsAnyItemActive())
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
			return;
		for (const auto& [name, value] : document.AuditionValues)
		{
			if (copy->FindParameter(name))
				m_Audition->SetParameter(name, value);
		}
		m_AuditionSound = document.Handle;
		if (!m_Audition->Start())
			StopAudition();
	}

	void SoundEditorPanel::StopAudition()
	{
		if (m_Audition && m_Audition->IsValid())
			m_Audition->Stop();
		m_Audition = nullptr;
		m_AuditionSound = 0;
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
			StopAudition();
	}

	void SoundEditorPanel::SetAuditionParameter(Document& document, const std::string& name, float value)
	{
		document.AuditionValues[name] = value;
		if (m_Audition && m_AuditionSound == document.Handle && m_Audition->IsValid())
			m_Audition->SetParameter(name, value);
	}

	const SoundEditorPanel::Waveform& SoundEditorPanel::GetWaveform(AssetHandle clip)
	{
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
		UpdateAudition();

		ImGui::SetNextWindowSize(ImVec2(960.0f, 620.0f), ImGuiCond_FirstUseEver);
		const bool visible = ImGui::Begin("Sound Editor", &isOpen);
		if (visible)
		{
			if (!Project::GetActive())
				ImGui::TextDisabled("Open a project to edit Sounds.");
			else if (m_Documents.empty())
			{
				ImGui::TextDisabled("No Sound open. Double-click a Sound in the Content Browser,");
				ImGui::TextDisabled("or use Edit next to a Sound in the Inspector.");
			}
			else
			{
				UI_Tabs();
				UI_CloseConfirm();

				Document* document = GetActiveDocument();
				Ref<Sound> sound = document ? AssetManager::GetAsset<Sound>(document->Handle) : nullptr;
				if (!document || !sound)
					ImGui::TextDisabled("This Sound could not be loaded.");
				else
				{
					if (document->SelectedNode >= sound->Nodes.size())
						document->SelectedNode = 0;
					bool edited = false;
					UI_Toolbar(*document, *sound, edited);
					ImGui::Separator();

					const float treeWidth = std::clamp(ImGui::GetContentRegionAvail().x * kTreeWidthFraction, kTreeMinWidth, kTreeMaxWidth);
					if (ImGui::BeginChild("##sound_tree_column", ImVec2(treeWidth, 0.0f), ImGuiChildFlags_Borders))
						UI_Tree(*document, *sound, edited);
					ImGui::EndChild();
					ImGui::SameLine();
					if (ImGui::BeginChild("##sound_properties_column", ImVec2(0.0f, 0.0f)))
					{
						UI_Node(*document, *sound, edited);
						UI_Parameters(*document, *sound, edited);
						UI_SoundProperties(*sound, edited);
					}
					ImGui::EndChild();

					TrackEdit(*document, *sound, edited);
				}
			}
		}
		ImGui::End();

		if (!isOpen)
			StopAudition();
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			m_DragPoint = -1;
			m_DragClipHandle = -1;
		}
	}

	void SoundEditorPanel::UI_Tabs()
	{
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
				if (ImGui::BeginTabItem(label.c_str(), &open, flags))
				{
					if (!m_SelectActiveTab)
						m_ActiveDocument = static_cast<int>(i);
					ImGui::EndTabItem();
				}
				if (!open)
					closeRequest = static_cast<int>(i);
			}
			ImGui::EndTabBar();
		}
		m_SelectActiveTab = false;

		if (closeRequest < 0)
			return;
		const Document& closing = m_Documents[static_cast<size_t>(closeRequest)];
		if (closing.Current == closing.Saved && !closing.EditInProgress)
			CloseDocument(static_cast<size_t>(closeRequest));
		else
		{
			m_PendingCloseIndex = closeRequest;
			m_OpenCloseConfirm = true;
		}
	}

	void SoundEditorPanel::UI_CloseConfirm()
	{
		if (m_OpenCloseConfirm)
		{
			ImGui::OpenPopup("Unsaved Sound##sound_close");
			m_OpenCloseConfirm = false;
		}
		if (!ImGui::BeginPopupModal("Unsaved Sound##sound_close", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		const bool valid = m_PendingCloseIndex >= 0 && m_PendingCloseIndex < static_cast<int>(m_Documents.size());
		Ref<Sound> sound = valid ? AssetManager::GetAsset<Sound>(m_Documents[static_cast<size_t>(m_PendingCloseIndex)].Handle) : nullptr;
		if (valid)
			ImGui::Text("'%s' has unsaved changes.", AssetName(m_Documents[static_cast<size_t>(m_PendingCloseIndex)].Handle, "Sound").c_str());

		const bool canSave = valid && sound && sound->Validate();
		ImGui::BeginDisabled(!canSave);
		// A refused save (the reason is logged) keeps the tab and the dialog open.
		if (ImGui::Button("Save") && canSave && SaveDocument(m_Documents[static_cast<size_t>(m_PendingCloseIndex)], *sound))
		{
			CloseDocument(static_cast<size_t>(m_PendingCloseIndex));
			m_PendingCloseIndex = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Discard") && valid)
		{
			const Document& document = m_Documents[static_cast<size_t>(m_PendingCloseIndex)];
			if (sound)
				document.Saved.Apply(*sound);
			CloseDocument(static_cast<size_t>(m_PendingCloseIndex));
			m_PendingCloseIndex = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || !valid)
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
		const bool dirty = !(document.Current == document.Saved) || document.EditInProgress;

		ImGui::BeginDisabled(!dirty || !valid);
		if (ImGui::Button(LUX_ICON_FLOPPY_O "  Save"))
			SaveDocument(document, sound);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!dirty);
		// Reverting goes through TrackEdit like any other change, so it can be undone.
		if (ImGui::Button(LUX_ICON_UNDO "  Revert"))
		{
			document.Saved.Apply(sound);
			edited = true;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		const bool native = AudioEngine::GetBackend() == AudioBackend::Native && AudioEngine::HasInitializedEngine();
		const bool playing = m_Audition && m_AuditionSound == document.Handle && m_Audition->IsValid() && m_Audition->IsPlaying();
		ImGui::BeginDisabled(!native || (!valid && !playing));
		if (ImGui::Button(playing ? LUX_ICON_STOP "  Stop###sound_audition" : LUX_ICON_PLAY "  Play###sound_audition"))
		{
			if (playing)
				StopAudition();
			else
				StartAudition(document, sound);
		}
		ImGui::SameLine();
		ImGui::Checkbox("Loop##sound_audition_loop", &m_LoopAudition);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Restart the audition when it ends; each restart chooses through the tree again.");

		ImGui::SameLine();
		if (!native)
			ImGui::TextDisabled("Auditioning needs the native audio backend.");
		else if (!valid)
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError), "Invalid: %s", error.c_str());
		else
			ImGui::TextDisabled("%s", dirty ? "Unsaved changes" : "Saved");
	}

	void SoundEditorPanel::UI_Tree(Document& document, Sound& sound, bool& edited)
	{
		const std::vector<int> parents = sound.GetParents();
		const uint32_t selected = document.SelectedNode;
		const SoundNodeType selectedType = sound.Nodes[selected].Type;
		// New nodes go under the selection, or beside it when it is a clip.
		const int addParent = selectedType != SoundNodeType::Clip ? static_cast<int>(selected) : parents[selected];

		if (ImGui::Button(LUX_ICON_PLUS "  Add"))
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
		ImGui::SameLine();
		ImGui::BeginDisabled(!siblings || position == 0);
		if (ImGui::Button(LUX_ICON_ARROW_UP "##sound_tree_up"))
		{
			std::swap((*siblings)[position], (*siblings)[position - 1]);
			edited = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!siblings || position + 1 >= siblings->size());
		if (ImGui::Button(LUX_ICON_ARROW_DOWN "##sound_tree_down"))
		{
			std::swap((*siblings)[position], (*siblings)[position + 1]);
			edited = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(current == 0);
		if (ImGui::Button(LUX_ICON_TRASH "##sound_tree_remove"))
		{
			sound.RemoveNode(current);
			document.SelectedNode = 0;
			edited = true;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Remove the selected node and everything under it.");

		ImGui::Separator();
		UI_TreeNode(document, sound, 0, 1);
	}

	void SoundEditorPanel::UI_TreeNode(Document& document, const Sound& sound, uint32_t index, uint32_t depth)
	{
		// Validate keeps the tree acyclic and at most MaxDepth deep; this guards an edit in progress.
		if (depth > Sound::MaxDepth || index >= sound.Nodes.size())
			return;
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
		const bool open = ImGui::TreeNodeEx("node", flags, "%s  %s%s%s", NodeIcon(node.Type), label.c_str(), looping ? "  " : "", looping ? LUX_ICON_REPEAT : "");
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			document.SelectedNode = index;
		if (open)
		{
			// A Switch's children are its labels, in order.
			const SoundParameter* switchParameter = node.Type == SoundNodeType::Switch ? sound.FindParameter(node.SwitchParameter) : nullptr;
			for (size_t child = 0; child < node.Children.size(); ++child)
			{
				if (switchParameter && child < switchParameter->Labels.size())
					ImGui::TextDisabled("%s", switchParameter->Labels[child].c_str());
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
		if (ImGuiEx::PropertyGridHeader("Sound"))
		{
			ImGuiEx::BeginPropertyGrid();
			edited |= ImGuiEx::Property("Volume", sound.Volume, 0.01f, 0.0f, 4.0f, "Linear gain; 1 plays the clips as recorded.", false);
			edited |= ImGuiEx::Property("Pitch", sound.Pitch, 0.01f, 0.01f, 4.0f, "Playback rate; 2 is an octave up and twice as fast.", false);
			edited |= ImGuiEx::Property("Bus", sound.Bus, "Where it mixes (bus:/...). Every bus plays through the master until the mixer exists.", false);
			edited |= ImGuiEx::Property("Stream", sound.Stream, "Read from disk while playing instead of decoding into memory first. For music and long ambience.", false);
			edited |= ImGuiEx::Property("Instance Limit", sound.InstanceLimit, 0u, Sound::MaxInstanceLimit, "Most instances playing at once; 0 is unlimited.", false);
			if (sound.InstanceLimit > 0)
				edited |= ImGuiEx::PropertyDropdown("Steal", s_StealNames, static_cast<int32_t>(std::size(s_StealNames)), sound.Steal,
					"At the limit, stop the oldest or quietest instance for the new one, or refuse the new one.", false);
			edited |= ImGuiEx::Property("Cooldown", sound.Cooldown, 0.01f, 0.0f, 60.0f, "Seconds after a start before the Sound may start again.", false);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Spatial"))
		{
			ImGuiEx::BeginPropertyGrid();
			edited |= ImGuiEx::Property("3D", sound.Spatial.Enabled, "Panned and attenuated from its position. Off plays it in 2D.", false);
			if (sound.Spatial.Enabled)
			{
				edited |= ImGuiEx::PropertyDropdown("Attenuation", s_AttenuationNames, static_cast<int32_t>(std::size(s_AttenuationNames)), sound.Spatial.Attenuation,
					"Inverse falls off like a real source; Linear reaches silence at the maximum distance.", false);
				edited |= ImGuiEx::Property("Min Distance", sound.Spatial.MinDistance, 0.1f, 0.01f, 10000.0f, "Metres; full volume up to here.", false);
				edited |= ImGuiEx::Property("Max Distance", sound.Spatial.MaxDistance, 0.1f, 0.01f, 10000.0f, "Metres; attenuation stops here, and the source is culled beyond it.", false);
				edited |= ImGuiEx::Property("Rolloff", sound.Spatial.Rolloff, 0.01f, 0.0f, 10.0f, "How quickly it falls off; 1 is natural.", false);
			}
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}
	}

	void SoundEditorPanel::UI_Parameters(Document& document, Sound& sound, bool& edited)
	{
		if (!ImGuiEx::PropertyGridHeader("Parameters"))
			return;

		int removeParameter = -1;
		for (size_t index = 0; index < sound.Parameters.size(); ++index)
		{
			SoundParameter& parameter = sound.Parameters[index];
			ImGui::PushID(static_cast<int>(index));
			ImGuiEx::BeginPropertyGrid();
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
				edited |= ImGuiEx::Property("Minimum", parameter.Minimum, 0.01f, 0.0f, 0.0f, "", false);
				edited |= ImGuiEx::Property("Maximum", parameter.Maximum, 0.01f, 0.0f, 0.0f, "", false);
				edited |= ImGuiEx::Property("Default", parameter.Default, 0.01f, parameter.Minimum, parameter.Maximum, "", false);
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
				&& ImGuiEx::PropertySlider("Audition", value, parameter.Minimum, parameter.Maximum, "The value the Play button uses; changes are heard live.", false))
				SetAuditionParameter(document, parameter.Name, value);
			ImGuiEx::EndPropertyGrid();

			if (parameter.IsLabeled())
			{
				if (ImGui::SmallButton(LUX_ICON_PLUS " Label"))
				{
					parameter.Labels.push_back(std::format("Label {}", parameter.Labels.size()));
					FitLabels(parameter);
					edited = true;
				}
				ImGui::SameLine();
				ImGui::BeginDisabled(parameter.Labels.size() <= 1);
				if (ImGui::SmallButton(LUX_ICON_MINUS " Label"))
				{
					parameter.Labels.pop_back();
					FitLabels(parameter);
					edited = true;
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
			}
			if (ImGui::SmallButton(LUX_ICON_TRASH " Remove Parameter"))
				removeParameter = static_cast<int>(index);
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

		ImGui::BeginDisabled(sound.Parameters.size() >= Sound::MaxParameters);
		if (ImGui::Button(LUX_ICON_PLUS "  Add Parameter"))
		{
			sound.Parameters.push_back({ UniqueParameterName(sound), 0.0f, 1.0f, 0.0f, {} });
			edited = true;
		}
		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	void SoundEditorPanel::UI_Node(Document& document, Sound& sound, bool& edited)
	{
		const uint32_t index = document.SelectedNode;
		SoundNode& node = sound.Nodes[index];
		const std::vector<int> parents = sound.GetParents();
		const int parent = parents[index];
		// One header name whatever the node, so it stays open across selections.
		if (!ImGuiEx::PropertyGridHeader("Selected Node"))
			return;

		ImGui::PushID(static_cast<int>(index));
		ImGuiEx::BeginPropertyGrid();
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
			edited |= ImGuiEx::Property("Weight", node.Weight, 0.01f, 0.0f, 1000.0f, "Chance of being chosen, relative to its siblings. 0 is never.", false);
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
		edited |= ImGuiEx::Property("Volume", node.Volume, 0.01f, 0.0f, 4.0f, "Multiplies everything below this node.", false);
		edited |= ImGuiEx::Property("Pitch", node.Pitch, 0.01f, 0.01f, 4.0f, "Multiplies everything below this node.", false);
		edited |= ImGuiEx::Property("Volume Variation", node.VolumeVariation, 0.1f, 0.0f, 24.0f, "dB; each start lowers the volume by a random amount up to this.", false);
		edited |= ImGuiEx::Property("Pitch Variation", node.PitchVariation, 0.05f, 0.0f, 12.0f, "Semitones; each start shifts the pitch by a random amount up to this, either way.", false);
		ImGuiEx::EndPropertyGrid();

		if (node.Type == SoundNodeType::Clip)
			UI_Clip(node, edited);
		UI_Curves(document, sound, node, edited);
		ImGui::PopID();
		ImGui::TreePop();
	}

	void SoundEditorPanel::UI_Clip(SoundNode& node, bool& edited)
	{
		ImGuiEx::BeginPropertyGrid();
		AssetHandle clip = node.Clip;
		if (ImGuiEx::PropertyAssetReference<AudioFile>("Clip", clip, "The audio file this node plays.", nullptr, {}, false))
		{
			node.Clip = clip;
			edited = true;
		}
		const Waveform* waveform = node.Clip ? &GetWaveform(node.Clip) : nullptr;
		const float duration = waveform ? waveform->Duration : 0.0f;
		const float maxTime = duration > 0.0f ? duration : 3600.0f;
		edited |= ImGuiEx::Property("Start Offset", node.StartOffset, 0.01f, 0.0f, maxTime, "Seconds into the clip where playback starts.", false);
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
				edited |= ImGuiEx::Property("Loop Start", node.LoopStart, 0.01f, 0.0f, maxTime, "Seconds.", false);
				edited |= ImGuiEx::Property("Loop End", node.LoopEnd, 0.01f, 0.0f, maxTime, "Seconds.", false);
			}
		}
		ImGuiEx::EndPropertyGrid();

		if (!waveform)
			return;
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		ImGui::InvisibleButton("##sound_waveform", ImVec2(width, kWaveformHeight));
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(min, max, Colors::Theme::backgroundDark, 4.0f);
		if (waveform->Peaks.empty() || duration <= 0.0f)
		{
			drawList->AddText(ImVec2(min.x + 8.0f, min.y + 8.0f), Colors::Theme::textDarker,
				AudioEngine::GetBackend() == AudioBackend::Native ? "The waveform could not be read." : "Waveforms need the native audio backend.");
			return;
		}
		const float middle = (min.y + max.y) * 0.5f, halfHeight = (max.y - min.y) * 0.5f - 2.0f;
		const auto peaks = waveform->Peaks.size();
		for (size_t bucket = 0; bucket < peaks; ++bucket)
		{
			const float x0 = min.x + width * static_cast<float>(bucket) / static_cast<float>(peaks);
			const float x1 = min.x + width * static_cast<float>(bucket + 1) / static_cast<float>(peaks);
			const float extent = std::max(0.5f, waveform->Peaks[bucket] * halfHeight);
			drawList->AddRectFilled(ImVec2(x0, middle - extent), ImVec2(std::max(x0 + 1.0f, x1), middle + extent), Colors::Theme::textDarker);
		}
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
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%.3f s of %.3f s\nClick or drag to set the start offset; drag the loop markers to set the region.", xToTime(mouseX), duration);
	}

	void SoundEditorPanel::UI_Curves(Document& document, Sound& sound, SoundNode& node, bool& edited)
	{
		ImGui::Spacing();
		ImGui::TextUnformatted("Parameter Curves");
		ImGui::Separator();
		int removeCurve = -1;
		std::vector<std::string> parameterNames;
		for (const auto& parameter : sound.Parameters)
			parameterNames.push_back(parameter.Name);
		for (size_t index = 0; index < node.Curves.size(); ++index)
		{
			SoundCurve& curve = node.Curves[index];
			ImGui::PushID(static_cast<int>(index));
			ImGuiEx::BeginPropertyGrid();
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
			if (ImGui::SmallButton(LUX_ICON_TRASH " Remove Curve"))
				removeCurve = static_cast<int>(index);
			ImGui::Separator();
			ImGui::PopID();
		}
		if (removeCurve >= 0)
		{
			node.Curves.erase(node.Curves.begin() + removeCurve);
			edited = true;
		}
		ImGui::BeginDisabled(sound.Parameters.empty());
		if (ImGui::Button(LUX_ICON_PLUS "  Add Curve"))
		{
			const auto& parameter = sound.Parameters.front();
			SoundCurve curve{ parameter.Name, SoundCurveTarget::Volume, {} };
			curve.Points.push_back({ parameter.Minimum, 1.0f });
			if (parameter.Maximum > parameter.Minimum)
				curve.Points.push_back({ parameter.Maximum, 1.0f });
			node.Curves.push_back(std::move(curve));
			edited = true;
		}
		ImGui::EndDisabled();
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
		ImGui::InvisibleButton("##sound_curve", ImVec2(width, kCurveHeight));
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
