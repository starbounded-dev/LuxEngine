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

	// Checks Sounds, clips and the project mixer; the directory is the FMOD bank location and unused.
	AudioValidationReport AudioValidation::ValidateBanks(const std::filesystem::path&,
		const std::vector<AudioValidationReference>& references, const std::vector<std::string>& buses)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		AudioValidationReport report;
		const auto project = Project::GetActive();
		const AudioMixerSettings mixer = project ? project->GetConfig().Audio.Mixer : AudioMixerSettings{};
		std::string mixerError;
		if (!mixer.Validate(&mixerError))
			Issue(report, true, "Audio Mixer", mixerError);
		const auto hasBus = [&](const std::string& path)
		{
			return path == "bus:/" || std::any_of(mixer.Buses.begin(), mixer.Buses.end(), [&](const AudioMixerBus& bus) { return bus.Path == path; });
		};
		for (const auto& path : buses)
		{
			if (!path.empty() && !hasBus(path))
				Issue(report, true, path, "Configured bus is absent from the project's mixer (View > Audio Mixer)");
		}
		std::set<uint64_t> used;
		std::set<std::filesystem::path> checkedClips;
		for (const auto& reference : references)
		{
			// A snapshot reference holds a mixer snapshot's ID, not a Sound.
			if (reference.Kind == AudioReferenceKind::Snapshot)
			{
				const auto id = static_cast<uint64_t>(reference.Event.Sound);
				if (std::none_of(mixer.Snapshots.begin(), mixer.Snapshots.end(), [&](const AudioMixerSnapshot& snapshot) { return static_cast<uint64_t>(snapshot.ID) == id; }))
					Issue(report, true, reference.Location, "Snapshot is not in the project's mixer");
				continue;
			}
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
			if (reference.Kind == AudioReferenceKind::Loop && !sound->Loops())
				Issue(report, true, reference.Location, "Referenced sound must loop");
			if (!hasBus(sound->Bus))
				Issue(report, false, reference.Location, "Sound plays on '" + sound->Bus + "', which the mixer does not have; it plays through the master");
			const bool unassigned = std::any_of(sound->Nodes.begin(), sound->Nodes.end(),
				[](const SoundNode& node) { return node.Type == SoundNodeType::Clip && !node.Clip; });
			if (unassigned)
				Issue(report, true, reference.Location, "Sound has a clip node with no audio clip assigned");
			for (const AssetHandle clip : sound->GetClips())
			{
				std::filesystem::path file;
				if (!NativeInternal::ResolveClip(clip, file, error))
					Issue(report, true, reference.Location, "Sound clip is unavailable: " + error);
				else if (checkedClips.insert(file).second)
				{
					if (const auto info = AudioFileUtils::GetFileInfo(file))
						report.BankBytes += info->FileSize; // the audio a build ships, in place of bank bytes
					else
						Issue(report, true, reference.Location, "Sound clip cannot be decoded: " + file.string());
				}
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
