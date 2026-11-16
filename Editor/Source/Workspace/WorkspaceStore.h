// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Thread.h"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace Lux
{

	class WorkspaceStore
	{
	public:
		static std::string Read(const std::filesystem::path& path);
		WorkspaceStore(std::filesystem::path path, std::string expected, bool backupCurrent = true);
		~WorkspaceStore();
		void Queue(std::string text, uint64_t revision);
		std::string GetError() const;
		uint64_t GetSavedRevision() const;

	private:
		void Run();
		void Write(const std::string& text);
		struct Pending
		{
			std::string Text;
			uint64_t Revision;
		};
		std::filesystem::path m_Path;
		std::string m_Expected;
		std::string m_Session;
		Thread m_Thread{"Workspace Save"};
		mutable std::mutex m_Mutex;
		std::condition_variable m_Condition;
		std::optional<Pending> m_Pending;
		std::string m_Error;
		uint64_t m_SavedRevision = 0;
		bool m_Stop = false;
		bool m_BackupCurrent = true;
	};

} // namespace Lux
