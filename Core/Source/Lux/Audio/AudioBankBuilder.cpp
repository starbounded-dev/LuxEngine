// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioBankBuilder.h"
#include "AudioPerformanceSettings.h"

#include <cstdlib>
#include <chrono>
#include <sstream>
#include <stdexcept>

#ifndef LUX_PLATFORM_WINDOWS
#include <unistd.h>
#endif

namespace Lux {

	namespace {

		// Checked before anything else so a machine with Studio installed somewhere unusual, or a
		// CI runner, can point at it without touching the project.
		constexpr const char* kStudioCommandLineEnvVar = "LUX_FMOD_STUDIO_CL";
		constexpr const char* kStudioApplicationEnvVar = "LUX_FMOD_STUDIO";
		constexpr auto kToolLookupInterval = std::chrono::seconds(2);

		// FMOD's installers do not put either binary on PATH, so the usual locations are tried
		// explicitly. Ordered newest-first within each platform.
		const char* kStudioCommandLineCandidates[] = {
#ifdef LUX_PLATFORM_WINDOWS
			"C:/Program Files/FMOD SoundSystem/FMOD Studio 2.03.14/fmodstudiocl.exe",
			"C:/Program Files/FMOD SoundSystem/FMOD Studio/fmodstudiocl.exe",
#else
			"/opt/fmodstudio/fmodstudiocl",
			"/usr/local/fmodstudio/fmodstudiocl",
#endif
		};

		const char* kStudioApplicationCandidates[] = {
#ifdef LUX_PLATFORM_WINDOWS
			"C:/Program Files/FMOD SoundSystem/FMOD Studio 2.03.14/FMOD Studio.exe",
			"C:/Program Files/FMOD SoundSystem/FMOD Studio/FMOD Studio.exe",
#else
			"/opt/fmodstudio/fmodstudio",
			"/usr/local/fmodstudio/fmodstudio",
#endif
		};

		bool IsExecutable(const std::filesystem::path& path)
		{
			std::error_code ec;
			if (!std::filesystem::is_regular_file(path, ec))
				return false;
#ifdef LUX_PLATFORM_WINDOWS
			return true;
#else
			return access(path.c_str(), X_OK) == 0;
#endif
		}

		std::filesystem::path ResolveTool(const char* envVar, const char* const* candidates, size_t candidateCount, const char* pathName)
		{
			if (const char* fromEnv = std::getenv(envVar))
			{
				std::filesystem::path envPath(fromEnv);
				if (IsExecutable(envPath))
					return std::filesystem::absolute(envPath);
			}

			for (size_t i = 0; i < candidateCount; i++)
			{
				std::filesystem::path candidate(candidates[i]);
				if (IsExecutable(candidate))
					return candidate;
			}

			if (const char* searchPath = std::getenv("PATH"))
			{
#ifdef LUX_PLATFORM_WINDOWS
				constexpr char separator = ';';
#else
				constexpr char separator = ':';
#endif
				std::istringstream directories(searchPath);
				std::string directory;
				while (std::getline(directories, directory, separator))
				{
					std::filesystem::path candidate = std::filesystem::path(directory) / pathName;
#ifdef LUX_PLATFORM_WINDOWS
					candidate += ".exe";
#endif
					if (IsExecutable(candidate))
						return std::filesystem::absolute(candidate);
				}
			}
			return {};
		}

		std::string QuoteArgument(const std::filesystem::path& path)
		{
#ifdef LUX_PLATFORM_WINDOWS
			// cmd.exe expands variables even inside quotes. Reject these names instead of running
			// a different command from the one the user selected.
			const std::string value = path.string();
			if (value.find_first_of("\"%!\r\n") != std::string::npos)
				throw std::invalid_argument("FMOD tool paths cannot contain quotes, %, ! or newlines on Windows");
			return "\"" + value + "\"";
#else
			std::string quoted = "'";
			for (char character : path.string())
			{
				if (character == '\'')
					quoted += "'\\''";
				else
					quoted += character;
			}
			return quoted + "'";
#endif
		}

		// Newest write time anywhere under a directory. Used on the .fspro's folder, which holds
		// both the XML metadata and the source audio, so any authoring change moves it.
		std::filesystem::file_time_type NewestWriteTimeUnder(const std::filesystem::path& directory, const std::filesystem::path& skipDirectory)
		{
			std::filesystem::file_time_type newest = std::filesystem::file_time_type::min();

			std::error_code ec;
			for (auto it = std::filesystem::recursive_directory_iterator(directory, ec);
				!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
			{
				// Build/GUIDs.txt and other platforms sit beside Build/Desktop. Neither those nor
				// Studio's caches/workspace settings are authored input to the bank build.
				const auto& path = it->path();
				if (it->is_directory(ec) && (path == skipDirectory || path == directory / "Build"
					|| path.filename() == ".cache" || path.filename() == ".user" || path.filename() == ".git"))
				{
					it.disable_recursion_pending();
					continue;
				}

				if (ec)
					break;
				if (!it->is_regular_file(ec))
				{
					if (ec)
						break;
					continue;
				}

				const auto writeTime = it->last_write_time(ec);
				if (ec)
					break;
				if (writeTime > newest)
					newest = writeTime;
			}
			if (ec)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot inspect Studio sources in '{0}': {1}", directory.string(), ec.message());
				return std::filesystem::file_time_type::max();
			}

			return newest;
		}

	}

	std::filesystem::path AudioBankBuilder::FindStudioCommandLineTool()
	{
		static std::filesystem::path s_Tool;
		static auto s_NextLookup = std::chrono::steady_clock::time_point::min();
		const auto now = std::chrono::steady_clock::now();
		// The settings panel asks every frame, including when Studio is not installed.
		if (now >= s_NextLookup)
		{
			s_Tool = ResolveTool(kStudioCommandLineEnvVar,
				kStudioCommandLineCandidates, std::size(kStudioCommandLineCandidates), "fmodstudiocl");
			s_NextLookup = now + kToolLookupInterval;
		}
		return s_Tool;
	}

	std::filesystem::path AudioBankBuilder::FindStudioApplication()
	{
		static std::filesystem::path s_App;
		if (s_App.empty() || !IsExecutable(s_App))
			s_App = ResolveTool(kStudioApplicationEnvVar,
				kStudioApplicationCandidates, std::size(kStudioApplicationCandidates), "fmodstudio");
		return s_App;
	}

	bool AudioBankBuilder::NeedsRebuild(const std::filesystem::path& studioProjectPath, const std::filesystem::path& bankDirectory)
	{
		std::error_code ec;
		if (studioProjectPath.empty() || !std::filesystem::is_regular_file(studioProjectPath, ec))
		{
			if (ec)
				LUX_CORE_ERROR_TAG("Audio", "Cannot inspect Studio project '{0}': {1}", studioProjectPath.string(), ec.message());
			return false;
		}

		if (!std::filesystem::is_directory(bankDirectory, ec))
			return true;

		// Oldest bank, not newest: a project that gained a bank since the last full build has one
		// current file and one stale one, and the stale one is what matters.
		std::filesystem::file_time_type oldestBank = std::filesystem::file_time_type::max();
		bool foundBank = false;
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

			foundBank = true;
			const auto writeTime = entry.last_write_time(ec);
			if (ec)
				break;
			if (writeTime < oldestBank)
				oldestBank = writeTime;
		}
		if (ec)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot inspect bank timestamps in '{0}': {1}", bankDirectory.string(), ec.message());
			return true;
		}

		if (!foundBank)
			return true;

		const std::filesystem::path projectDirectory = std::filesystem::absolute(studioProjectPath).parent_path().lexically_normal();
		return NewestWriteTimeUnder(projectDirectory, std::filesystem::absolute(bankDirectory).lexically_normal()) > oldestBank;
	}

	bool AudioBankBuilder::Build(const std::filesystem::path& studioProjectPath, const std::string& platform)
	{
		if (!IsValidStudioPlatform(platform))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid FMOD build platform '{}'", platform);
			return false;
		}
		std::error_code ec;
		if (studioProjectPath.empty() || !std::filesystem::is_regular_file(studioProjectPath, ec))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot build banks: FMOD Studio project not found at '{0}'", studioProjectPath.string());
			return false;
		}

		const std::filesystem::path tool = FindStudioCommandLineTool();
		if (tool.empty())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot build banks: fmodstudiocl was not found. Set {0} to its full path", kStudioCommandLineEnvVar);
			return false;
		}

		// -export-guids writes Build/GUIDs.txt: the {guid} -> event:/Path table, for tooling and for
		// diffing what changed between builds. The engine itself reads event GUIDs from the loaded
		// banks (AudioEngine::GetEvents) so the two can never disagree, but the exported file is what
		// makes a rename in Studio traceable - scenes reference events by GUID precisely because a
		// rename would otherwise leave them silently mute.
		std::string command;
		try
		{
			command = QuoteArgument(tool) + " -build -export-guids -platforms " + QuoteArgument(platform) + " " + QuoteArgument(studioProjectPath);
#ifdef LUX_PLATFORM_WINDOWS
			// cmd.exe removes the outer quotes when the command starts with a quoted executable.
			command = "\"" + command + "\"";
#endif
		}
		catch (const std::invalid_argument& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot build banks: {0}", error.what());
			return false;
		}

		LUX_CORE_INFO_TAG("Audio", "Building FMOD banks from '{0}'...", studioProjectPath.filename().string());

		const int result = std::system(command.c_str());
		if (result != 0)
		{
			LUX_CORE_ERROR_TAG("Audio", "Bank build failed with exit code {0}. Command: {1}", result, command);
			return false;
		}

		LUX_CORE_INFO_TAG("Audio", "Bank build completed.");
		return true;
	}

	bool AudioBankBuilder::OpenInStudio(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (path.empty() || !std::filesystem::exists(path, ec))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot open '{0}' in FMOD Studio: it does not exist", path.string());
			return false;
		}

		const std::filesystem::path application = FindStudioApplication();

		// Checked up front because the launch below cannot report this. Backgrounding with "&" makes
		// the shell return 0 immediately whether or not the binary exists, so std::system's exit code
		// says nothing about whether FMOD Studio actually started.
		if (application.empty())
		{
			LUX_CORE_ERROR_TAG("Audio", "FMOD Studio not found at '{0}'. Set {1} to its full path.",
				application.string(), kStudioApplicationEnvVar);
			return false;
		}

		// Detached, because FMOD Studio is a long-lived GUI application - std::system would block
		// the editor until the designer closed it.
		std::string command;
		try
		{
#ifdef LUX_PLATFORM_WINDOWS
			command = "start \"\" " + QuoteArgument(application) + " " + QuoteArgument(path);
#else
			command = QuoteArgument(application) + " " + QuoteArgument(path) + " >/dev/null 2>&1 &";
#endif
		}
		catch (const std::invalid_argument& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot open FMOD Studio: {0}", error.what());
			return false;
		}

		LUX_CORE_INFO_TAG("Audio", "Opening '{0}' in FMOD Studio...", path.filename().string());
		if (std::system(command.c_str()) != 0)
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed to launch FMOD Studio ('{0}')", application.string());
			return false;
		}

		return true;
	}

}
