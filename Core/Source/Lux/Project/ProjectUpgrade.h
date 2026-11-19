// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Project/FormatVersion.h"

#include <filesystem>
#include <string>
#include <vector>

// Opening a project made by another Lux (docs/PROJECT_FORMAT_PLAN.md): find out whether it is current, older (an upgrade is offered) or newer
// (refused), and upgrade an older one with a backup. Main thread; it reads and writes files, never the scene or the renderer.
namespace Lux
{
	struct ProjectUpgradeFile
	{
		std::filesystem::path Path;
		FormatVersion::Kind FileKind = FormatVersion::Kind::Project;
		uint32_t From = 0; // the version the file records
	};

	struct ProjectUpgradeReport
	{
		enum class State : uint8_t
		{
			UpToDate,     // every file is at this build's version
			NeedsUpgrade, // `Files` would change
			Newer,        // a file was saved by a newer Lux: `Message` says which
			Unreadable,   // no project file, or one that is not YAML
		};

		State Result = State::Unreadable;
		std::string Message;
		std::string EngineVersion;                 // the Lux that last saved the project file, empty for none
		std::vector<ProjectUpgradeFile> Files;     // what an upgrade changes, the project file first
	};

	class ProjectUpgrade
	{
	public:
		// Reads the project file and every versioned text file under its asset directory (skipping Cache, Binaries and the backups).
		static ProjectUpgradeReport Inspect(const std::filesystem::path& projectFile);

		struct Result
		{
			bool Ok = false;
			std::filesystem::path Backup; // the folder holding the originals of every file that changed
			uint32_t Upgraded = 0;
			std::string Error;
		};

		// Copies every file of `report` into <project>/.luxbackup/<date-time>/ (same relative paths), then rewrites each one atomically,
		// walking it through `steps`. A failure stops there and says so; files done before it are valid at their new version.
		static Result Upgrade(const std::filesystem::path& projectFile, const ProjectUpgradeReport& report, const std::vector<FormatVersion::Step>& steps);
		static Result Upgrade(const std::filesystem::path& projectFile, const ProjectUpgradeReport& report);

		// What an unattended run does with an older project: LUX_PROJECT_UPGRADE=yes|no|ask, and yes inside the test harnesses
		// (LUX_NO_WATCHDOG, LUX_USER_DATA_DIR). Ask for a person at the editor.
		enum class Decision : uint8_t { Ask, Yes, No };
		static Decision UnattendedDecision();
	};
}
