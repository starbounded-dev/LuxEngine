// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Audio/SoundBank.h"
#include "Lux/Editor/EditorPanel.h"

#include <functional>
#include <string>

namespace Lux
{
	// Edits a Sound Bank (.lbank): its Sounds (dropped from the Content Browser or picked), Resident, an
	// estimate of the memory it holds, and the project's Sounds that no bank holds. Every edit is saved
	// at once and goes on the editor's undo stack. Main thread only.
	class SoundBankEditorPanel : public EditorPanel
	{
	public:
		using UndoPushFn = std::function<void(const std::string& label, std::function<void()> undo, std::function<void()> redo)>;

		void OnImGuiRender(bool& isOpen) override;
		void OnProjectChanged(const Ref<Project>& project) override;
		void OnEvent(Event& event) override;
		void OnClose() override
		{
			m_IsFocused = false;
			m_RequestFocus = false;
		}
		void SetUndoCallback(UndoPushFn callback) { m_PushUndo = std::move(callback); }
		void OpenBank(AssetHandle bank);
#ifndef LUX_DIST
		static bool RunSelfTests(std::vector<std::string>& failures);
#endif

	private:
		// Replaces the bank's contents with next, saves it, and pushes one undo step.
		bool Commit(const SoundBank& next, const char* label);
		void UI_Sounds(const SoundBank& bank);
		void UI_Memory(const SoundBank& bank);
		void UI_Unbanked(const SoundBank& bank);

		AssetHandle m_Bank = 0;
		bool m_RequestFocus = false;
		bool m_IsFocused = false;
		std::string m_Error;
		bool m_MemoryDirty = true;
		bool m_UnbankedDirty = true;
		uint64_t m_RegistryRevision = 0;
		uint64_t m_DataGeneration = 0;
		double m_DecodedBytes = 0.0;
		uint32_t m_Streamed = 0;
		std::vector<std::pair<std::string, AssetHandle>> m_Unbanked;
		std::vector<AssetHandle> m_EstimatedSounds;
		bool m_UnbankedIncomplete = false;
		UndoPushFn m_PushUndo;
	};
}
