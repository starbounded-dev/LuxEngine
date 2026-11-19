// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Lux
{
	struct BeamSessionDocument
	{
		std::filesystem::path Path;   // empty: an untitled document
		std::string BackupFile;       // the unsaved text, in the session folder; empty when the document was saved
		uint32_t Untitled = 0;        // "Untitled N"
		int Line = 0, Column = 0;     // 0-based cursor
	};

	struct BeamSessionData
	{
		std::vector<BeamSessionDocument> Documents;
		int Active = -1;
	};

	// What keeps Beam from losing work (docs/BEAM_EDITOR_PLAN.md, Phase 1): the open tabs of a project and a copy of every unsaved
	// buffer, kept in the user's persistent storage (never in the project), so a crash or a quit with unsaved tabs comes back as it was.
	// All calls are main-thread file work on small files.
	class BeamSession
	{
	public:
		// The folder of one project's session and backups.
		static std::filesystem::path DirectoryFor(const std::filesystem::path& projectDirectory);

		static bool Save(const std::filesystem::path& directory, const BeamSessionData& data);
		static bool Load(const std::filesystem::path& directory, BeamSessionData& data);

		static std::string NewBackupName();
		static bool WriteBackup(const std::filesystem::path& directory, const std::string& name, const std::string& text);
		static bool ReadBackup(const std::filesystem::path& directory, const std::string& name, std::string& text);
		static void DeleteBackup(const std::filesystem::path& directory, const std::string& name);
		// Removes backup files the session no longer names.
		static void PruneBackups(const std::filesystem::path& directory, const BeamSessionData& data);
	};
}
