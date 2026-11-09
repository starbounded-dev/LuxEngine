// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Finds and removes the FMOD-era data an older project still carries: event GUIDs, paths and bank
// names on sources, zones, music directors, surface overrides and tables; dialogue audio-table keys;
// the project's Studio settings; GUID-keyed accessibility entries; and registry entries of the
// legacy AudioProject / AudioBank types. It works on the files' text, line by line, so it opens no
// scene and leaves every other line exactly as it was. Main thread (or any one thread); no engine state.
namespace Lux::AudioLegacyCleanup
{
	enum class FileKind { Asset, Dialogue, Project, Registry };

	struct Finding
	{
		std::filesystem::path File; // as passed to Scan (absolute in the editor)
		uint32_t Line = 0;          // 1-based, in the file as scanned
		std::string Where;          // "AudioSourceComponent.EventGuid", "Audio.StudioProjectPath", ...
		std::string Value;          // unquoted; empty keys are removed but not listed
	};

	struct CleanedFile
	{
		std::filesystem::path Path;
		std::string Text;
		std::string ScannedText;   // the file as scanned; Apply refuses a file that no longer matches it
	};

	struct ScanResult
	{
		std::vector<Finding> Findings;
		std::vector<CleanedFile> Files;  // the files Apply rewrites (also those with only empty keys)
		std::vector<std::string> Errors; // files that could not be read, or cleaned safely
	};

	// One file's text. Fills `cleaned` and appends findings; false, with `error`, when the result would
	// not be valid YAML or a key cannot be removed without restructuring (the first key of a list item).
	bool CleanText(std::string_view text, FileKind kind, std::string& cleaned, std::vector<Finding>& findings, std::string& error);

	// Every scene, prefab, dialogue and surface table under `assetDirectory`, plus the project file and
	// the asset registry. Reads only.
	ScanResult Scan(const std::filesystem::path& projectFile, const std::filesystem::path& assetDirectory, const std::filesystem::path& registryFile);

	// Writes each changed file once, through a temporary file. A file modified since the scan is
	// skipped with an error. True when every file was written.
	bool Apply(const ScanResult& scan, std::vector<std::string>& errors);
}
