// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioMixerPanel.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/SoundBank.h"
#include "Lux/Editor/EditorStack.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/Scene.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace Lux
{
	namespace {
		constexpr const char* kMaster = "bus:/";
		constexpr const char* k_Pages[] = {"Buses", "Reverb return", "Snapshots", "VCAs",
										   "Scene Sound Banks"};

		bool IsInside(const std::string& path, const std::string& ancestor)
		{
			return path == ancestor || (ancestor == kMaster ? path.rfind(kMaster, 0) == 0 : path.rfind(ancestor + "/", 0) == 0);
		}

		std::string Name(const std::string& path)
		{
			return path == kMaster ? std::string("Master") : path.substr(path.find_last_of('/') + 1);
		}

		int Depth(const std::string& path)
		{
			return path == kMaster ? 0 : static_cast<int>(std::count(path.begin() + 5, path.end(), '/')) + 1;
		}

		std::string ChildPath(const std::string& parent, const std::string& name)
		{
			return parent == kMaster ? kMaster + name : parent + "/" + name;
		}

		float ToDecibels(float volume)
		{
			return volume > 0.0f ? 20.0f * std::log10(volume) : -INFINITY;
		}
	}

	void AudioMixerPanel::OnProjectChanged(const Ref<Project>& project)
	{
		m_Project = project;
		m_Committed = project ? project->GetConfig().Audio.Mixer : AudioMixerSettings{};
		m_Selected = kMaster;
		m_SelectedSnapshot = 0;
		m_NameBuffer[0] = m_VCANameBuffer[0] = m_SnapshotNameBuffer[0] = '\0';
		m_NewEffectType = 0;
		m_Error.clear();
		m_Dirty = m_NeedsRestart = false;
		m_IsFocused = false;
		m_NarrowDetails = false;
		m_OpenPage = true;
		if (!project)
			m_Context = nullptr;
		StopPreview();
	}

	void AudioMixerPanel::StopPreview()
	{
		if (m_Preview)
			m_Preview->Stop(false);
		m_Preview = nullptr;
		m_PreviewIntensity = 0.0f;
	}

	AudioMixerSettings* AudioMixerPanel::GetMixer()
	{
		return m_Project ? &m_Project->GetConfig().Audio.Mixer : nullptr;
	}

	void AudioMixerPanel::ApplyLive(const AudioMixerBus& bus) const
	{
		if (!AudioEngine::HasInitializedEngine())
			return;
		// Clearing applies the edited project volume and keeps the bus under snapshots, as a mix edit
		// should; SetBusVolume would take it over as gameplay does.
		AudioEngine::ClearBusVolume(bus.Path);
		AudioEngine::SetBusMuted(bus.Path, bus.Muted);
	}

	void AudioMixerPanel::SetMixer(const AudioMixerSettings& mixer)
	{
		AudioMixerSettings* current = GetMixer();
		if (!current)
			return;
		// Volume and mute apply live; the engine builds everything else (layout, effects, reverb,
		// snapshots) at Init, so any other change needs Apply.
		const auto layout = [](AudioMixerSettings settings)
		{
			for (AudioMixerBus& bus : settings.Buses)
			{
				bus.Volume = 1.0f;
				bus.Muted = false;
			}
			return settings;
		};
		const bool structural = !(layout(*current) == layout(mixer));
		m_NeedsRestart |= structural;
		if (structural)
			StopPreview();
		*current = mixer;
		m_Committed = mixer;
		m_Dirty = true;
		for (const auto& bus : mixer.Buses)
			ApplyLive(bus);
	}

	void AudioMixerPanel::CommitEdits(bool finish)
	{
		AudioMixerSettings* mixer = GetMixer();
		if (!mixer || (!finish && ImGui::IsAnyItemActive()) || *mixer == m_Committed)
			return;
		const AudioMixerSettings before = m_Committed, after = *mixer;
		SetMixer(after);
		if (m_PushUndo)
			m_PushUndo("Edit Audio Mixer", [this, before]() { SetMixer(before); }, [this, after]() { SetMixer(after); });
	}

	void AudioMixerPanel::OnClose()
	{
		m_IsFocused = false;
		CommitEdits(true);
		StopPreview();
	}

	void AudioMixerPanel::OnEvent(Event& event)
	{
		if (!m_IsFocused)
			return;
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(
			[](KeyPressedEvent&) { return ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0; });
	}

	void AudioMixerPanel::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "Page" << Yaml::Value << k_Pages[m_Page];
		out << Yaml::Key << "BusFraction" << Yaml::Value << m_BusFraction;
	}

	void AudioMixerPanel::LoadState(const Yaml::Node& state)
	{
		const std::string page = state["Page"].as<std::string>("");
		for (uint32_t index = 0; index < std::size(k_Pages); ++index)
			if (page == k_Pages[index])
			{
				if (m_Page != index)
					StopPreview();
				m_Page = index;
				m_OpenPage = true;
			}
		const float fraction = state["BusFraction"].as<float>(m_BusFraction);
		if (std::isfinite(fraction))
			m_BusFraction = std::clamp(fraction, 0.1f, 0.8f);
	}

	void AudioMixerPanel::OnImGuiRender(bool& isOpen)
	{
		m_IsFocused = false;
		if (!isOpen)
			return;
		ImGui::SetNextWindowSize({760.0f, 720.0f}, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Audio Mixer", &isOpen))
		{
			ImGui::End();
			return;
		}
		m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		AudioMixerSettings* mixer = GetMixer();
		if (!mixer)
		{
			ImGuiEx::PanelEmptyState("##mixer_project", "No project open",
									 "Open a project to edit its audio mixer.");
			ImGui::End();
			return;
		}

		UI_Toolbar();
		if (!m_Error.empty())
			ImGuiEx::PanelMessage("Audio Mixer", m_Error.c_str(), true);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##mixer_page", k_Pages[m_Page]))
		{
			for (uint32_t page = 0; page < std::size(k_Pages); ++page)
			{
				const bool selected = page == m_Page;
				if (ImGui::Selectable(k_Pages[page], selected))
				{
					if (m_Page != page)
						StopPreview();
					m_Page = page;
					m_OpenPage = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ImGui::BeginChild("##mixer_content"))
		{
			if (m_Page == 0)
			{
				const float font = ImGui::GetFontSize();
				const auto split = ImGuiEx::PanelSplitWidths(ImGui::GetContentRegionAvail().x, m_BusFraction,
															 font * 24.0f, font * 24.0f);
				if (split.SideBySide)
				{
					if (ImGui::BeginChild("##mixer_bus_list", {split.First, 0.0f}))
						UI_Buses(*mixer);
					ImGui::EndChild();
					ImGui::SameLine();
					ImGuiEx::PanelSplitHandle("##mixer_split", split, m_BusFraction);
					ImGui::SameLine();
					if (ImGui::BeginChild("##mixer_bus_details"))
						UI_SelectedBus(*mixer);
					ImGui::EndChild();
				}
				else if (m_NarrowDetails)
				{
					const ImGuiEx::PanelAction actions[] = {
						{0, "All buses", LUX_ICON_ARROW_LEFT, nullptr, true, false, true}};
					if (ImGuiEx::PanelActionBar("##mixer_back", actions) == 0)
						m_NarrowDetails = false;
					UI_SelectedBus(*mixer);
				}
				else
					UI_Buses(*mixer);
			}
			else
			{
				// A selected page must expose its first section even if that section used to be
				// collapsed in the old all-sections view.
				if (m_OpenPage)
				{
					ImGui::SetScrollY(0.0f);
					ImGui::SetNextItemOpen(true);
					m_OpenPage = false;
				}
				switch (m_Page)
				{
				case 1:
					UI_Reverb(*mixer);
					break;
				case 2:
					UI_Snapshots(*mixer);
					break;
				case 3:
					UI_VCAs(*mixer);
					break;
				case 4:
					UI_SceneBanks();
					break;
				}
			}
		}
		ImGui::EndChild();
		CommitEdits();
		ImGui::End();
	}

	void AudioMixerPanel::UI_SceneBanks()
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Scene Sound Banks"))
			return;
		if (!m_Context)
		{
			ImGuiEx::PanelMessage("No scene open", "Open a scene to choose the banks it loads.");
			return;
		}
		ImGui::TextWrapped("Loaded before this scene plays and unloaded when it stops. Resident banks load in every scene on their own.");
		std::vector<AssetHandle> banks = m_Context->GetSoundBanks();
		bool changed = false;
		std::optional<size_t> removed;
		ImGui::BeginDisabled(m_Context->IsRunning());
		for (size_t index = 0; index < banks.size(); ++index)
		{
			ImGuiEx::ScopedID id(static_cast<int>(index));
			AssetHandle handle = banks[index];
			const std::string label = "Bank " + std::to_string(index + 1);
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 5.0f, ImGui::GetFontSize() * 24.0f);
			if (ImGuiEx::PropertyAssetReference<SoundBank>(label.c_str(), handle, "", nullptr, {}, false) &&
				handle && std::find(banks.begin(), banks.end(), handle) == banks.end())
			{
				banks[index] = handle;
				changed = true;
			}
			ImGuiEx::EndPropertyGrid();
			const ImGuiEx::PanelAction actions[] = {{0, "Remove bank", LUX_ICON_TIMES}};
			if (ImGuiEx::PanelActionBar("##scene_bank_actions", actions) == 0)
				removed = index;
		}
		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 5.0f, ImGui::GetFontSize() * 24.0f);
		AssetHandle added = 0;
		if (ImGuiEx::PropertyAssetReference<SoundBank>("Add Bank", added, "Drop a Sound Bank here from the Content Browser, or pick one.", nullptr, {}, false)
			&& added && std::find(banks.begin(), banks.end(), added) == banks.end())
		{
			banks.push_back(added);
			changed = true;
		}
		ImGuiEx::EndPropertyGrid();
		ImGui::EndDisabled();
		if (removed)
		{
			banks.erase(banks.begin() + static_cast<std::ptrdiff_t>(*removed));
			changed = true;
		}
		if (changed)
		{
			m_Context->SetSoundBanks(std::move(banks));
			EditorStack::Get().MarkSceneEdited("Scene Sound Banks");
		}
	}

	bool AudioMixerPanel::SaveProject()
	{
		CommitEdits(true);
		if (!m_Project)
			return false;
		if (!m_Project->GetConfig().Audio.Mixer.Validate(&m_Error))
			return false;
		if (!Project::SaveActive(m_Project->GetProjectFilePath()))
		{
			m_Error = "Project save failed. Mixer edits are retained; see Log and retry Save project.";
			return false;
		}
		m_Dirty = false;
		m_Error.clear();
		return true;
	}

	bool AudioMixerPanel::ApplyChanges()
	{
		if (!m_NeedsRestart || (m_Context && m_Context->IsRunning()))
			return false;
		if (!GetMixer() || !GetMixer()->Validate(&m_Error))
			return false;
		StopPreview();
		AudioEngine::Shutdown();
		AudioEngine::Init();
		if (!AudioEngine::HasInitializedEngine())
		{
			m_Error = "Audio engine restart failed. Mixer edits are retained; see Log and retry Apply.";
			return false;
		}
		m_NeedsRestart = false;
		m_Error.clear();
		return true;
	}

	void AudioMixerPanel::UI_Toolbar()
	{
		const bool playing = m_Context && m_Context->IsRunning();
		const auto* mixer = GetMixer();
		const bool canSave = m_Dirty || (mixer && *mixer != m_Committed);
		const ImGuiEx::PanelAction actions[] = {
			{0, "Save project", LUX_ICON_FLOPPY_O, nullptr, canSave, false, true},
			{1, "Apply", LUX_ICON_REFRESH, nullptr, m_NeedsRestart && !playing}};
		switch (ImGuiEx::PanelActionBar("##mixer_actions", actions))
		{
		case 0:
			SaveProject();
			break;
		case 1:
			ApplyChanges();
			break;
		default:
			break;
		}
		if (m_NeedsRestart)
			ImGui::TextWrapped("Apply restarts the audio engine to update layout, effects and snapshots.%s",
							   playing ? " Stop Play before applying." : "");
	}

	void AudioMixerPanel::UI_Buses(AudioMixerSettings& mixer)
	{
		if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 28.0f)
		{
			if (ImGuiEx::PanelListItem("##master", "Master", m_Selected == kMaster))
			{
				m_Selected = kMaster;
				m_NarrowDetails = true;
			}
			ImGui::TextWrapped("Select a bus to edit its name, children and effects.");
			for (auto& bus : mixer.Buses)
			{
				ImGuiEx::ScopedID id(bus.Path.c_str());
				if (ImGuiEx::PanelListItem("##select", bus.Path, m_Selected == bus.Path))
				{
					m_Selected = bus.Path;
					m_NarrowDetails = true;
				}
				ImGui::TextWrapped("Volume (linear)");
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGuiEx::SliderFloat("##volume", &bus.Volume, 0.0f, 4.0f, "%.2f",
										 ImGuiSliderFlags_AlwaysClamp, AudioMixerBus{}.Volume))
					ApplyLive(bus);
				const float decibels = ToDecibels(bus.Volume);
				if (std::isfinite(decibels))
					ImGui::TextWrapped("%+.1f dB", decibels);
				else
					ImGui::TextWrapped("-inf dB");
				if (ImGui::Checkbox("Mute", &bus.Muted))
					ApplyLive(bus);
				ImGui::Separator();
			}
			return;
		}
		if (!ImGui::BeginTable("##mixer_buses", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
			return;
		ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("dB", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
		ImGui::TableSetupColumn("Mute", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.0f);
		ImGui::TableHeadersRow();

		// The master is the engine output; its volume belongs to the game (Audio.SetBusVolume), not the mix.
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		if (ImGuiEx::PanelListItem("###bus:/", "Master", m_Selected == kMaster))
		{
			m_Selected = kMaster;
			m_NarrowDetails = true;
		}
		for (AudioMixerBus& bus : mixer.Buses)
		{
			ImGui::PushID(bus.Path.c_str());
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			const float indent = std::min(ImGui::GetFontSize() * static_cast<float>(Depth(bus.Path)),
										  ImGui::GetContentRegionAvail().x * 0.3f);
			ImGui::Indent(indent);
			if (ImGuiEx::PanelListItem("##select", Name(bus.Path), m_Selected == bus.Path, bus.Path))
			{
				m_Selected = bus.Path;
				m_NarrowDetails = true;
			}
			ImGui::Unindent(indent);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGuiEx::SliderFloat("##volume", &bus.Volume, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp, AudioMixerBus{}.Volume))
				ApplyLive(bus);
			ImGui::TableNextColumn();
			const float decibels = ToDecibels(bus.Volume);
			ImGui::TextUnformatted(std::isfinite(decibels) ? std::format("{:+.1f}", decibels).c_str() : "-inf");
			ImGui::TableNextColumn();
			if (ImGui::Checkbox("##mute", &bus.Muted))
				ApplyLive(bus);
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	void AudioMixerPanel::UI_SelectedBus(AudioMixerSettings& mixer)
	{
		const auto selected = std::find_if(mixer.Buses.begin(), mixer.Buses.end(), [&](const AudioMixerBus& bus) { return bus.Path == m_Selected; });
		if (m_Selected != kMaster && selected == mixer.Buses.end())
			m_Selected = kMaster;

		ImGui::TextWrapped("Selected: %s", m_Selected.c_str());
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##bus_name", "Bus name", m_NameBuffer, sizeof(m_NameBuffer));
		const std::string name = m_NameBuffer;
		const bool validName = !name.empty() && name.find('/') == std::string::npos;

		// Each change is checked whole before it replaces the mix, so an invalid layout never lands.
		const auto tryApply = [&](AudioMixerSettings candidate)
		{
			if (!candidate.Validate(&m_Error))
				return false;
			m_Error.clear();
			mixer = std::move(candidate);
			return true;
		};

		const ImGuiEx::PanelAction actions[] = {
			{0, "Add child", LUX_ICON_PLUS, nullptr, validName},
			{1, "Rename", LUX_ICON_PENCIL, nullptr, validName && m_Selected != kMaster},
			{2, "Remove subtree", LUX_ICON_TRASH, nullptr, m_Selected != kMaster}};
		const int action = ImGuiEx::PanelActionBar("##bus_actions", actions);
		if (action == 0)
		{
			AudioMixerSettings candidate = mixer;
			candidate.Buses.push_back({ ChildPath(m_Selected, name) });
			if (tryApply(std::move(candidate)))
			{
				m_Selected = ChildPath(m_Selected, name);
				m_NameBuffer[0] = '\0';
			}
		}
		else if (action == 1)
		{
			// The bus and every bus below it move to the new name.
			AudioMixerSettings candidate = mixer;
			const std::string renamed = ChildPath(AudioMixerSettings::ParentPath(m_Selected), name);
			const auto rename = [&](AudioMixerBus& bus)
			{
				if (IsInside(bus.Path, m_Selected))
					bus.Path = renamed + bus.Path.substr(m_Selected.size());
			};
			std::for_each(candidate.Buses.begin(), candidate.Buses.end(), rename);
			for (AudioMixerSnapshot& snapshot : candidate.Snapshots)
				std::for_each(snapshot.Buses.begin(), snapshot.Buses.end(), rename);
			for (AudioMixerVCA& vca : candidate.VCAs)
				for (std::string& path : vca.Buses)
					if (IsInside(path, m_Selected))
						path = renamed + path.substr(m_Selected.size());
			if (tryApply(std::move(candidate)))
			{
				m_Selected = renamed;
				m_NameBuffer[0] = '\0';
			}
		}
		else if (action == 2)
		{
			AudioMixerSettings candidate = mixer;
			const auto removed = [&](const AudioMixerBus& bus) { return IsInside(bus.Path, m_Selected); };
			std::erase_if(candidate.Buses, removed);
			for (AudioMixerSnapshot& snapshot : candidate.Snapshots)
				std::erase_if(snapshot.Buses, removed);
			for (AudioMixerVCA& vca : candidate.VCAs)
				std::erase_if(vca.Buses, [&](const std::string& path) { return IsInside(path, m_Selected); });
			if (tryApply(std::move(candidate)))
				m_Selected = kMaster;
		}

		// The selected bus's effect chain, in order after its volume.
		const auto bus = std::find_if(mixer.Buses.begin(), mixer.Buses.end(), [&](const AudioMixerBus& entry) { return entry.Path == m_Selected; });
		if (bus == mixer.Buses.end())
			return;
		ImGui::Spacing();
		ImGui::TextWrapped("Effects apply from top to bottom. Changes need Apply.");
		auto& effects = bus->Effects;
		std::optional<size_t> moveUp, remove;
		for (size_t index = 0; index < effects.size(); ++index)
		{
			AudioMixerEffect& effect = effects[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::Separator();
			ImGui::TextWrapped("%s", AudioMixerSettings::EffectName(effect.Type));
			const ImGuiEx::PanelAction effectActions[] = {
				{0, "Bypass", nullptr, nullptr, true, effect.Bypass},
				{1, "Up", LUX_ICON_ARROW_UP, nullptr, index > 0},
				{2, "Down", LUX_ICON_ARROW_DOWN, nullptr, index + 1 < effects.size()},
				{3, "Remove", LUX_ICON_TIMES}};
			switch (ImGuiEx::PanelActionBar("##effect_actions", effectActions))
			{
			case 0:
				effect.Bypass = !effect.Bypass;
				break;
			case 1:
				moveUp = index;
				break;
			case 2:
				moveUp = index + 1;
				break;
			case 3:
				remove = index;
				break;
			default:
				break;
			}
			const auto slider = [](const char* label, float* value, float minimum, float maximum,
								   const char* format, ImGuiSliderFlags flags, float defaultValue)
			{
				ImGuiEx::ScopedID field(label);
				ImGui::TextWrapped("%s", label);
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGuiEx::SliderFloat("##value", value, minimum, maximum, format, flags, defaultValue);
			};
			ImGui::BeginDisabled(effect.Bypass);
			switch (effect.Type)
			{
				case AudioMixerEffectType::LowPass:
				case AudioMixerEffectType::HighPass:
					slider("Cutoff (Hz)", &effect.FrequencyHz, 20.0f, 20000.0f, "%.0f",
						   ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.FrequencyHz);
					break;
				case AudioMixerEffectType::PeakingEQ:
					slider("Frequency (Hz)", &effect.FrequencyHz, 20.0f, 20000.0f, "%.0f",
						   ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.FrequencyHz);
					slider("Gain (dB)", &effect.GainDb, -24.0f, 24.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.GainDb);
					slider("Q", &effect.Q, 0.1f, 10.0f, "%.2f",
						   ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp, AudioMixerEffect{}.Q);
					break;
				case AudioMixerEffectType::Delay:
					slider("Time (s)", &effect.DelaySeconds, 0.001f, 2.0f, "%.3f",
						   ImGuiSliderFlags_AlwaysClamp, AudioMixerEffect{}.DelaySeconds);
					slider("Feedback", &effect.Feedback, 0.0f, 0.95f, "%.2f", ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.Feedback);
					slider("Wet", &effect.Wet, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.Wet);
					break;
				case AudioMixerEffectType::Compressor:
					slider("Threshold (dB)", &effect.ThresholdDb, -60.0f, 0.0f, "%.1f",
						   ImGuiSliderFlags_AlwaysClamp, AudioMixerEffect{}.ThresholdDb);
					slider("Ratio", &effect.Ratio, 1.0f, 20.0f, "%.1f:1", ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.Ratio);
					slider("Attack (ms)", &effect.AttackMs, 0.1f, 500.0f, "%.1f",
						   ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.AttackMs);
					slider("Release (ms)", &effect.ReleaseMs, 1.0f, 5000.0f, "%.0f",
						   ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.ReleaseMs);
					slider("Makeup (dB)", &effect.MakeupDb, 0.0f, 24.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp,
						   AudioMixerEffect{}.MakeupDb);
					break;
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		// Applied after the loop, so no widget above refers to a moved or erased element.
		if (moveUp)
			std::swap(effects[*moveUp - 1], effects[*moveUp]);
		if (remove)
			effects.erase(effects.begin() + static_cast<std::ptrdiff_t>(*remove));

		static const char* s_EffectTypes[] = { "Low-pass", "High-pass", "Peaking EQ", "Delay", "Compressor" };
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::Combo("##effect_type", &m_NewEffectType, s_EffectTypes, IM_ARRAYSIZE(s_EffectTypes));
		const ImGuiEx::PanelAction addEffect[] = {
			{0, "Add effect", LUX_ICON_PLUS, nullptr, effects.size() < AudioMixerSettings::kMaxEffects}};
		if (ImGuiEx::PanelActionBar("##add_effect", addEffect) == 0)
		{
			AudioMixerEffect effect;
			effect.Type = static_cast<AudioMixerEffectType>(m_NewEffectType);
			effects.push_back(effect);
		}
	}

	void AudioMixerPanel::UI_Reverb(AudioMixerSettings& mixer)
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Reverb Return", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		ImGui::TextWrapped("Every voice sends Soundwave's ReverbSend, plus its Sound's Send curves, to this reverb. It plays into the master.");
		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 12.0f, ImGui::GetFontSize() * 24.0f);
		ImGuiEx::PropertySlider("Room Size", mixer.Reverb.RoomSize, 0.0f, 1.0f, "Larger rooms decay longer.", false, AudioReverbReturnSettings{}.RoomSize);
		ImGuiEx::PropertySlider("Damping", mixer.Reverb.Damping, 0.0f, 1.0f, "How fast the highs die away.", false, AudioReverbReturnSettings{}.Damping);
		ImGuiEx::PropertySlider("Width", mixer.Reverb.Width, 0.0f, 1.0f, "Stereo spread of the tail.", false, AudioReverbReturnSettings{}.Width);
		ImGuiEx::PropertySlider("Return Volume", mixer.Reverb.Volume, 0.0f, 4.0f, "The return's level into the master.", false, AudioReverbReturnSettings{}.Volume);
		ImGuiEx::EndPropertyGrid();
	}

	void AudioMixerPanel::UI_Snapshots(AudioMixerSettings& mixer)
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Snapshots", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		ImGui::TextWrapped("A zone or script blends a snapshot in by intensity (0-1): each listed bus moves from its own volume toward the snapshot's. Where active snapshots list the same bus, the higher priority wins.");

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##snapshot_name", "Snapshot name", m_SnapshotNameBuffer, sizeof(m_SnapshotNameBuffer));
		const std::string name = m_SnapshotNameBuffer;
		const bool hasSelection =
			std::any_of(mixer.Snapshots.begin(), mixer.Snapshots.end(),
						[&](const AudioMixerSnapshot& entry) { return entry.ID == m_SelectedSnapshot; });
		const auto tryApply = [&](AudioMixerSettings candidate)
		{
			if (!candidate.Validate(&m_Error))
				return false;
			m_Error.clear();
			mixer = std::move(candidate);
			return true;
		};
		const ImGuiEx::PanelAction actions[] = {
			{0, "Add snapshot", LUX_ICON_PLUS, nullptr, !name.empty()},
			{1, "Rename", LUX_ICON_PENCIL, nullptr, !name.empty() && hasSelection},
			{2, "Remove snapshot", LUX_ICON_TIMES, nullptr, hasSelection}};
		const int action = ImGuiEx::PanelActionBar("##snapshot_actions", actions);
		if (action == 0)
		{
			AudioMixerSettings candidate = mixer;
			AudioMixerSnapshot snapshot;
			snapshot.ID = UUID();
			snapshot.Name = name;
			candidate.Snapshots.push_back(snapshot);
			if (tryApply(std::move(candidate)))
			{
				m_SelectedSnapshot = snapshot.ID;
				m_SnapshotNameBuffer[0] = '\0';
			}
		}
		else if (action == 1)
		{
			AudioMixerSettings candidate = mixer;
			for (AudioMixerSnapshot& entry : candidate.Snapshots)
				if (entry.ID == m_SelectedSnapshot)
					entry.Name = name;
			if (tryApply(std::move(candidate)))
				m_SnapshotNameBuffer[0] = '\0';
		}
		else if (action == 2)
		{
			StopPreview();
			AudioMixerSettings candidate = mixer;
			std::erase_if(candidate.Snapshots, [&](const AudioMixerSnapshot& entry) { return entry.ID == m_SelectedSnapshot; });
			if (tryApply(std::move(candidate)))
				m_SelectedSnapshot = 0;
		}

		for (const AudioMixerSnapshot& entry : mixer.Snapshots)
		{
			ImGuiEx::ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(entry.ID)));
			if (ImGuiEx::PanelListItem("##snapshot", entry.Name, entry.ID == m_SelectedSnapshot) &&
				entry.ID != m_SelectedSnapshot)
			{
				StopPreview();
				m_SelectedSnapshot = entry.ID;
			}
		}
		auto selected =
			std::find_if(mixer.Snapshots.begin(), mixer.Snapshots.end(),
						 [&](const AudioMixerSnapshot& entry) { return entry.ID == m_SelectedSnapshot; });
		if (selected == mixer.Snapshots.end())
		{
			return;
		}

		AudioMixerSnapshot& snapshot = *selected;
		ImGui::Separator();
		ImGui::TextWrapped("Priority");
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputInt("##snapshot_priority", &snapshot.Priority);
		if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 28.0f)
		{
			for (const auto& bus : mixer.Buses)
			{
				ImGuiEx::ScopedID id(bus.Path.c_str());
				ImGui::TextWrapped("%s", bus.Path.c_str());
				const auto found =
					std::find_if(snapshot.Buses.begin(), snapshot.Buses.end(),
								 [&](const AudioMixerBus& entry) { return entry.Path == bus.Path; });
				bool overridden = found != snapshot.Buses.end();
				if (ImGui::Checkbox("Override", &overridden))
				{
					if (overridden)
						snapshot.Buses.push_back({bus.Path, bus.Volume});
					else
						snapshot.Buses.erase(found);
				}
				const auto entry =
					std::find_if(snapshot.Buses.begin(), snapshot.Buses.end(),
								 [&](const AudioMixerBus& value) { return value.Path == bus.Path; });
				if (entry != snapshot.Buses.end())
				{
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGuiEx::SliderFloat("##volume", &entry->Volume, 0.0f, 4.0f, "%.2f",
										 ImGuiSliderFlags_AlwaysClamp, AudioMixerBus{}.Volume);
				}
			}
		}
		else if (ImGui::BeginTable("##snapshot_buses", 3,
								   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Override", ImGuiTableColumnFlags_WidthFixed,
									ImGui::GetFontSize() * 5.5f);
			ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableHeadersRow();
			for (const AudioMixerBus& bus : mixer.Buses)
			{
				ImGui::PushID(bus.Path.c_str());
				const auto it = std::find_if(snapshot.Buses.begin(), snapshot.Buses.end(), [&](const AudioMixerBus& entry) { return entry.Path == bus.Path; });
				bool overridden = it != snapshot.Buses.end();
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Checkbox("##override", &overridden))
				{
					if (overridden)
						snapshot.Buses.push_back({ bus.Path, bus.Volume });
					else
						snapshot.Buses.erase(it);
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(bus.Path.c_str());
				ImGui::TableNextColumn();
				const auto entry = std::find_if(snapshot.Buses.begin(), snapshot.Buses.end(), [&](const AudioMixerBus& e) { return e.Path == bus.Path; });
				if (entry != snapshot.Buses.end())
				{
					ImGui::SetNextItemWidth(-1.0f);
					ImGuiEx::SliderFloat("##volume", &entry->Volume, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp, AudioMixerBus{}.Volume);
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		// Preview: plays the snapshot the running engine has (after Apply), live in edit mode or Play.
		const bool canPreview = AudioEngine::HasInitializedEngine() && !m_NeedsRestart;
		ImGui::BeginDisabled(!canPreview);
		ImGui::TextWrapped("Preview intensity");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGuiEx::SliderFloat("##preview_intensity", &m_PreviewIntensity, 0.0f, 1.0f, "%.2f",
								 ImGuiSliderFlags_AlwaysClamp, 0.0f))
		{
			if (m_PreviewIntensity <= 0.0f)
				StopPreview();
			else
			{
				if (!m_Preview || !m_Preview->IsValid())
				{
					m_Preview = AudioEventInstance::Create(std::to_string(static_cast<uint64_t>(snapshot.ID)));
					if (m_Preview && !m_Preview->Start())
						m_Preview = nullptr;
				}
				if (m_Preview)
					m_Preview->SetSnapshotIntensity(m_PreviewIntensity);
			}
		}
		ImGui::EndDisabled();
		if (!canPreview)
			ImGui::SetItemTooltip(m_NeedsRestart ? "Apply first: the running engine has the previous snapshots." : "Previews need the audio engine running.");
	}

	void AudioMixerPanel::UI_VCAs(AudioMixerSettings& mixer)
	{
		if (!ImGuiEx::BoundedCollapsingHeader("VCAs"))
			return;
		ImGui::TextWrapped("A VCA scales a group of buses; game code sets its volume with Audio.SetVCAVolume (\"vca:/Name\"). It starts at 1.");
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##vca_name", "VCA name", m_VCANameBuffer, sizeof(m_VCANameBuffer));
		const std::string name = m_VCANameBuffer;
		const ImGuiEx::PanelAction actions[] = {
			{0, "Add VCA", LUX_ICON_PLUS, nullptr, !name.empty() && name.find('/') == std::string::npos}};
		if (ImGuiEx::PanelActionBar("##vca_actions", actions) == 0)
		{
			AudioMixerSettings candidate = mixer;
			candidate.VCAs.push_back({ "vca:/" + name });
			if (candidate.Validate(&m_Error))
			{
				m_Error.clear();
				mixer = std::move(candidate);
				m_VCANameBuffer[0] = '\0';
			}
		}
		std::optional<size_t> remove;
		for (size_t index = 0; index < mixer.VCAs.size(); ++index)
		{
			AudioMixerVCA& vca = mixer.VCAs[index];
			ImGuiEx::ScopedID vcaID(vca.Path.c_str());
			ImGui::Separator();
			ImGui::TextWrapped("%s", vca.Path.c_str());
			const ImGuiEx::PanelAction removeAction[] = {{0, "Remove VCA", LUX_ICON_TIMES}};
			if (ImGuiEx::PanelActionBar("##remove_vca", removeAction) == 0)
				remove = index;
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 12.0f, ImGui::GetFontSize() * 24.0f);
			for (const AudioMixerBus& bus : mixer.Buses)
			{
				bool assigned = std::find(vca.Buses.begin(), vca.Buses.end(), bus.Path) != vca.Buses.end();
				if (ImGuiEx::Property(bus.Path.c_str(), assigned, nullptr, false))
				{
					if (assigned)
						vca.Buses.push_back(bus.Path);
					else
						std::erase(vca.Buses, bus.Path);
				}
			}
			ImGuiEx::EndPropertyGrid();
		}
		if (remove)
			mixer.VCAs.erase(mixer.VCAs.begin() + static_cast<std::ptrdiff_t>(*remove));
	}
}
