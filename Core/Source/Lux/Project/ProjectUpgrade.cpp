// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ProjectUpgrade.h"

#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>

namespace Lux
{
	namespace
	{
		using FormatVersion::Kind;

		bool IsSkippedFolder(const std::string& name)
		{
			static const char* skipped[] = { "Cache", "Binaries", "Intermediates", "bin", "bin-int", "obj", "node_modules", "packages" };
			if (!name.empty() && name[0] == '.')
				return true; // .luxbackup, .git, .vs
			return std::any_of(std::begin(skipped), std::end(skipped), [&](const char* entry) { return name == entry; });
		}

		bool ReadHead(const std::filesystem::path& path, size_t limit, std::string& out)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				return false;
			out.assign(limit, '\0');
			stream.read(out.data(), static_cast<std::streamsize>(limit));
			out.resize(static_cast<size_t>(stream.gcount()));
			return true;
		}

		bool ReadAll(const std::filesystem::path& path, std::string& out)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				return false;
			out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
			return true;
		}

		// The kinds that stamp `FormatVersion` are read from the start of the file; the others are small and parsed.
		bool RecordedOf(const std::filesystem::path& path, Kind kind, uint32_t& version)
		{
			std::string text;
			const bool stamps = kind == Kind::Project || kind == Kind::Scene || kind == Kind::Material;
			if (stamps ? !ReadHead(path, 2048, text) : !ReadAll(path, text))
				return false;
			version = FormatVersion::Recorded(kind, text);
			return true;
		}

		bool HasStep(const std::vector<FormatVersion::Step>& steps, Kind kind, uint32_t from)
		{
			return std::any_of(steps.begin(), steps.end(), [&](const FormatVersion::Step& step) { return step.FileKind == kind && step.From == from; });
		}
	}

	ProjectUpgradeReport ProjectUpgrade::Inspect(const std::filesystem::path& projectFile)
	{
		ProjectUpgradeReport report;
		std::string projectText;
		if (!ReadAll(projectFile, projectText))
		{
			report.Message = std::format("Cannot read the project file '{}'.", projectFile.string());
			return report;
		}

		std::filesystem::path assetDirectory = "Assets";
		try
		{
			const auto project = Yaml::Load(projectText)["Project"];
			if (!project)
				throw std::runtime_error("no Project map");
			assetDirectory = project["AssetDirectory"].as<std::string>("Assets");
		}
		catch (const std::exception& error)
		{
			report.Message = std::format("'{}' is not a Lux project file ({}).", projectFile.string(), error.what());
			return report;
		}

		report.EngineVersion = FormatVersion::RecordedEngine(projectText);
		const std::vector<FormatVersion::Step>& steps = FormatVersion::Steps();

		const uint32_t projectVersion = FormatVersion::Recorded(Kind::Project, projectText);
		if (projectVersion > FormatVersion::Current(Kind::Project))
		{
			report.Result = ProjectUpgradeReport::State::Newer;
			report.Message = FormatVersion::NewerMessage(Kind::Project, projectVersion, projectFile, report.EngineVersion);
			return report;
		}
		if (projectVersion < FormatVersion::Current(Kind::Project) && HasStep(steps, Kind::Project, projectVersion))
			report.Files.push_back({ projectFile, Kind::Project, projectVersion });

		const std::filesystem::path root = assetDirectory.is_absolute() ? assetDirectory : projectFile.parent_path() / assetDirectory;
		std::error_code error;
		if (std::filesystem::is_directory(root, error))
		{
			std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
			for (; !error && iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(error))
			{
				const std::filesystem::directory_entry& entry = *iterator;
				std::error_code entryError;
				if (entry.is_directory(entryError))
				{
					if (IsSkippedFolder(entry.path().filename().string()))
						iterator.disable_recursion_pending();
					continue;
				}

				Kind kind;
				uint32_t version = 0;
				if (!FormatVersion::KindOf(entry.path(), kind) || kind == Kind::Project || !RecordedOf(entry.path(), kind, version))
					continue;

				if (version > FormatVersion::Current(kind))
				{
					report.Result = ProjectUpgradeReport::State::Newer;
					report.Message = FormatVersion::NewerMessage(kind, version, entry.path(), report.EngineVersion);
					report.Files.clear();
					return report;
				}
				if (version < FormatVersion::Current(kind) && HasStep(steps, kind, version))
					report.Files.push_back({ entry.path(), kind, version });
			}
		}

		report.Result = report.Files.empty() ? ProjectUpgradeReport::State::UpToDate : ProjectUpgradeReport::State::NeedsUpgrade;
		return report;
	}

	ProjectUpgrade::Result ProjectUpgrade::Upgrade(const std::filesystem::path& projectFile, const ProjectUpgradeReport& report)
	{
		return Upgrade(projectFile, report, FormatVersion::Steps());
	}

	ProjectUpgrade::Result ProjectUpgrade::Upgrade(const std::filesystem::path& projectFile, const ProjectUpgradeReport& report, const std::vector<FormatVersion::Step>& steps)
	{
		Result result;
		if (report.Files.empty())
		{
			result.Ok = true;
			return result;
		}

		const std::filesystem::path projectDirectory = projectFile.parent_path();
		const std::string stamp = std::format("{:%Y%m%d-%H%M%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
		std::filesystem::path backup = projectDirectory / ".luxbackup" / stamp;
		std::error_code error;
		for (int attempt = 2; std::filesystem::exists(backup, error); attempt++)
			backup = projectDirectory / ".luxbackup" / std::format("{}-{}", stamp, attempt);

		// Every original is copied before anything is changed: a failure while copying leaves the project untouched.
		for (const ProjectUpgradeFile& file : report.Files)
		{
			std::filesystem::path relative = std::filesystem::relative(file.Path, projectDirectory, error);
			if (error || relative.empty() || relative.native().starts_with(std::filesystem::path("..").native()))
				relative = file.Path.filename();
			const std::filesystem::path target = backup / relative;
			std::filesystem::create_directories(target.parent_path(), error);
			if (error)
			{
				result.Error = std::format("Cannot create the backup folder '{}': {}", target.parent_path().string(), error.message());
				return result;
			}
			std::filesystem::copy_file(file.Path, target, std::filesystem::copy_options::overwrite_existing, error);
			if (error)
			{
				result.Error = std::format("Cannot back up '{}': {}", file.Path.string(), error.message());
				return result;
			}
		}
		result.Backup = backup;

		for (const ProjectUpgradeFile& file : report.Files)
		{
			std::string text;
			if (!ReadAll(file.Path, text))
			{
				result.Error = std::format("Cannot read '{}'. The originals are in '{}'.", file.Path.string(), backup.string());
				return result;
			}
			uint32_t reached = file.From;
			const std::string upgraded = FormatVersion::Upgrade(file.FileKind, text, file.From, FormatVersion::Current(file.FileKind), steps, &reached);
			if (upgraded == text)
				continue;
			// Binary: the file keeps its line endings.
			if (!FileSystem::WriteFileAtomically(file.Path, upgraded, true))
			{
				result.Error = std::format("Cannot write '{}'. The originals are in '{}'.", file.Path.string(), backup.string());
				return result;
			}
			result.Upgraded++;
		}

		LUX_CORE_INFO_TAG("Project", "Upgraded {} file(s) of '{}'; the originals are in '{}'", result.Upgraded, projectFile.filename().string(), backup.string());
		result.Ok = true;
		return result;
	}

	ProjectUpgrade::Decision ProjectUpgrade::UnattendedDecision()
	{
		if (FileSystem::HasEnvironmentVariable("LUX_PROJECT_UPGRADE"))
		{
			const std::string value = FileSystem::GetEnvironmentVariable("LUX_PROJECT_UPGRADE");
			return value == "no" || value == "0" ? Decision::No : Decision::Yes;
		}
		if (FileSystem::HasEnvironmentVariable("LUX_NO_WATCHDOG") || FileSystem::HasEnvironmentVariable("LUX_USER_DATA_DIR"))
			return Decision::Yes;
		return Decision::Ask;
	}
}
