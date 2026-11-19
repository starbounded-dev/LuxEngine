// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Thread.h"
#include "Lux/Scripting/ScriptBuilder.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace Lux
{
	// Builds the project's C# scripts for Beam on a thread of its own (docs/BEAM_EDITOR_PLAN.md, Phase 5). The main thread starts it,
	// cancels it and polls for the result once a frame; the build thread touches nothing of the editor, the scene or ImGui.
	class BeamBuildService
	{
	public:
		BeamBuildService();
		~BeamBuildService();

		BeamBuildService(const BeamBuildService&) = delete;
		BeamBuildService& operator=(const BeamBuildService&) = delete;

		// False while a build is already running.
		bool Start(const std::filesystem::path& project, const std::string& configuration);
		void Cancel();
		bool IsRunning() const { return m_Running.load(std::memory_order_acquire); }

		// The last line the compiler printed, for a status text. Safe from the main thread while a build runs.
		std::string GetLatestLine() const;

		// True once, on the frame after a build finishes, with its result.
		bool Poll(ScriptBuildResult& result);

	private:
		void Join();

		Thread m_Thread;
		bool m_HasThread = false;
		std::atomic<bool> m_Running{ false };
		std::atomic<bool> m_Cancel{ false };
		mutable std::mutex m_Mutex;
		std::optional<ScriptBuildResult> m_Finished;
		std::string m_LatestLine;
	};
}
