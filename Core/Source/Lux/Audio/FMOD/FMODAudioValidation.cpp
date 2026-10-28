// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioValidation.h"

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
			FMOD_STUDIO_PARAMETER_DESCRIPTION occlusion{};
			if (reference.Occludable && !snapshot && event->getParameterDescriptionByName("Occlusion", &occlusion) != FMOD_OK)
				Issue(report, false, reference.Location, "Event has no Occlusion parameter, so walls will not muffle it; add a local 0-1 Occlusion parameter driving a filter in FMOD Studio");
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
}
