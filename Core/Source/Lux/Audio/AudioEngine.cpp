#include "lpch.h"
#include "AudioEngine.h"
#include "AudioAccessibility.h"

#include "Lux/Project/Project.h"

#include <fmod.hpp>
#include <fmod_errors.h>
#include <fmod_studio.hpp>

#include <algorithm>
#include <cmath>

namespace Lux {

	namespace {

		constexpr int kMaxChannels = 512;
		bool s_LiveUpdateEnabled = false;
		std::vector<FMOD::Studio::Bank*> s_Banks;
		std::vector<std::filesystem::path> s_BankPaths;
		std::vector<AudioBankInfo> s_BankInfo;
		std::vector<AudioEventInfo> s_Events;
		FMOD_RESULT s_LastStudioUpdateResult = FMOD_OK;

		bool CheckFMOD(FMOD_RESULT result, const char* operation)
		{
			if (result == FMOD_OK)
				return true;

			LUX_CORE_ERROR_TAG("Audio", "{0}: {1}", operation, FMOD_ErrorString(result));
			return false;
		}

		// FMOD writes GUIDs in the same braced form fmodstudiocl exports to GUIDs.txt, so scenes
		// and the exported table use one spelling and can be compared as plain strings.
		std::string GuidToString(const FMOD_GUID& guid)
		{
			char buffer[40];
			std::snprintf(buffer, sizeof(buffer), "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
				guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
				guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
			return buffer;
		}
	}

	FMOD::System* AudioEngine::s_Engine;
	FMOD::Studio::System* AudioEngine::s_StudioSystem;

	void AudioEngine::Init()
	{
		LUX_PROFILE_FUNCTION("AudioEngine::Init");

		if (s_HasInitializedAudioEngine)
			return;

		s_ShuttingDown = false;
		s_LastStudioUpdateResult = FMOD_OK;

		// Studio is created first and owns the Core system: Studio::System::initialize creates it
		// internally, so calling System_Create ourselves would leave a second, silent core system
		// that nothing mixes through. getCoreSystem hands back the one Studio will use, and it is
		// valid before initialize precisely so core settings can be applied first.
		FMOD_RESULT result = FMOD::Studio::System::create(&s_StudioSystem);
		if (result != FMOD_OK)
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed to create FMOD Studio system: {}", FMOD_ErrorString(result));
			s_StudioSystem = nullptr;
			return;
		}

		result = s_StudioSystem->getCoreSystem(&s_Engine);
		if (result != FMOD_OK)
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed to get the FMOD core system: {}", FMOD_ErrorString(result));
			CheckFMOD(s_StudioSystem->release(), "Failed to release FMOD Studio after startup failure");
			s_StudioSystem = nullptr;
			s_Engine = nullptr;
			return;
		}

		// Live update lets the FMOD Studio app attach to this process and remix while the game
		// runs. It opens a listening socket, so it follows the project's setting rather than being
		// unconditional, and is meaningless in a shipped build.
		FMOD_STUDIO_INITFLAGS studioFlags = FMOD_STUDIO_INIT_NORMAL;
		s_LiveUpdateEnabled = false;
#ifndef LUX_DIST
		if (Ref<Project> project = Project::GetActive(); project && project->GetConfig().Audio.EnableLiveUpdate)
		{
			studioFlags |= FMOD_STUDIO_INIT_LIVEUPDATE;
			s_LiveUpdateEnabled = true;
		}
#endif

		result = s_StudioSystem->initialize(kMaxChannels, studioFlags,
			FMOD_INIT_3D_RIGHTHANDED, nullptr);
		if (result != FMOD_OK)
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed to initialize FMOD Studio system: {}", FMOD_ErrorString(result));
			CheckFMOD(s_StudioSystem->release(), "Failed to release FMOD Studio after startup failure");
			s_StudioSystem = nullptr;
			s_Engine = nullptr;
			s_LiveUpdateEnabled = false;
			return;
		}
		s_HasInitializedAudioEngine = true;

		// Logged unconditionally on success, not only on failure: "did FMOD come up, and with what"
		// is the first question asked whenever a project is silent, and an absent line is itself the
		// answer.
		int sampleRate = 0;
		CheckFMOD(s_Engine->getSoftwareFormat(&sampleRate, nullptr, nullptr), "Failed to query FMOD software format");
		LUX_CORE_INFO_TAG("Audio", "FMOD Studio initialized ({0} Hz, {1} channels, live update {2})",
			sampleRate, kMaxChannels, s_LiveUpdateEnabled ? "on" : "off");

		if (s_LiveUpdateEnabled)
			LUX_CORE_INFO_TAG("Audio", "Connect the FMOD Studio app to this process to mix while it runs");
	}

	void AudioEngine::Shutdown()
	{
		LUX_PROFILE_FUNCTION("AudioEngine::Shutdown");

		s_ShuttingDown = true;
		s_HasInitializedAudioEngine = false;

		UnloadAllBanks();

		// Only the Studio system is released: it created the core system in initialize and owns it,
		// so closing or releasing s_Engine here as well would be a double free. Releasing Studio
		// tears down both.
		if (s_StudioSystem)
		{
			CheckFMOD(s_StudioSystem->release(), "Failed to release FMOD Studio");
			s_StudioSystem = nullptr;
		}

		s_Engine = nullptr;
		s_LiveUpdateEnabled = false;
	}

	void AudioEngine::Update()
	{
		// Studio owns and updates the Core mixer; all playback is authored as Studio events.
		if (s_StudioSystem)
		{
			const FMOD_RESULT result = s_StudioSystem->update();
			if (result != s_LastStudioUpdateResult)
				CheckFMOD(result, "FMOD Studio update failed");
			s_LastStudioUpdateResult = result;
		}
	}

	bool AudioEngine::LoadBank(const std::filesystem::path& path)
	{
		std::error_code ec;
		const auto canonical = std::filesystem::weakly_canonical(path, ec);
		if (!s_StudioSystem || ec || !std::filesystem::is_regular_file(canonical, ec) || canonical.extension() != ".bank")
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot load bank '{0}': check FMOD initialization and the .bank file path", path.string());
			return false;
		}
		if (std::find(s_BankPaths.begin(), s_BankPaths.end(), canonical) != s_BankPaths.end())
			return true;
		FMOD::Studio::Bank* bank = nullptr;
		const auto result = s_StudioSystem->loadBankFile(canonical.string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);
		if (result != FMOD_OK || !bank)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot load bank '{0}': {1}", path.string(),
				result != FMOD_OK ? FMOD_ErrorString(result) : "FMOD returned no bank handle");
			return false;
		}
		s_Banks.push_back(bank);
		s_BankPaths.push_back(canonical);
		s_BankInfo.push_back({ canonical.filename().string(), 0, canonical.stem().extension() == ".strings" });
		return RefreshBankEvents();
	}

	bool AudioEngine::LoadBanks(const std::filesystem::path& bankDirectory)
	{
		if (!s_StudioSystem)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot load banks from '{0}': FMOD Studio is not initialized", bankDirectory.string());
			return false;
		}

		std::error_code ec;
		if (bankDirectory.empty() || !std::filesystem::is_directory(bankDirectory, ec))
		{
			LUX_CORE_WARN_TAG("Audio", "No built banks at '{0}' - build the FMOD Studio project to produce them", bankDirectory.string());
			return false;
		}

		// Collect first so the strings bank can be loaded before the rest. It carries the event
		// path table; loading it late means every getEvent("event:/...") issued in between fails
		// with EVENT_NOTFOUND, which does not hint at the real cause.
		std::vector<std::filesystem::path> stringsBanks;
		std::vector<std::filesystem::path> contentBanks;
		for (auto it = std::filesystem::directory_iterator(bankDirectory, ec);
			!ec && it != std::filesystem::directory_iterator(); it.increment(ec))
		{
			const auto& entry = *it;
			if (!entry.is_regular_file(ec) || entry.path().extension() != ".bank")
			{
				if (ec)
					break;
				continue;
			}

			if (entry.path().stem().extension() == ".strings")
				stringsBanks.push_back(entry.path());
			else
				contentBanks.push_back(entry.path());
		}
		if (ec)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot enumerate banks in '{0}': {1}", bankDirectory.string(), ec.message());
			return false;
		}
		if (stringsBanks.empty() && contentBanks.empty())
		{
			LUX_CORE_ERROR_TAG("Audio", "No .bank files in '{0}'", bankDirectory.string());
			return false;
		}

		UnloadAllBanks();
		std::sort(stringsBanks.begin(), stringsBanks.end());
		std::sort(contentBanks.begin(), contentBanks.end());

		if (stringsBanks.empty())
			LUX_CORE_WARN_TAG("Audio", "No .strings.bank in '{0}' - events will only resolve by GUID, not by path", bankDirectory.string());

		auto loadOne = [](const std::filesystem::path& path, bool isStringsBank)
			{
				std::error_code pathError;
				const auto canonical = std::filesystem::weakly_canonical(path, pathError);
				if (pathError)
				{
					LUX_CORE_ERROR_TAG("Audio", "Cannot resolve bank path '{0}': {1}", path.string(), pathError.message());
					return false;
				}
				FMOD::Studio::Bank* bank = nullptr;
				const FMOD_RESULT result = s_StudioSystem->loadBankFile(path.string().c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);
				if (result != FMOD_OK || !bank)
				{
					LUX_CORE_ERROR_TAG("Audio", "Failed to load bank '{0}': {1}", path.filename().string(), FMOD_ErrorString(result));
					return false;
				}

				s_Banks.push_back(bank);
				s_BankPaths.push_back(canonical);
				s_BankInfo.push_back(AudioBankInfo{ path.filename().string(), 0, isStringsBank });
				return true;
			};

		bool success = true;
		for (const auto& path : stringsBanks)
			success = loadOne(path, true) && success;
		for (const auto& path : contentBanks)
			success = loadOne(path, false) && success;

		if (s_Banks.empty())
			return false;

		if (!CheckFMOD(s_StudioSystem->flushCommands(), "Failed to flush FMOD bank commands"))
			return false;

		success = RefreshBankEvents() && success;
		return success;
	}

	bool AudioEngine::RefreshBankEvents()
	{
		s_Events.clear();
		++s_BankRevision;
		bool success = true;
		for (size_t bankIndex = 0; bankIndex < s_Banks.size(); bankIndex++)
		{
			FMOD::Studio::Bank* bank = s_Banks[bankIndex];

			int eventCount = 0;
			if (!CheckFMOD(bank->getEventCount(&eventCount), "Failed to query bank event count"))
			{
				success = false;
				continue;
			}
			s_BankInfo[bankIndex].EventCount = eventCount;
			if (eventCount <= 0)
				continue;

			std::vector<FMOD::Studio::EventDescription*> descriptions(static_cast<size_t>(eventCount), nullptr);
			int retrieved = 0;
			if (!CheckFMOD(bank->getEventList(descriptions.data(), eventCount, &retrieved), "Failed to enumerate bank events"))
			{
				success = false;
				continue;
			}

			for (int i = 0; i < retrieved; i++)
			{
				FMOD::Studio::EventDescription* description = descriptions[static_cast<size_t>(i)];
				if (!description)
					continue;

				AudioEventInfo info;

				FMOD_GUID guid{};
				if (!CheckFMOD(description->getID(&guid), "Failed to query event GUID"))
				{
					success = false;
					continue;
				}
				info.Guid = GuidToString(guid);
				info.Path = info.Guid; // GUID-only banks are valid even without the strings bank.
				int pathLength = 0;
				const FMOD_RESULT pathResult = description->getPath(nullptr, 0, &pathLength);
				if ((pathResult == FMOD_OK || pathResult == FMOD_ERR_TRUNCATED) && pathLength > 0)
				{
					std::string path(static_cast<size_t>(pathLength), '\0');
					if (CheckFMOD(description->getPath(path.data(), pathLength, nullptr), "Failed to query event path"))
					{
						path.resize(static_cast<size_t>(pathLength - 1));
						info.Path = std::move(path);
					}
					else
						success = false;
				}
				else if (pathResult != FMOD_ERR_EVENT_NOTFOUND && !CheckFMOD(pathResult, "Failed to query event path length"))
					success = false;

				bool is3D = false;
				success = CheckFMOD(description->is3D(&is3D), "Failed to query event spatialization") && success;
				info.Is3D = is3D;

				bool isOneshot = false;
				success = CheckFMOD(description->isOneshot(&isOneshot), "Failed to query event playback mode") && success;
				info.IsOneshot = isOneshot;
				success = CheckFMOD(description->isSnapshot(&info.IsSnapshot), "Failed to query snapshot type") && success;

				// Recorded here rather than looked up later: this loop is the only place the
				// bank-to-event relationship is known without asking FMOD again.
				info.BankName = s_BankInfo[bankIndex].Name;

				s_Events.push_back(std::move(info));
			}
		}

		std::sort(s_Events.begin(), s_Events.end(),
			[](const AudioEventInfo& a, const AudioEventInfo& b) { return a.Path < b.Path; });

		return success;
	}

	bool AudioEngine::LoadRuntimeBanks(const std::filesystem::path& assets, const AudioBankManifest& manifest)
	{
		if (!manifest.Validate())
			return false;
		if (!HasInitializedEngine())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot start runtime audio: FMOD initialization failed");
			return false;
		}
		UnloadAllBanks();
		auto banks = manifest.Banks;
		std::stable_sort(banks.begin(), banks.end(), [](const std::string& a, const std::string& b)
		{
			return std::filesystem::path(a).stem().extension() == ".strings"
				&& std::filesystem::path(b).stem().extension() != ".strings";
		});
		for (const auto& name : banks)
		{
			const auto path = assets / manifest.Directory / name;
			if (!LoadBank(path))
			{
				UnloadAllBanks();
				return false;
			}
		}
		return true;
	}

	void AudioEngine::UnloadAllBanks()
	{
		AudioAccessibility::ReleaseMixer();
		++s_EventGeneration;
		if (s_StudioSystem)
		{
			for (FMOD::Studio::Bank* bank : s_Banks)
			{
				if (bank)
					CheckFMOD(bank->unload(), "Failed to unload FMOD bank");
			}
		}

		s_Banks.clear();
		s_BankPaths.clear();
		++s_BankRevision;
		s_BankInfo.clear();
		s_Events.clear();
	}

	const std::vector<AudioBankInfo>& AudioEngine::GetLoadedBanks()
	{
		return s_BankInfo;
	}

	const std::vector<AudioEventInfo>& AudioEngine::GetEvents()
	{
		return s_Events;
	}

	std::string AudioEngine::ResolveEventReference(const std::string& reference)
	{
		if (!reference.starts_with("event:/") && !reference.starts_with("snapshot:/"))
			return reference;
		FMOD_GUID guid{};
		if (!s_StudioSystem || !CheckFMOD(s_StudioSystem->lookupID(reference.c_str(), &guid), "Failed to resolve event path (load the strings bank)"))
			return {};
		return GuidToString(guid);
	}

	bool AudioEngine::SetBusMuted(const std::string& path, bool muted)
	{
		FMOD::Studio::Bus* bus = nullptr;
		return s_StudioSystem && CheckFMOD(s_StudioSystem->getBus(path.c_str(), &bus), "Bus lookup failed")
			&& CheckFMOD(bus->setMute(muted), "Set bus mute failed");
	}

	bool AudioEngine::SetVCAVolume(const std::string& path, float volume)
	{
		if (!std::isfinite(volume))
		{
			LUX_CORE_ERROR_TAG("Audio", "VCA volume must be finite");
			return false;
		}
		FMOD::Studio::VCA* vca = nullptr;
		return s_StudioSystem && CheckFMOD(s_StudioSystem->getVCA(path.c_str(), &vca), "VCA lookup failed")
			&& CheckFMOD(vca->setVolume(std::max(0.0f, volume)), "Set VCA volume failed");
	}

	bool AudioEngine::SetGlobalParameter(const std::string& name, float value)
	{
		if (!std::isfinite(value))
		{
			LUX_CORE_ERROR_TAG("Audio", "Global parameter value must be finite");
			return false;
		}
		return s_StudioSystem && CheckFMOD(s_StudioSystem->setParameterByName(name.c_str(), value), "Set global parameter failed");
	}

	float AudioEngine::GetGlobalParameter(const std::string& name)
	{
		float value = 0.0f;
		if (s_StudioSystem)
			CheckFMOD(s_StudioSystem->getParameterByName(name.c_str(), &value), "Get global parameter failed");
		return value;
	}

	bool AudioEngine::SetBusVolume(const std::string& busPath, float volume)
	{
		if (!s_StudioSystem)
			return false;

		FMOD::Studio::Bus* bus = nullptr;
		if (!CheckFMOD(s_StudioSystem->getBus(busPath.c_str(), &bus), "Bus lookup failed") || !bus)
			return false;

		if (!std::isfinite(volume))
		{
			LUX_CORE_ERROR_TAG("Audio", "Bus volume must be finite");
			return false;
		}
		return CheckFMOD(bus->setVolume(std::max(volume, 0.0f)), "Set bus volume failed");
	}

	float AudioEngine::GetBusVolume(const std::string& busPath)
	{
		if (!s_StudioSystem)
			return 0.0f;

		FMOD::Studio::Bus* bus = nullptr;
		if (!CheckFMOD(s_StudioSystem->getBus(busPath.c_str(), &bus), "Bus lookup failed") || !bus)
			return 0.0f;

		float volume = 0.0f;
		CheckFMOD(bus->getVolume(&volume), "Get bus volume failed");
		return volume;
	}

	AudioEngineStats AudioEngine::GetStats()
	{
		AudioEngineStats stats;
		stats.BackendName = "FMOD";
		stats.Initialized = s_Engine != nullptr;
		if (!s_Engine)
			return stats;

		stats.HasMixerStats = true;

		// FMOD packs its version as 0xMMMMmmpp (major/minor/patch nibbles), e.g. 0x00020314.
		unsigned int version = 0;
		if (s_Engine->getVersion(&version) == FMOD_OK)
		{
			stats.VersionMajor = (version >> 16) & 0xFFFF;
			stats.VersionMinor = (version >> 8) & 0xFF;
			stats.VersionPatch = version & 0xFF;
		}

		s_Engine->getSoftwareFormat(&stats.SampleRate, nullptr, nullptr);
		s_Engine->getChannelsPlaying(&stats.ChannelsPlaying, &stats.RealChannelsPlaying);

		FMOD_CPU_USAGE usage{};
		if (s_Engine->getCPUUsage(&usage) == FMOD_OK)
		{
			stats.DSPCPUPercent = usage.dsp;
			stats.StreamCPUPercent = usage.stream;
			stats.UpdateCPUPercent = usage.update;
		}

		// blocking = false: this runs every frame from the editor UI, and the mixer holding the
		// allocator lock is not worth stalling the main thread over for a debug readout.
		FMOD::Memory_GetStats(&stats.MemoryCurrentBytes, &stats.MemoryPeakBytes, false);

		stats.LiveUpdateEnabled = s_LiveUpdateEnabled;
		stats.LoadedBankCount = (int)s_BankInfo.size();
		stats.EventDescriptionCount = (int)s_Events.size();

		// Instance count is per-description, so it has to be summed. Cheap: this is a handful of
		// integer getters over the events a project actually has.
		if (s_StudioSystem)
		{
			int instanceCount = 0;
			for (FMOD::Studio::Bank* bank : s_Banks)
			{
				int eventCount = 0;
				if (!bank || bank->getEventCount(&eventCount) != FMOD_OK || eventCount <= 0)
					continue;

				std::vector<FMOD::Studio::EventDescription*> descriptions((size_t)eventCount, nullptr);
				int retrieved = 0;
				if (bank->getEventList(descriptions.data(), eventCount, &retrieved) != FMOD_OK)
					continue;

				for (int i = 0; i < retrieved; i++)
				{
					int perDescription = 0;
					if (descriptions[(size_t)i] && descriptions[(size_t)i]->getInstanceCount(&perDescription) == FMOD_OK)
						instanceCount += perDescription;
				}
			}
			stats.PlayingEventInstances = instanceCount;
		}

		return stats;
	}

}
