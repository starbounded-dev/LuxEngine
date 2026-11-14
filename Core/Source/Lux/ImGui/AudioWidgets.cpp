// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioWidgets.h"
#include "ImGuiUtilities.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Project/Project.h"
#include <imgui.h>
#include <algorithm>

namespace Lux::ImGuiEx
{
	namespace
	{
		// Each picker edits only its own backend's field, so choosing in one build never erases the
		// other build's assignment.
		std::string SoundName(AssetHandle handle)
		{
			auto* editor = dynamic_cast<EditorAssetManager*>(Project::GetAssetManager().Raw());
			const auto path = editor ? editor->GetMetadata(handle).FilePath : std::filesystem::path{};
			return path.empty() ? std::to_string(static_cast<uint64_t>(handle)) : path.stem().string();
		}

		Ref<Sound> LoadSound(AssetHandle handle)
		{
			return handle && AssetManager::IsAssetHandleValid(handle) && AssetManager::GetAssetType(handle) == AssetType::Sound
				? AssetManager::GetAsset<Sound>(handle) : nullptr;
		}

		bool SoundPicker(const char* label, AudioEventRef& reference, bool oneShot, bool mixed, bool require2D)
		{
			ScopedID id(label);
			bool changed = false;
			const auto fits = [&](const Sound& sound) { return sound.Loops() != oneShot && (!require2D || !sound.Spatial.Enabled); };
			const AssetHandle assigned = reference.Sound;
			const Ref<Sound> assignedSound = LoadSound(assigned);
			const std::string preview = mixed ? "Multiple values" : !assigned ? "None" : SoundName(assigned);
			if (ImGui::BeginCombo(label, preview.c_str()))
			{
				if (ImGui::Selectable("None", !assigned))
				{
					reference.Sound = 0;
					changed = true;
				}
				std::vector<std::pair<std::string, AssetHandle>> sounds;
				for (const auto handle : AssetManager::GetAllAssetsWithType<Sound>())
				{
					if (const auto sound = LoadSound(handle); sound && fits(*sound))
						sounds.emplace_back(SoundName(handle), handle);
				}
				std::sort(sounds.begin(), sounds.end());
				for (const auto& [name, handle] : sounds)
				{
					ScopedID soundID(handle);
					if (ImGui::Selectable(name.c_str(), handle == assigned))
					{
						reference.Sound = handle;
						changed = true;
					}
				}
				if (sounds.empty())
					ImGui::TextDisabled(oneShot ? "No one-shot sounds. Create one from an audio file in the Content Browser."
						: "No looping sounds. Create one, then set it to loop.");
				ImGui::EndCombo();
			}
			if (!changed && !mixed && assigned && (!assignedSound || !fits(*assignedSound)))
				ImGui::TextWrapped(oneShot ? "Assigned sound is unavailable or loops; this needs a one-shot sound."
					: "Assigned sound is unavailable or does not loop.");
			return changed;
		}
	}

	bool SurfaceEventPicker(const char* label, AudioEventRef& reference, bool oneShot, bool mixed, bool require2D)
	{
		return SoundPicker(label, reference, oneShot, mixed, require2D);
	}
}
