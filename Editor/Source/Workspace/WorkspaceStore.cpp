// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "WorkspaceStore.h"

#include "Lux/Core/Log.h"
#include "Lux/Core/UUID.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Utilities/FileSystem.h"

#include <fstream>
#include <stdexcept>

namespace Lux
{

	namespace
	{
		constexpr size_t k_MaxStoreBytes = 8 * 1024 * 1024;
	}

	std::string WorkspaceStore::Read(const std::filesystem::path& path)
	{
		if (!FileSystem::Exists(path))
			return {};
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file || file.tellg() < 0 || file.tellg() > static_cast<std::streamoff>(k_MaxStoreBytes))
			throw std::runtime_error("Layout file is unreadable or exceeds 8 MiB: " + path.string());
		std::string text(static_cast<size_t>(file.tellg()), '\0');
		file.seekg(0);
		if (!file.read(text.data(), static_cast<std::streamsize>(text.size())))
			throw std::runtime_error("Cannot read layout file: " + path.string());
		return text;
	}

	WorkspaceStore::WorkspaceStore(std::filesystem::path path, std::string expected, bool backupCurrent)
		: m_Path(std::move(path)), m_Expected(std::move(expected)),
		  m_Session(std::to_string(static_cast<uint64_t>(UUID())))
	{
		m_BackupCurrent = backupCurrent;
		m_Thread.Dispatch([this]() { Run(); });
	}

	WorkspaceStore::~WorkspaceStore()
	{
		{
			std::lock_guard lock(m_Mutex);
			m_Stop = true;
		}
		m_Condition.notify_one();
		m_Thread.Join();
	}

	void WorkspaceStore::Queue(std::string text, uint64_t revision)
	{
		{
			std::lock_guard lock(m_Mutex);
			m_Pending = Pending{std::move(text), revision};
			m_Error.clear();
		}
		m_Condition.notify_one();
	}

	std::string WorkspaceStore::GetError() const
	{
		std::lock_guard lock(m_Mutex);
		return m_Error;
	}

	uint64_t WorkspaceStore::GetSavedRevision() const
	{
		std::lock_guard lock(m_Mutex);
		return m_SavedRevision;
	}

	void WorkspaceStore::Write(const std::string& text)
	{
		if (text.size() > k_MaxStoreBytes)
			throw std::runtime_error("Personal layouts exceed the 8 MiB storage limit");
		auto lock = FileSystem::TryLockFile(m_Path.string() + ".lock");
		if (!lock)
			throw std::runtime_error("Layouts are being saved by another editor. Retry after it finishes.");
		const std::string current = Read(m_Path);
		if (current != m_Expected)
		{
			const auto recovery = m_Path.parent_path() / ("Workspaces.conflict." + m_Session + ".yaml");
			if (!FileSystem::WriteFileAtomically(recovery, text, true))
				throw std::runtime_error("Another editor changed layouts; conflict recovery could not be "
										 "written. Keep this editor open.");
			throw std::runtime_error("Another editor changed layouts. This session was preserved in " +
									 recovery.string() + "; reopen the editor to load the latest layouts.");
		}
		const auto backup =
			m_BackupCurrent ? m_Path.string() + ".previous" : m_Path.string() + ".rejected." + m_Session;
		if (!current.empty() && !FileSystem::WriteFileAtomically(backup, current, true))
			throw std::runtime_error("Could not preserve the previous layout file; save cancelled.");
		if (!FileSystem::WriteFileAtomically(m_Path, text, true))
			throw std::runtime_error("Could not save personal layouts. The previous file is unchanged.");
		m_Expected = text;
		m_BackupCurrent = true;
	}

	void WorkspaceStore::Run()
	{
		LUX_PROFILE_THREAD("Workspace Save");
		for (;;)
		{
			std::optional<Pending> pending;
			{
				std::unique_lock lock(m_Mutex);
				m_Condition.wait(lock, [this]() { return m_Stop || m_Pending.has_value(); });
				if (!m_Pending)
					return;
				pending = std::move(m_Pending);
				m_Pending.reset();
			}
			try
			{
				Write(pending->Text);
				std::lock_guard lock(m_Mutex);
				m_SavedRevision = pending->Revision;
				m_Error.clear();
			}
			catch (const std::exception& error)
			{
				{
					std::lock_guard lock(m_Mutex);
					m_Error = error.what();
				}
				LUX_CORE_ERROR_TAG("Editor", "Workspace save: {}", error.what());
			}
		}
	}

} // namespace Lux
