// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioValidation.h"
#include "AudioSurfaceTable.h"
#include "DialogueTable.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include <algorithm>
namespace Lux
{
	namespace
	{
		void Issue(AudioValidationReport& report, bool error, const std::string& where, const std::string& message)
		{
			report.Issues.push_back({ error ? AudioValidationSeverity::Error : AudioValidationSeverity::Warning, where, message });
		}
	}
	bool AudioValidationReport::HasErrors() const
	{
		return std::any_of(Issues.begin(), Issues.end(), [](const auto& issue) { return issue.Severity == AudioValidationSeverity::Error; });
	}
	void AudioValidationReport::Log() const
	{
		for (const auto& issue : Issues)
		{
			if (issue.Severity == AudioValidationSeverity::Error)
				LUX_CORE_ERROR_TAG("Audio", "{}: {}", issue.Location, issue.Message);
			else
				LUX_CORE_WARN_TAG("Audio", "{}: {}", issue.Location, issue.Message);
		}
		LUX_CORE_INFO_TAG("Audio", "Audio validation: {} banks, {} bytes, {} catalog events, {} referenced events, {} issues",
			Banks.size(), BankBytes, CatalogEvents, ReferencedEvents, Issues.size());
	}
	AudioValidationReport AudioValidation::ValidateProject(const Project& project, Scene* current)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		AudioValidationReport collected;
		std::vector<AudioValidationReference> references;
		const auto add = [&](const AudioEventRef& reference, const std::string& location, AudioReferenceKind kind = AudioReferenceKind::Event, bool occludable = false)
		{
			if (reference.IsValid())
				references.push_back({ reference, location, kind, occludable });
		};
		const auto scene = [&](Scene& scene)
		{
			for (auto handle : scene.GetAllEntitiesWith<IDComponent>())
			{
				Entity entity{ handle, &scene };
				const std::string location = scene.GetName() + "/" + entity.GetComponent<TagComponent>().Tag + " (" + std::to_string(static_cast<uint64_t>(entity.GetUUID())) + ")";
				if (const auto* source = entity.TryGetComponent<AudioSourceComponent>())
				{
					if (!source->Event.IsValid())
						Issue(collected, source->LegacyAudio != 0, location, source->LegacyAudio ?
							"Legacy audio source cannot play; assign an FMOD event" : "Audio source has no FMOD event assigned");
					add(source->Event, location, AudioReferenceKind::Event, true);
					if (source->Priority < 0 || source->Priority > 256)
						Issue(collected, true, location, "Audio priority must be between 0 and 256");
				}
				if (const auto* music = entity.TryGetComponent<MusicDirectorComponent>())
					add(music->Event, location + "/Music", AudioReferenceKind::Loop);
				if (const auto* zone = entity.TryGetComponent<AudioZoneComponent>())
				{
					add(zone->AmbienceEvent, location + "/Ambience", AudioReferenceKind::Loop);
					add(zone->Snapshot, location + "/Snapshot", AudioReferenceKind::Snapshot);
				}
				if (const auto* surface = entity.TryGetComponent<AudioSurfaceComponent>())
				{
					add(surface->FootstepOverride, location + "/Footstep");
					add(surface->ImpactOverride, location + "/Impact");
				}
				if (const auto* collider = entity.TryGetComponent<MeshColliderComponent>(); collider && collider->AcousticMotion != AcousticGeometryMode::Disabled)
				{
					const auto* surface = entity.TryGetComponent<AudioSurfaceComponent>();
					const auto stored = surface ? surface->Material : collider->Acoustic;
					if (!IsValidAcousticMaterial(stored))
						Issue(collected, true, location, "Invalid acoustic material");
					else if (scene.ResolveAcousticMaterial(entity) == AcousticMaterial::Default)
						Issue(collected, false, location, "Collider uses the default concrete acoustic material; verify this is intentional");
				}
			}
		};
		if (current)
			scene(*current);
		for (const auto id : AssetManager::GetAllAssetsWithType<Scene>())
		{
			if (current && current->Handle == id)
				continue;
			if (auto asset = AssetManager::GetAsset<Scene>(id))
				scene(*asset);
			else
				Issue(collected, true, std::to_string(static_cast<uint64_t>(id)), "Scene could not be loaded for audio validation");
		}
		for (const auto id : AssetManager::GetAllAssetsWithType<Prefab>())
		{
			if (auto asset = AssetManager::GetAsset<Prefab>(id); asset && asset->GetScene())
			{
				Ref<Scene> prefabScene = asset->GetScene();
				scene(*prefabScene);
			}
			else
				Issue(collected, true, std::to_string(static_cast<uint64_t>(id)), "Prefab could not be loaded for audio validation");
		}
		for (const auto id : AssetManager::GetAllAssetsWithType<AudioSurfaceTable>())
		{
			auto table = AssetManager::GetAsset<AudioSurfaceTable>(id);
			const std::string location = "Surface table " + std::to_string(static_cast<uint64_t>(id));
			if (!table || !table->Validate())
			{
				Issue(collected, true, location, "Surface table unavailable or invalid");
				continue;
			}
			for (const auto& sounds : table->Surfaces)
			{
				add(sounds.Footstep, location); add(sounds.Impact, location);
				add(sounds.Scrape, location, AudioReferenceKind::Loop); add(sounds.Roll, location, AudioReferenceKind::Loop);
			}
		}
		for (const auto id : AssetManager::GetAllAssetsWithType<DialogueTable>())
		{
			auto table = AssetManager::GetAsset<DialogueTable>(id);
			const std::string location = "Dialogue table " + std::to_string(static_cast<uint64_t>(id));
			if (!table || !table->Validate())
			{
				Issue(collected, true, location, "Dialogue table unavailable or invalid");
				continue;
			}
			for (const auto& [key, line] : table->Lines)
				add(line.Event, location + "/" + key);
		}
		const auto& config = project.GetConfig().Audio;
		for (const auto& [guid, metadata] : config.Accessibility.Events)
			add({ guid, {}, {} }, "Accessibility metadata");
		std::vector<std::string> buses;
		for (const auto& [path, limit] : project.GetAudioPerformance().BusVoices)
			buses.push_back(path);
		for (const auto& path : config.Accessibility.BusPaths)
			if (!path.empty())
				buses.push_back(path);
		auto report = ValidateBanks(project.GetStudioProjectPath().empty() ? std::filesystem::path{} : project.GetStudioBankDirectory(), references, buses);
		report.Issues.insert(report.Issues.end(), collected.Issues.begin(), collected.Issues.end());
		if (!project.GetAudioPerformance().Validate() || !IsValidStudioPlatform(project.GetStudioPlatform()) || !config.Windows.Validate() || !config.Linux.Validate())
			Issue(report, true, "Project audio settings", "Invalid desktop audio profile or performance budgets");
		std::sort(report.Issues.begin(), report.Issues.end(), [](const auto& a, const auto& b)
		{
			return a.Severity != b.Severity ? a.Severity > b.Severity : std::tie(a.Location, a.Message) < std::tie(b.Location, b.Message);
		});
		return report;
	}
}
