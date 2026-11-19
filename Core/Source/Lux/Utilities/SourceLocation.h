// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Finding "file and line" in compiler output and log text, so the console and Beam can jump to the source (docs/BEAM_EDITOR_PLAN.md,
// M5). Pure text work, no file access: the caller decides whether the path exists.
namespace Lux
{
	struct SourceLocation
	{
		std::filesystem::path File;
		int Line = 0;   // 1-based
		int Column = 0; // 1-based, 0 when the text gave none

		bool operator==(const SourceLocation&) const = default;
	};

	// The first source location in `text`. Understands the forms the engine's tools print:
	//   C:\game\Source\Player.cs(12,34): error CS1002: ; expected     (MSBuild, csc)
	//   at Game.Player.OnUpdate() in C:\game\Source\Player.cs:line 27   (.NET stack trace)
	//   /home/me/game/Source/Player.cs:12:34: error: ...               (gcc, clang, glslang)
	//   Shaders/Foo.glsl:57                                            (anything path:line)
	// A path needs a file extension, so "10:30:15" and "a:1" are not locations.
	bool FindSourceLocation(std::string_view text, SourceLocation& out);

	enum class DiagnosticSeverity : uint8_t { Info = 0, Warning, Error };

	struct BuildDiagnostic
	{
		SourceLocation Location;
		DiagnosticSeverity Severity = DiagnosticSeverity::Error;
		std::string Code;    // "CS1002", may be empty
		std::string Message; // without the location, the code and a trailing "[project]"
	};

	// One line of MSBuild / csc output in the form "path(line,col): error CODE: message [project]". False for any other line, a line
	// without a location (CS8032 style "error CS0006: Metadata file not found") included: those have no place in a file.
	bool ParseCompilerDiagnostic(std::string_view line, BuildDiagnostic& out);
}
