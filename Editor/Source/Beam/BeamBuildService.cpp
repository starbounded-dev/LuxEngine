// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "BeamBuildService.h"

namespace Lux
{
	BeamBuildService::BeamBuildService()
		: m_Thread("BeamBuild")
	{
	}

	BeamBuildService::~BeamBuildService()
	{
		Cancel();
		Join();
	}

	void BeamBuildService::Join()
	{
		if (m_HasThread)
		{
			m_Thread.Join();
			m_HasThread = false;
		}
	}

	bool BeamBuildService::Start(const std::filesystem::path& project, const std::string& configuration)
	{
		if (m_Running.load(std::memory_order_acquire))
			return false;

		Join(); // the previous build's thread, finished
		{
			std::scoped_lock lock(m_Mutex);
			m_Finished.reset();
			m_LatestLine.clear();
		}
		m_Cancel.store(false, std::memory_order_relaxed);
		m_Running.store(true, std::memory_order_release);
		m_HasThread = true;
		m_Thread.Dispatch([this, project, configuration]()
		{
			LUX_PROFILE_THREAD("BeamBuild");
			ScriptBuildResult result = ScriptBuilder::Build(project, configuration, &m_Cancel, [this](std::string_view line)
			{
				if (line.empty())
					return;
				std::scoped_lock lock(m_Mutex);
				m_LatestLine.assign(line);
			});
			{
				std::scoped_lock lock(m_Mutex);
				m_Finished = std::move(result);
			}
			m_Running.store(false, std::memory_order_release);
		});
		return true;
	}

	void BeamBuildService::Cancel()
	{
		m_Cancel.store(true, std::memory_order_relaxed);
	}

	std::string BeamBuildService::GetLatestLine() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_LatestLine;
	}

	bool BeamBuildService::Poll(ScriptBuildResult& result)
	{
		if (m_Running.load(std::memory_order_acquire))
			return false;
		std::scoped_lock lock(m_Mutex);
		if (!m_Finished)
			return false;
		result = std::move(*m_Finished);
		m_Finished.reset();
		return true;
	}
}
