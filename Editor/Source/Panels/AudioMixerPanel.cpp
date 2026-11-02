// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioMixerPanel.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/Scene.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <format>

namespace Lux
{
	namespace {
		constexpr const char* kMaster = "bus:/";

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
		m_Error.clear();
		m_Dirty = m_NeedsRestart = false;
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
		// The FMOD build mixes in Studio; writing its buses from here would change the authored mix.
		if (AudioEngine::GetBackend() != AudioBackend::Native || !AudioEngine::HasInitializedEngine())
			return;
		AudioEngine::SetBusVolume(bus.Path, bus.Volume);
		AudioEngine::SetBusMuted(bus.Path, bus.Muted);
	}

	void AudioMixerPanel::SetMixer(const AudioMixerSettings& mixer)
	{
		AudioMixerSettings* current = GetMixer();
		if (!current)
			return;
		std::vector<std::string> before;
		for (const auto& bus : current->Buses)
			before.push_back(bus.Path);
		// The engine copies reverb settings and snapshots at Init, so changing them needs Apply.
		const bool reverbChanged = !(current->Reverb == mixer.Reverb) || current->Snapshots != mixer.Snapshots;
		*current = mixer;
		m_Committed = mixer;
		m_Dirty = true;
		std::vector<std::string> after;
		for (const auto& bus : mixer.Buses)
		{
			after.push_back(bus.Path);
			ApplyLive(bus);
		}
		m_NeedsRestart |= before != after || reverbChanged;
	}

	void AudioMixerPanel::CommitEdits()
	{
		AudioMixerSettings* mixer = GetMixer();
		if (!mixer || ImGui::IsAnyItemActive() || *mixer == m_Committed)
			return;
		const AudioMixerSettings before = m_Committed, after = *mixer;
		SetMixer(after);
		if (m_PushUndo)
			m_PushUndo("Edit Audio Mixer", [this, before]() { SetMixer(before); }, [this, after]() { SetMixer(after); });
	}

	void AudioMixerPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Audio Mixer", &isOpen))
		{
			ImGui::End();
			return;
		}
		AudioMixerSettings* mixer = GetMixer();
		if (!mixer)
		{
			ImGui::TextDisabled("Open a project to edit its mixer.");
			ImGui::End();
			return;
		}
		if (AudioEngine::GetBackend() != AudioBackend::Native)
			ImGui::TextWrapped("This build plays audio through FMOD, which mixes in FMOD Studio. This mixer is used by the native backend (--audio-backend=native).");

		UI_Toolbar(*mixer);
		ImGui::Separator();
		UI_Buses(*mixer);
		ImGui::Spacing();
		UI_SelectedBus(*mixer);
		ImGui::Spacing();
		UI_Reverb(*mixer);
		ImGui::Spacing();
		UI_Snapshots(*mixer);
		CommitEdits();
		ImGui::End();
	}

	void AudioMixerPanel::UI_Toolbar(AudioMixerSettings& mixer)
	{
		ImGui::BeginDisabled(!m_Dirty);
		if (ImGui::Button("Save Project") && Project::SaveActive(m_Project->GetProjectFilePath()))
			m_Dirty = false;
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool playing = m_Context && m_Context->IsRunning();
		const bool canApply = m_NeedsRestart && !playing && AudioEngine::GetBackend() == AudioBackend::Native;
		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button("Apply"))
		{
			// The native engine builds its buses at Init. Restarting releases every voice (their
			// wrappers stay safely invalid, as after a project switch).
			StopPreview();
			AudioEngine::Shutdown();
			AudioEngine::Init();
			m_NeedsRestart = false;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(playing ? "Stop Play to restart the audio engine with the new buses." :
			"Restarts the audio engine so added, renamed or removed buses and reverb settings take effect.");
		if (m_NeedsRestart)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Bus layout changed: apply to hear it.");
		}
		(void)mixer;
	}

	void AudioMixerPanel::UI_Buses(AudioMixerSettings& mixer)
	{
		if (!ImGui::BeginTable("##mixer_buses", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
			return;
		ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("dB", ImGuiTableColumnFlags_WidthFixed, 56.0f);
		ImGui::TableSetupColumn("Mute", ImGuiTableColumnFlags_WidthFixed, 40.0f);
		ImGui::TableHeadersRow();

		// The master is the engine output; its volume belongs to the game (Audio.SetBusVolume), not the mix.
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		if (ImGui::Selectable("Master###bus:/", m_Selected == kMaster, ImGuiSelectableFlags_SpanAllColumns))
			m_Selected = kMaster;
		for (AudioMixerBus& bus : mixer.Buses)
		{
			ImGui::PushID(bus.Path.c_str());
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Indent(14.0f * static_cast<float>(Depth(bus.Path)));
			if (ImGui::Selectable(std::format("{}###select", Name(bus.Path)).c_str(), m_Selected == bus.Path))
				m_Selected = bus.Path;
			ImGui::Unindent(14.0f * static_cast<float>(Depth(bus.Path)));
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderFloat("##volume", &bus.Volume, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
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

		ImGui::TextUnformatted(std::format("Selected: {}", m_Selected).c_str());
		ImGui::SetNextItemWidth(200.0f);
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

		ImGui::SameLine();
		ImGui::BeginDisabled(!validName);
		if (ImGui::Button("Add Child"))
		{
			AudioMixerSettings candidate = mixer;
			candidate.Buses.push_back({ ChildPath(m_Selected, name) });
			if (tryApply(std::move(candidate)))
			{
				m_Selected = ChildPath(m_Selected, name);
				m_NameBuffer[0] = '\0';
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(m_Selected == kMaster);
		if (ImGui::Button("Rename"))
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
			if (tryApply(std::move(candidate)))
			{
				m_Selected = renamed;
				m_NameBuffer[0] = '\0';
			}
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(m_Selected == kMaster);
		if (ImGui::Button("Remove"))
		{
			AudioMixerSettings candidate = mixer;
			const auto removed = [&](const AudioMixerBus& bus) { return IsInside(bus.Path, m_Selected); };
			std::erase_if(candidate.Buses, removed);
			for (AudioMixerSnapshot& snapshot : candidate.Snapshots)
				std::erase_if(snapshot.Buses, removed);
			if (tryApply(std::move(candidate)))
				m_Selected = kMaster;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Removes the bus and every bus below it. Sounds on a removed bus play through the master.");
		if (!m_Error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", m_Error.c_str());
	}

	void AudioMixerPanel::UI_Reverb(AudioMixerSettings& mixer)
	{
		if (!ImGuiEx::PropertyGridHeader("Reverb Return", true))
			return;
		ImGui::TextWrapped("Every voice sends Soundwave's ReverbSend, plus its Sound's Send curves, to this reverb. It plays into the master.");
		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::PropertySlider("Room Size", mixer.Reverb.RoomSize, 0.0f, 1.0f, "Larger rooms decay longer.", false);
		ImGuiEx::PropertySlider("Damping", mixer.Reverb.Damping, 0.0f, 1.0f, "How fast the highs die away.", false);
		ImGuiEx::PropertySlider("Width", mixer.Reverb.Width, 0.0f, 1.0f, "Stereo spread of the tail.", false);
		ImGuiEx::PropertySlider("Return Volume", mixer.Reverb.Volume, 0.0f, 4.0f, "The return's level into the master.", false);
		ImGuiEx::EndPropertyGrid();
		ImGui::TreePop();
	}

	void AudioMixerPanel::UI_Snapshots(AudioMixerSettings& mixer)
	{
		if (!ImGuiEx::PropertyGridHeader("Snapshots", true))
			return;
		ImGui::TextWrapped("A zone or script blends a snapshot in by intensity (0-1): each listed bus moves from its own volume toward the snapshot's. Where active snapshots list the same bus, the higher priority wins.");

		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputTextWithHint("##snapshot_name", "Snapshot name", m_SnapshotNameBuffer, sizeof(m_SnapshotNameBuffer));
		const std::string name = m_SnapshotNameBuffer;
		auto selected = std::find_if(mixer.Snapshots.begin(), mixer.Snapshots.end(), [&](const AudioMixerSnapshot& entry) { return entry.ID == m_SelectedSnapshot; });
		const auto tryApply = [&](AudioMixerSettings candidate)
		{
			if (!candidate.Validate(&m_Error))
				return false;
			m_Error.clear();
			mixer = std::move(candidate);
			return true;
		};
		ImGui::SameLine();
		ImGui::BeginDisabled(name.empty());
		if (ImGui::Button("Add Snapshot"))
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
		ImGui::SameLine();
		ImGui::BeginDisabled(selected == mixer.Snapshots.end());
		if (ImGui::Button("Rename##snapshot"))
		{
			AudioMixerSettings candidate = mixer;
			for (AudioMixerSnapshot& entry : candidate.Snapshots)
				if (entry.ID == m_SelectedSnapshot)
					entry.Name = name;
			if (tryApply(std::move(candidate)))
				m_SnapshotNameBuffer[0] = '\0';
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(selected == mixer.Snapshots.end());
		if (ImGui::Button("Remove##snapshot"))
		{
			StopPreview();
			AudioMixerSettings candidate = mixer;
			std::erase_if(candidate.Snapshots, [&](const AudioMixerSnapshot& entry) { return entry.ID == m_SelectedSnapshot; });
			if (tryApply(std::move(candidate)))
				m_SelectedSnapshot = 0;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Zones that use this snapshot keep its ID and show it as missing.");

		for (const AudioMixerSnapshot& entry : mixer.Snapshots)
		{
			ImGuiEx::ScopedID id(&entry);
			if (ImGui::Selectable(entry.Name.c_str(), entry.ID == m_SelectedSnapshot) && entry.ID != m_SelectedSnapshot)
			{
				StopPreview();
				m_SelectedSnapshot = entry.ID;
			}
		}
		selected = std::find_if(mixer.Snapshots.begin(), mixer.Snapshots.end(), [&](const AudioMixerSnapshot& entry) { return entry.ID == m_SelectedSnapshot; });
		if (selected == mixer.Snapshots.end())
		{
			ImGui::TreePop();
			return;
		}

		AudioMixerSnapshot& snapshot = *selected;
		ImGui::Separator();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::InputInt("Priority", &snapshot.Priority);
		if (ImGui::BeginTable("##snapshot_buses", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Override", ImGuiTableColumnFlags_WidthFixed, 64.0f);
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
					ImGui::SliderFloat("##volume", &entry->Volume, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		// Preview: plays the snapshot the running engine has (after Apply), live in edit mode or Play.
		const bool canPreview = AudioEngine::GetBackend() == AudioBackend::Native && AudioEngine::HasInitializedEngine() && !m_NeedsRestart;
		ImGui::BeginDisabled(!canPreview);
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::SliderFloat("Preview Intensity", &m_PreviewIntensity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
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
			ImGui::SetItemTooltip(m_NeedsRestart ? "Apply first: the running engine has the previous snapshots." : "Previews need the native backend.");
		ImGui::TreePop();
	}
}
