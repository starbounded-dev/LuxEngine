// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "BeamSession.h"

#include "Lux/Utilities/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace Lux
{
	namespace
	{
		constexpr const char* k_SessionFile = "session.txt";
		constexpr const char* k_Header = "BEAM-SESSION 1";

		std::string ToUtf8(const std::filesystem::path& path)
		{
			const auto text = path.generic_u8string();
			return std::string(reinterpret_cast<const char*>(text.data()), text.size());
		}

		std::filesystem::path FromUtf8(const std::string& text)
		{
			return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
		}

		// The folder and any parents it needs (the storage folder has no Beam folder yet on a first run).
		bool EnsureDirectory(const std::filesystem::path& directory)
		{
			std::error_code error;
			std::filesystem::create_directories(directory, error);
			return std::filesystem::is_directory(directory, error);
		}

		std::vector<std::string> SplitTabs(const std::string& line)
		{
			std::vector<std::string> parts;
			size_t begin = 0;
			for (size_t end = line.find('\t'); end != std::string::npos; end = line.find('\t', begin))
			{
				parts.push_back(line.substr(begin, end - begin));
				begin = end + 1;
			}
			parts.push_back(line.substr(begin));
			return parts;
		}

		// A backup name is a plain file name made here; anything else in the session file is ignored, so a damaged or hostile file can
		// never name a path outside the session folder.
		bool IsBackupName(const std::string& name)
		{
			return name.starts_with("backup_") && name.ends_with(".txt") && name.find_first_of("/\\") == std::string::npos && name.find("..") == std::string::npos;
		}
	}

	std::filesystem::path BeamSession::DirectoryFor(const std::filesystem::path& projectDirectory)
	{
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(projectDirectory, error).lexically_normal();
		const size_t hash = std::hash<std::string>{}(ToUtf8(error ? projectDirectory : absolute));
		char name[32];
		std::snprintf(name, sizeof(name), "%016zx", hash);
		return FileSystem::GetPersistentStoragePath() / "Beam" / name;
	}

	bool BeamSession::Save(const std::filesystem::path& directory, const BeamSessionData& data)
	{
		if (!EnsureDirectory(directory))
			return false;
		std::string text = std::string(k_Header) + "\nACTIVE\t" + std::to_string(data.Active) + "\n";
		for (const BeamSessionDocument& document : data.Documents)
		{
			text += "DOC\t" + ToUtf8(document.Path) + "\t" + document.BackupFile + "\t" + std::to_string(document.Untitled) + "\t"
				+ std::to_string(document.Line) + "\t" + std::to_string(document.Column) + "\n";
		}
		return FileSystem::WriteFileAtomically(directory / k_SessionFile, text, true);
	}

	bool BeamSession::Load(const std::filesystem::path& directory, BeamSessionData& data)
	{
		std::ifstream file(directory / k_SessionFile, std::ios::binary);
		if (!file)
			return false;
		std::string line;
		if (!std::getline(file, line))
			return false;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line != k_Header)
			return false;

		BeamSessionData loaded;
		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			const std::vector<std::string> parts = SplitTabs(line);
			if (parts.size() == 2 && parts[0] == "ACTIVE")
			{
				loaded.Active = std::atoi(parts[1].c_str());
			}
			else if (parts.size() == 6 && parts[0] == "DOC")
			{
				BeamSessionDocument document;
				document.Path = FromUtf8(parts[1]);
				document.BackupFile = IsBackupName(parts[2]) ? parts[2] : std::string();
				document.Untitled = static_cast<uint32_t>(std::max(0, std::atoi(parts[3].c_str())));
				document.Line = std::max(0, std::atoi(parts[4].c_str()));
				document.Column = std::max(0, std::atoi(parts[5].c_str()));
				loaded.Documents.push_back(std::move(document));
			}
		}
		data = std::move(loaded);
		return true;
	}

	std::string BeamSession::NewBackupName()
	{
		static uint32_t s_Counter = 0;
		const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		return "backup_" + std::to_string(now) + "_" + std::to_string(s_Counter++) + ".txt";
	}

	bool BeamSession::WriteBackup(const std::filesystem::path& directory, const std::string& name, const std::string& text)
	{
		if (!IsBackupName(name) || !EnsureDirectory(directory))
			return false;
		return FileSystem::WriteFileAtomically(directory / name, text, true);
	}

	bool BeamSession::ReadBackup(const std::filesystem::path& directory, const std::string& name, std::string& text)
	{
		if (!IsBackupName(name))
			return false;
		std::ifstream file(directory / name, std::ios::binary);
		if (!file)
			return false;
		std::stringstream contents;
		contents << file.rdbuf();
		text = contents.str();
		return true;
	}

	void BeamSession::DeleteBackup(const std::filesystem::path& directory, const std::string& name)
	{
		if (!IsBackupName(name))
			return;
		std::error_code error;
		std::filesystem::remove(directory / name, error);
	}

	void BeamSession::PruneBackups(const std::filesystem::path& directory, const BeamSessionData& data)
	{
		std::error_code error;
		if (!std::filesystem::is_directory(directory, error))
			return;
		for (const auto& entry : std::filesystem::directory_iterator(directory, error))
		{
			const std::string name = entry.path().filename().string();
			if (!IsBackupName(name))
				continue;
			const bool named = std::any_of(data.Documents.begin(), data.Documents.end(), [&](const BeamSessionDocument& document) { return document.BackupFile == name; });
			if (!named)
				std::filesystem::remove(entry.path(), error);
		}
	}
}
