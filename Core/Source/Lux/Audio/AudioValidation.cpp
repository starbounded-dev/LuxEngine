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
#include <fmod.hpp>
#include <fmod_studio.hpp>
#include <fmod_errors.h>
#include <map>
#include <set>
#include <algorithm>
namespace Lux
{
	namespace
	{
		struct CatalogHost
		{
			FMOD::Studio::System* System = nullptr;
			~CatalogHost()
			{
				if (System)
					if (auto result = System->release(); result != FMOD_OK)
						LUX_CORE_ERROR_TAG("Audio", "Cannot release validation FMOD system: {}", FMOD_ErrorString(result));
			}
		};
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
	AudioValidationReport AudioValidation::ValidateBanks(const std::filesystem::path& directory,
		const std::vector<AudioValidationReference>& references, const std::vector<std::string>& buses)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		AudioValidationReport report;
		std::error_code error;
		std::vector<std::filesystem::path> files;
		if (!directory.empty() && std::filesystem::is_directory(directory, error))
		{
			for (std::filesystem::directory_iterator it(directory, error); !error && it != std::filesystem::directory_iterator(); it.increment(error))
			{
				if (it->path().extension() != ".bank")
					continue;
				if (!it->is_regular_file(error))
				{
					Issue(report, true, it->path().string(), "Bank is not a regular file");
					continue;
				}
				const auto bytes = it->file_size(error);
				if (error || bytes == 0)
					Issue(report, true, it->path().string(), "Bank is empty or unreadable");
				else
				{
					files.push_back(it->path());
					report.Banks.push_back({ it->path().filename().string(), bytes });
					report.BankBytes += bytes;
				}
			}
		}
		if (error)
			Issue(report, true, directory.string(), error.message());
		if (files.empty())
		{
			Issue(report, !references.empty(), directory.string(), "No built FMOD banks found");
			return report;
		}
		if (std::none_of(files.begin(), files.end(), [](const auto& file) { return file.stem().extension() == ".strings"; }))
			Issue(report, false, directory.string(), "No strings bank found; GUID playback works, but script event paths cannot resolve");
		std::sort(files.begin(), files.end(), [](const auto& a, const auto& b)
		{
			const bool as = a.stem().extension() == ".strings", bs = b.stem().extension() == ".strings";
			return as != bs ? as : a < b;
		});
		CatalogHost host;
		const auto check = [&](FMOD_RESULT result, const std::string& operation)
		{
			if (result == FMOD_OK)
				return true;
			Issue(report, true, operation, FMOD_ErrorString(result));
			return false;
		};
		FMOD::System* core = nullptr;
		if (!check(FMOD::Studio::System::create(&host.System), "Create validation system") ||
			!check(host.System->getCoreSystem(&core), "Get validation mixer") ||
			!check(core->setOutput(FMOD_OUTPUTTYPE_NOSOUND), "Set validation output") ||
			!check(host.System->initialize(32, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr), "Initialize validation system"))
			return report;
		std::map<std::string, FMOD::Studio::EventDescription*> catalog;
		std::set<FMOD::Studio::EventDescription*> used;
		for (const auto& file : files)
		{
			FMOD::Studio::Bank* bank = nullptr;
			if (!check(host.System->loadBankFile(file.string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank), file.string()))
				continue;
			int count = 0;
			if (!check(bank->getEventCount(&count), file.string()))
				continue;
			std::vector<FMOD::Studio::EventDescription*> events(static_cast<size_t>(count));
			int retrieved = 0;
			if (count && !check(bank->getEventList(events.data(), count, &retrieved), file.string()))
				continue;
			for (int i = 0; i < retrieved; ++i)
			{
				FMOD_GUID id{};
				if (!check(events[i]->getID(&id), file.string()))
					continue;
				char guid[40];
				std::snprintf(guid, sizeof(guid), "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
					id.Data1, id.Data2, id.Data3, id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
				catalog[guid] = events[i];
			}
		}
		report.CatalogEvents = catalog.size();
		for (const auto& reference : references)
		{
			FMOD_GUID id{};
			FMOD::Studio::EventDescription* event = nullptr;
			if (FMOD::Studio::parseID(reference.Event.Guid.c_str(), &id) != FMOD_OK || host.System->getEventByID(&id, &event) != FMOD_OK || !event)
			{
				Issue(report, true, reference.Location, "Event is absent from built banks: " + reference.Event.Guid + " " + reference.Event.Path);
				continue;
			}
			used.insert(event);
			bool snapshot = false, oneShot = false;
			if (!check(event->isSnapshot(&snapshot), reference.Location) || !check(event->isOneshot(&oneShot), reference.Location))
				continue;
			if ((reference.Kind == AudioReferenceKind::Snapshot) != snapshot || (reference.Kind == AudioReferenceKind::Loop && oneShot))
				Issue(report, true, reference.Location, "Referenced event has the wrong playback type");
		}
		report.ReferencedEvents = used.size();
		for (const auto& [guid, event] : catalog)
		{
			if (used.contains(event))
				continue;
			char path[1024]{};
			const auto result = event->getPath(path, sizeof(path), nullptr);
			Issue(report, false, result == FMOD_OK ? path : guid, "No serialized reference found; verify whether scripts use this event before removing it");
		}
		for (const auto& path : buses)
		{
			FMOD::Studio::Bus* bus = nullptr;
			if (!path.empty() && host.System->getBus(path.c_str(), &bus) != FMOD_OK)
				Issue(report, true, path, "Configured bus is absent from built banks");
		}
		std::sort(report.Banks.begin(), report.Banks.end(), [](const auto& a, const auto& b) { return a.Name < b.Name; });
		return report;
	}

	AudioValidationReport AudioValidation::ValidateProject(const Project& project, Scene* current)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		AudioValidationReport collected;
		std::vector<AudioValidationReference> references;
		const auto add = [&](const AudioEventRef& reference, const std::string& location, AudioReferenceKind kind = AudioReferenceKind::Event)
		{
			if (reference.IsValid())
				references.push_back({ reference, location, kind });
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
					add(source->Event, location);
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
					const auto material = surface ? surface->Material : collider->Acoustic;
					if (!IsValidAcousticMaterial(material))
						Issue(collected, true, location, "Invalid acoustic material");
					else if (material == AcousticMaterial::Default)
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
