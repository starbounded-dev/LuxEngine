// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioValidation.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "NativeInternal.h"

#include <set>

// Validates Sound assets where the FMOD build validates built banks: every reference resolves to a
// Sound with a decodable clip and the right playback kind, and unreferenced Sounds are listed. The
// bank directory and bus list do not apply (there are no banks, and every bus is the master).
namespace Lux
{
	namespace
	{
		void Issue(AudioValidationReport& report, bool error, const std::string& where, const std::string& message)
		{
			report.Issues.push_back({ error ? AudioValidationSeverity::Error : AudioValidationSeverity::Warning, where, message });
		}
	}

	AudioValidationReport AudioValidation::ValidateBanks(const std::filesystem::path&,
		const std::vector<AudioValidationReference>& references, const std::vector<std::string>&)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		AudioValidationReport report;
		std::set<uint64_t> used;
		std::set<std::filesystem::path> checkedClips;
		for (const auto& reference : references)
		{
			Ref<Sound> sound;
			std::string error;
			if (!NativeInternal::ResolveSound(reference.Event.GetReference(), sound, error))
			{
				Issue(report, true, reference.Location, "Sound is unavailable: " + error);
				continue;
			}
			used.insert(static_cast<uint64_t>(sound->Handle));
			if (!sound->Validate(&error))
				Issue(report, true, reference.Location, "Sound is invalid: " + error);
			if (reference.Kind == AudioReferenceKind::Snapshot)
				Issue(report, true, reference.Location, "Snapshots are not supported by the native backend yet");
			else if (reference.Kind == AudioReferenceKind::Loop && !sound->Loops())
				Issue(report, true, reference.Location, "Referenced sound must loop");
			const bool unassigned = std::any_of(sound->Nodes.begin(), sound->Nodes.end(),
				[](const SoundNode& node) { return node.Type == SoundNodeType::Clip && !node.Clip; });
			if (unassigned)
				Issue(report, true, reference.Location, "Sound has a clip node with no audio clip assigned");
			for (const AssetHandle clip : sound->GetClips())
			{
				std::filesystem::path file;
				if (!NativeInternal::ResolveClip(clip, file, error))
					Issue(report, true, reference.Location, "Sound clip is unavailable: " + error);
				else if (checkedClips.insert(file).second && !AudioFileUtils::GetFileInfo(file))
					Issue(report, true, reference.Location, "Sound clip cannot be decoded: " + file.string());
			}
		}
		report.ReferencedEvents = used.size();
		const auto catalog = AssetManager::GetAllAssetsWithType<Sound>();
		report.CatalogEvents = catalog.size();
		auto* editor = dynamic_cast<EditorAssetManager*>(Project::GetAssetManager().Raw());
		for (const auto handle : catalog)
		{
			if (used.contains(static_cast<uint64_t>(handle)))
				continue;
			const auto path = editor ? editor->GetMetadata(handle).FilePath.generic_string() : std::string{};
			Issue(report, false, path.empty() ? std::to_string(static_cast<uint64_t>(handle)) : path,
				"No serialized reference found; verify whether scripts use this sound before removing it");
		}
		return report;
	}
}
