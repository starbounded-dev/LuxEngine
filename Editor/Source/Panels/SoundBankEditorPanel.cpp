// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SoundBankEditorPanel.h"
#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/Sound.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Project/Project.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <unordered_set>

namespace Lux
{
	namespace
	{
		std::string AssetPath(AssetHandle handle)
		{
			const auto metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
			return metadata.IsValid() ? metadata.FilePath.generic_string() : "Missing asset " + std::to_string(static_cast<uint64_t>(handle));
		}

		// Writes the loaded bank to its file; the serializer refuses an invalid one.
		void Save(AssetHandle handle, const SoundBank& state)
		{
			if (Ref<SoundBank> bank = AssetManager::GetAsset<SoundBank>(handle))
			{
				bank->Sounds = state.Sounds;
				bank->Resident = state.Resident;
				AssetImporter::Serialize(bank.As<Asset>());
			}
		}
	}

	void SoundBankEditorPanel::Commit(const SoundBank& next, const char* label)
	{
		const Ref<SoundBank> bank = AssetManager::GetAsset<SoundBank>(m_Bank);
		if (!bank || (bank->Sounds == next.Sounds && bank->Resident == next.Resident))
			return;
		if (next.ToYAML().size() > SoundBank::MaxFileBytes)
		{
			LUX_CORE_ERROR_TAG("Editor", "Sound Bank Editor: '{}' would be larger than the 64 KiB a bank file may be", AssetPath(m_Bank));
			return;
		}
		SoundBank before;
		before.Sounds = bank->Sounds;
		before.Resident = bank->Resident;
		SoundBank after;
		after.Sounds = next.Sounds;
		after.Resident = next.Resident;
		Save(m_Bank, after);
		if (m_PushUndo)
		{
			const AssetHandle handle = m_Bank;
			m_PushUndo(label, [handle, before] { Save(handle, before); }, [handle, after] { Save(handle, after); });
		}
	}

	void SoundBankEditorPanel::OnImGuiRender(bool& isOpen)
	{
		if (m_RequestFocus)
		{
			ImGui::SetNextWindowFocus();
			m_RequestFocus = false;
		}
		ImGuiEx::ScopedStyle padding(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
		ImGui::SetNextWindowSize(ImVec2(760.0f, 540.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Sound Bank Editor", &isOpen))
		{
			ImGui::End();
			return;
		}
		const Ref<SoundBank> bank = m_Bank && Project::GetEditorAssetManager() && AssetManager::GetAssetType(m_Bank) == AssetType::SoundBank
			? AssetManager::GetAsset<SoundBank>(m_Bank) : nullptr;
		if (!bank)
		{
			ImGui::TextDisabled("Double-click a Sound Bank in the Content Browser to edit it.");
			ImGui::End();
			return;
		}
		// Keep the list readable when this tool is maximized on a wide display.
		const float contentWidth = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 48.0f);
		if (!ImGui::BeginChild("##bank_content", ImVec2(contentWidth, 0.0f)))
		{
			ImGui::EndChild();
			ImGui::End();
			return;
		}
		const std::string path = AssetPath(m_Bank);
		ImGui::TextUnformatted(std::filesystem::path(path).stem().string().c_str());
		ImGui::TextDisabled("%s", path.c_str());
		ImGui::TextDisabled("Saves automatically  |  Ctrl+Z to undo");
		ImGui::Spacing();
		ImGui::TextWrapped("The Sounds below load together: when a scene that lists this bank starts, or when a script calls Audio.LoadBank.");

		SoundBank next;
		next.Sounds = bank->Sounds;
		next.Resident = bank->Resident;
		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 7.0f);
		if (ImGuiEx::Property("Resident", next.Resident, "Loaded at game startup and kept in memory for every scene.", false))
			Commit(next, "Sound Bank: Resident");
		ImGuiEx::EndPropertyGrid();

		ImGui::Separator();
		UI_Sounds(*bank);
		ImGui::Spacing();
		UI_Memory(*bank);
		ImGui::Spacing();
		UI_Unbanked(*bank);
		ImGui::EndChild();
		ImGui::End();
	}

	void SoundBankEditorPanel::UI_Sounds(const SoundBank& bank)
	{
		ImGui::Text("Sounds (%d)", static_cast<int>(bank.Sounds.size()));
		SoundBank next;
		next.Sounds = bank.Sounds;
		next.Resident = bank.Resident;
		int removed = -1;
		bool replaced = false;
		const float labelWidth = std::min(ImGui::GetFontSize() * 7.0f, ImGui::GetContentRegionAvail().x * 0.3f);
		ImGuiEx::BeginPropertyGrid();
		ImGui::SetColumnWidth(0, labelWidth);
		const float removeWidth = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGuiEx::PropertyAssetReferenceSettings rowSettings;
		rowSettings.AdvanceToNextColumn = false;
		rowSettings.WidthOffset = removeWidth + ImGui::GetStyle().ItemSpacing.x;
		for (size_t index = 0; index < next.Sounds.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			AssetHandle handle = next.Sounds[index];
			const std::string label = "Sound " + std::to_string(index + 1);
			if (ImGuiEx::PropertyAssetReference<Sound>(label.c_str(), handle, "", nullptr, rowSettings, false) && handle && !next.Contains(handle))
			{
				next.Sounds[index] = handle;
				replaced = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove", ImVec2(removeWidth, 28.0f)))
				removed = static_cast<int>(index);
			ImGui::NextColumn();
			ImGui::PopID();
		}
		// An empty slot: drop a Sound on it, or pick one, to add it.
		AssetHandle added = 0;
		if (ImGuiEx::PropertyAssetReference<Sound>("Add Sound", added, "Drop a Sound here from the Content Browser, or pick one.", nullptr, {}, false) && added)
			replaced |= next.Add(added);
		ImGuiEx::EndPropertyGrid();
		if (removed >= 0)
		{
			next.Sounds.erase(next.Sounds.begin() + removed);
			replaced = true;
		}
		if (replaced)
			Commit(next, "Sound Bank: Sounds");
	}

	void SoundBankEditorPanel::UI_Memory(const SoundBank& bank)
	{
		// The clips' decoded size (float samples), each clip once; streamed Sounds hold only stream pages.
		std::unordered_set<uint64_t> clips;
		double decodedBytes = 0.0;
		int streamed = 0;
		for (const AssetHandle handle : bank.Sounds)
		{
			const Ref<Sound> sound = AssetManager::GetAssetType(handle) == AssetType::Sound ? AssetManager::GetAsset<Sound>(handle) : nullptr;
			if (!sound)
				continue;
			if (sound->Stream)
			{
				++streamed;
				continue;
			}
			for (const AssetHandle clip : sound->GetClips())
			{
				if (!clips.insert(static_cast<uint64_t>(clip)).second || AssetManager::GetAssetType(clip) != AssetType::Audio)
					continue;
				if (const Ref<AudioFile> file = AssetManager::GetAsset<AudioFile>(clip))
					decodedBytes += file->Duration * file->SamplingRate * file->NumChannels * sizeof(float);
			}
		}
		ImGui::Text("Memory when loaded: about %.2f MiB decoded", decodedBytes / (1024.0 * 1024.0));
		if (streamed)
		{
			ImGui::TextDisabled("%d streamed Sound%s (stream pages only)", streamed, streamed == 1 ? "" : "s");
		}
	}

	void SoundBankEditorPanel::UI_Unbanked(const SoundBank& bank)
	{
		if (!ImGui::TreeNode("Sounds in no bank"))
			return;
		std::unordered_set<uint64_t> banked;
		for (const AssetHandle handle : AssetManager::GetAllAssetsWithType<SoundBank>())
			if (const Ref<SoundBank> other = AssetManager::GetAsset<SoundBank>(handle))
				for (const AssetHandle sound : other->Sounds)
					banked.insert(static_cast<uint64_t>(sound));
		std::vector<std::pair<std::string, AssetHandle>> unbanked;
		for (const AssetHandle sound : AssetManager::GetAllAssetsWithType<Sound>())
			if (!banked.contains(static_cast<uint64_t>(sound)))
				unbanked.emplace_back(AssetPath(sound), sound);
		std::sort(unbanked.begin(), unbanked.end());
		if (unbanked.empty())
			ImGui::TextDisabled("Every Sound is in a bank.");
		else
			ImGui::TextWrapped("These load when they first play, which can stall that frame. Add each to the bank of the scene that uses it.");
		for (const auto& [path, sound] : unbanked)
		{
			ImGui::PushID(path.c_str());
			if (ImGui::SmallButton("Add"))
			{
				SoundBank next;
				next.Sounds = bank.Sounds;
				next.Resident = bank.Resident;
				if (next.Add(sound))
					Commit(next, "Sound Bank: Sounds");
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(path.c_str());
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
}
