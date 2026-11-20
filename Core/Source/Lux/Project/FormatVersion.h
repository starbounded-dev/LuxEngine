// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Format versions of the text files a project is made of (docs/PROJECT_FORMAT_PLAN.md). Pure code: no registry, no GPU, any thread.
//
// Project, scene (and prefab) and material files carry `FormatVersion: N` as their first top-level key. A file without it was written
// before 2027.1 and is version 0; version 1 is what 2027.1 writes and changes no data, so both read the same way. Sound, sound bank,
// sprite sheet and animation graph files already carry their own `Version` inside their root map; they take part here with that number.
// A file newer than this build knows is refused, never half-read.
namespace Lux::FormatVersion
{
	enum class Kind : uint8_t
	{
		Project = 0,    // .luxproj
		Scene,          // .luxscene and .lprefab (a prefab is a scene file)
		Material,       // .lmat
		Sound,          // .lsound
		SoundBank,      // .lbank
		SpriteSheet,    // .lsheet
		AnimationGraph, // .lanimgraph
	};

	inline constexpr const char* Key = "FormatVersion";

	// The version this build writes (and the newest it reads) for a kind.
	uint32_t Current(Kind kind);

	// The kind of file a path is, by extension, or false for any other file.
	bool KindOf(const std::filesystem::path& path, Kind& kind);

	// The version a file's text records; 0 for none. Cheap for the kinds that stamp the key (only the start of the text is read).
	uint32_t Recorded(Kind kind, std::string_view text);

	// The Lux that wrote a project file, from its `EngineVersion` (empty for none). For messages only: nothing decides by it.
	std::string RecordedEngine(std::string_view projectText);

	// True when the recorded version is one this build reads. Otherwise logs a clear error naming the file and returns false.
	bool CheckReadable(Kind kind, uint32_t recorded, const std::filesystem::path& file, std::string_view engine = {});

	// The message CheckReadable logs, for a dialog.
	std::string NewerMessage(Kind kind, uint32_t recorded, const std::filesystem::path& file, std::string_view engine);

	// An upgrade step: the text of a file at version `From`, as it is at From + 1.
	struct Step
	{
		Kind FileKind;
		uint32_t From;
		std::string (*Apply)(const std::string& text);
	};

	// The steps this build knows, in order.
	const std::vector<Step>& Steps();

	// Walks `text` through the steps of `steps` from `from` toward `target`, stopping where a step is missing. Returns the new text;
	// `reached` (if given) is the version it got to.
	std::string Upgrade(Kind kind, std::string text, uint32_t from, uint32_t target, const std::vector<Step>& steps, uint32_t* reached = nullptr);

	// Version 0 to 1: the `FormatVersion` line, first, in the file's own line endings.
	std::string Stamp(const std::string& text, uint32_t version);
}
