// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>
#include <string>

namespace Lux {

	// Builds an FMOD Studio project's banks by shelling out to FMOD's command-line tool, the same
	// way ScriptBuilder shells out to dotnet. Banks are build output, not source: the .fspro is what
	// lives in the repo, and the engine consumes only what this produces from it.
	//
	// Building banks requires the FMOD Studio application in addition to the runtime SDK.
	class AudioBankBuilder
	{
	public:
		// Absolute path to fmodstudiocl, or an empty path when it cannot be found. Resolution order:
		// the LUX_FMOD_STUDIO_CL environment variable, then the platform's usual install locations,
		// then PATH. The result is cached for two seconds so UI queries do not scan PATH per frame.
		static std::filesystem::path FindStudioCommandLineTool();

		static bool IsAvailable() { return !FindStudioCommandLineTool().empty(); }

		// True when authored input under the .fspro's directory is newer than the oldest built bank,
		// or when no banks exist yet. Build output and Studio caches/workspace state are excluded.
		// A missing .fspro answers false: there is nothing to build, which
		// is not the same as being out of date.
		static bool NeedsRebuild(const std::filesystem::path& studioProjectPath, const std::filesystem::path& bankDirectory);

		// Runs fmodstudiocl -build -export-guids -platforms <platform>. Blocking, and can take seconds,
		// so callers should not run it inside a frame. Returns false and logs on any failure,
		// including the tool being absent.
		static bool Build(const std::filesystem::path& studioProjectPath, const std::string& platform = "Desktop");

		// Opens a path in the FMOD Studio GUI - a .fspro, or a .bank whose owning project is
		// resolved from it. Returns false if Studio cannot be located.
		static bool OpenInStudio(const std::filesystem::path& path);

	private:
		static std::filesystem::path FindStudioApplication();
	};

}
