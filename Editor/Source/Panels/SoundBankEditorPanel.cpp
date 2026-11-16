// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SoundBankEditorPanel.h"
#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/Sound.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/StringUtils.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <unordered_set>

namespace Lux
{
	namespace
	{
		std::string AssetPath(AssetHandle handle)
		{
			const auto manager = Project::GetEditorAssetManager();
			const auto metadata = manager ? manager->GetMetadata(handle) : AssetMetadata{};
			return metadata.IsValid() ? Utils::PathToUTF8(metadata.FilePath)
									  : "Missing asset " + std::to_string(static_cast<uint64_t>(handle));
		}

		// Writes the loaded bank to its file; the serializer refuses an invalid one.
		bool Save(AssetHandle handle, const SoundBank& state)
		{
			if (AssetManager::GetAssetType(handle) != AssetType::SoundBank)
			{
				LUX_CORE_ERROR_TAG("Editor", "Cannot save Sound Bank {}: the handle no longer names a bank",
								   static_cast<uint64_t>(handle));
				return false;
			}
			if (Ref<SoundBank> bank = AssetManager::GetAsset<SoundBank>(handle))
			{
				auto candidate = Ref<SoundBank>::Create();
				candidate->Handle = handle;
				candidate->Sounds = state.Sounds;
				candidate->Resident = state.Resident;
				// Publish only after persistence succeeds, including undo/redo. A failed automatic
				// save rejects this atomic bank operation rather than pretending it was applied.
				if (!AssetImporter::TrySerialize(candidate.As<Asset>()))
					return false;
				bank->Sounds = state.Sounds;
				bank->Resident = state.Resident;
				return true;
			}
			LUX_CORE_ERROR_TAG("Editor", "Cannot save Sound Bank {}: the asset is unavailable",
							   static_cast<uint64_t>(handle));
			return false;
		}
	}

	void SoundBankEditorPanel::OpenBank(AssetHandle bank)
	{
		m_Bank = bank;
		m_RequestFocus = true;
		m_MemoryDirty = m_UnbankedDirty = true;
		m_Error.clear();
	}

	void SoundBankEditorPanel::OnProjectChanged(const Ref<Project>&)
	{
		OnClose();
		m_Bank = 0;
		m_Error.clear();
		m_Unbanked.clear();
		m_EstimatedSounds.clear();
		m_MemoryDirty = m_UnbankedDirty = true;
	}

	void SoundBankEditorPanel::OnEvent(Event& event)
	{
		if (!m_IsFocused)
			return;
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(
			[](KeyPressedEvent&) { return ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0; });
	}

	bool SoundBankEditorPanel::Commit(const SoundBank& next, const char* label)
	{
		const Ref<SoundBank> bank = AssetManager::GetAsset<SoundBank>(m_Bank);
		if (!bank || (bank->Sounds == next.Sounds && bank->Resident == next.Resident))
			return false;
		if (next.ToYAML().size() > SoundBank::MaxFileBytes)
		{
			LUX_CORE_ERROR_TAG("Editor", "Sound Bank Editor: '{}' would be larger than the 64 KiB a bank file may be", AssetPath(m_Bank));
			m_Error = "Change not applied: the bank would exceed its 64 KiB limit.";
			return false;
		}
		SoundBank before;
		before.Sounds = bank->Sounds;
		before.Resident = bank->Resident;
		SoundBank after;
		after.Sounds = next.Sounds;
		after.Resident = next.Resident;
		if (!Save(m_Bank, after))
		{
			m_Error = "Change not applied: saving " + AssetPath(m_Bank) +
					  " failed. See Log, then retry the change.";
			return false;
		}
		m_Error.clear();
		m_MemoryDirty = m_UnbankedDirty = true;
		if (m_PushUndo)
		{
			const AssetHandle handle = m_Bank;
			m_PushUndo(label, [handle, before] { Save(handle, before); }, [handle, after] { Save(handle, after); });
		}
		return true;
	}

	void SoundBankEditorPanel::OnImGuiRender(bool& isOpen)
	{
		m_IsFocused = false;
		if (!isOpen)
			return;
		if (m_RequestFocus)
		{
			ImGui::SetNextWindowFocus();
			m_RequestFocus = false;
		}
		ImGui::SetNextWindowSize(ImVec2(760.0f, 540.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Sound Bank Editor", &isOpen))
		{
			ImGui::End();
			return;
		}
		m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const Ref<SoundBank> bank = m_Bank && Project::GetEditorAssetManager() && AssetManager::GetAssetType(m_Bank) == AssetType::SoundBank
			? AssetManager::GetAsset<SoundBank>(m_Bank) : nullptr;
		if (!bank)
		{
			ImGuiEx::PanelEmptyState(
				"##bank_empty", "No Sound Bank open",
				"Double-click a Sound Bank in Content Browser to edit its Sounds and residency.");
			ImGui::End();
			return;
		}
		const ImGuiEx::PanelAction actions[] = {{0, "Refresh estimates", LUX_ICON_REFRESH}};
		if (ImGuiEx::PanelActionBar("##bank_actions", actions) == 0)
			m_MemoryDirty = m_UnbankedDirty = true;
		if (!m_Error.empty())
			ImGuiEx::PanelMessage("Sound Bank was not saved", m_Error.c_str(), true);
		const auto revision = Project::GetEditorAssetManager()->GetAssetRegistry().GetRevision();
		const auto generation = AssetManager::GetDataGeneration();
		if (m_RegistryRevision != revision || m_DataGeneration != generation)
		{
			m_RegistryRevision = revision;
			m_DataGeneration = generation;
			m_MemoryDirty = m_UnbankedDirty = true;
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
		ImGui::TextWrapped("%s", path.c_str());
		ImGui::TextWrapped("Saves automatically | Ctrl+Z to undo");
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
		const bool compact = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 28.0f;
		if (!compact)
			ImGuiEx::BeginPropertyGrid(2, labelWidth);
		const float removeWidth = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGuiEx::PropertyAssetReferenceSettings rowSettings;
		rowSettings.AdvanceToNextColumn = compact;
		rowSettings.WidthOffset = compact ? 0.0f : removeWidth + ImGui::GetStyle().ItemSpacing.x;
		for (size_t index = 0; index < next.Sounds.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			if (compact)
				ImGuiEx::BeginPropertyGrid(2, labelWidth);
			AssetHandle handle = next.Sounds[index];
			const std::string label = "Sound " + std::to_string(index + 1);
			if (ImGuiEx::PropertyAssetReference<Sound>(label.c_str(), handle, "", nullptr, rowSettings, false) && handle && !next.Contains(handle))
			{
				next.Sounds[index] = handle;
				replaced = true;
			}
			if (compact)
			{
				ImGuiEx::EndPropertyGrid();
				const ImGuiEx::PanelAction actions[] = {{0, "Remove Sound", LUX_ICON_TIMES}};
				if (ImGuiEx::PanelActionBar("##remove_sound", actions) == 0)
					removed = static_cast<int>(index);
			}
			else
			{
				ImGui::SameLine();
				if (ImGui::Button("Remove", ImVec2(removeWidth, ImGui::GetFrameHeight())))
					removed = static_cast<int>(index);
				ImGui::NextColumn();
			}
			ImGui::PopID();
		}
		if (!compact)
			ImGuiEx::EndPropertyGrid();
		// An empty slot: drop a Sound on it, or pick one, to add it.
		ImGuiEx::BeginPropertyGrid(2, labelWidth);
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
		if (m_EstimatedSounds != bank.Sounds)
			m_MemoryDirty = m_UnbankedDirty = true;
		if (m_MemoryDirty)
		{
			m_EstimatedSounds = bank.Sounds;
			m_DecodedBytes = 0.0;
			m_Streamed = 0;
			std::unordered_set<uint64_t> clips;
			for (const AssetHandle handle : bank.Sounds)
			{
				const Ref<Sound> sound = AssetManager::GetAssetType(handle) == AssetType::Sound
											 ? AssetManager::GetAsset<Sound>(handle)
											 : nullptr;
				if (!sound)
					continue;
				if (sound->Stream)
				{
					++m_Streamed;
					continue;
				}
				for (const AssetHandle clip : sound->GetClips())
				{
					if (!clips.insert(static_cast<uint64_t>(clip)).second ||
						AssetManager::GetAssetType(clip) != AssetType::Audio)
						continue;
					if (const Ref<AudioFile> file = AssetManager::GetAsset<AudioFile>(clip))
						m_DecodedBytes +=
							file->Duration * file->SamplingRate * file->NumChannels * sizeof(float);
				}
			}
			m_MemoryDirty = false;
		}
		ImGui::TextWrapped("Memory when loaded: about %.2f MiB decoded", m_DecodedBytes / (1024.0 * 1024.0));
		if (m_Streamed)
			ImGui::TextWrapped("%u streamed Sounds use stream pages only.", m_Streamed);
		ImGui::TextWrapped("Refresh estimates after editing Sounds or other banks elsewhere.");
	}

	void SoundBankEditorPanel::UI_Unbanked(const SoundBank& bank)
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Sounds in no bank"))
			return;
		if (m_UnbankedDirty)
		{
			std::unordered_set<uint64_t> banked;
			m_UnbankedIncomplete = false;
			for (const AssetHandle handle : AssetManager::GetAllAssetsWithType<SoundBank>())
			{
				if (const Ref<SoundBank> other = AssetManager::GetAsset<SoundBank>(handle))
					for (const AssetHandle sound : other->Sounds)
						banked.insert(static_cast<uint64_t>(sound));
				else
					m_UnbankedIncomplete = true;
			}
			m_Unbanked.clear();
			for (const AssetHandle sound : AssetManager::GetAllAssetsWithType<Sound>())
				if (!banked.contains(static_cast<uint64_t>(sound)))
					m_Unbanked.emplace_back(AssetPath(sound), sound);
			std::ranges::sort(m_Unbanked);
			m_UnbankedDirty = false;
		}
		if (m_UnbankedIncomplete)
			ImGuiEx::PanelMessage("Some banks could not be read",
								  "See Log. This list may include Sounds held by an unreadable bank.", true);
		if (m_Unbanked.empty())
			ImGui::TextWrapped("Every known Sound is in a readable bank.");
		else
			ImGui::TextWrapped("These load when they first play, which can stall that frame. Add each to the bank of the scene that uses it.");
		for (const auto& [path, sound] : m_Unbanked)
		{
			ImGuiEx::ScopedID id(reinterpret_cast<const void*>(static_cast<uint64_t>(sound)));
			ImGui::TextWrapped("%s", path.c_str());
			const ImGuiEx::PanelAction actions[] = {{0, "Add to bank", LUX_ICON_PLUS}};
			if (ImGuiEx::PanelActionBar("##unbanked_actions", actions) == 0)
			{
				SoundBank next;
				next.Sounds = bank.Sounds;
				next.Resident = bank.Resident;
				if (next.Add(sound))
					Commit(next, "Sound Bank: Sounds");
			}
		}
	}
}
