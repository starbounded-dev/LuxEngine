// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Thread.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Lux
{
	struct BeamSearchMatch
	{
		std::filesystem::path File;
		int Line = 0;     // 1-based
		int Column = 0;   // 1-based
		std::string Text; // the line, trimmed
	};

	struct BeamSearchOptions
	{
		std::string Query;
		bool CaseSensitive = false;
		std::filesystem::path Root;
	};

	// Find in files for Beam (docs/BEAM_EDITOR_PLAN.md, Phase 3), on a thread of its own so typing and drawing never wait for the disk.
	// Plain text matching: the first match per line, text files only, a few folders skipped. The main thread starts it, polls the
	// results in batches and cancels it when the query changes; the thread touches nothing of the editor.
	class BeamSearch
	{
	public:
		static constexpr size_t MaxMatches = 2000;

		BeamSearch();
		~BeamSearch();

		BeamSearch(const BeamSearch&) = delete;
		BeamSearch& operator=(const BeamSearch&) = delete;

		// Cancels a search in progress and starts this one.
		void Start(const BeamSearchOptions& options);
		void Cancel();
		bool IsRunning() const { return m_Running.load(std::memory_order_acquire); }

		// Appends the matches found since the last call. `files` and `truncated` describe the search so far.
		void Poll(std::vector<BeamSearchMatch>& results, size_t& files, bool& truncated);

		// The pure parts, shared with the checks.
		static bool IsSearchable(const std::filesystem::path& file);
		static bool FindInLine(std::string_view line, std::string_view query, bool caseSensitive, size_t& column);
		static void SearchText(const std::filesystem::path& file, std::string_view text, const BeamSearchOptions& options, std::vector<BeamSearchMatch>& out);

	private:
		void Join();
		void Run(BeamSearchOptions options, uint64_t generation);

		Thread m_Thread;
		bool m_HasThread = false;
		std::atomic<bool> m_Running{ false };
		std::atomic<uint64_t> m_Generation{ 0 };
		mutable std::mutex m_Mutex;
		std::vector<BeamSearchMatch> m_Pending;
		size_t m_Files = 0;
		bool m_Truncated = false;
	};
}
