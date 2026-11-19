// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "BeamSearch.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>

namespace Lux
{
	namespace
	{
		constexpr uintmax_t k_MaxFileSize = 2 * 1024 * 1024;
		constexpr size_t k_BinaryProbe = 4096;
		constexpr size_t k_MaxLineText = 240;

		bool IsSkippedDirectory(const std::string& name)
		{
			static const std::array<const char*, 8> skipped = { "bin", "bin-int", "obj", "Cache", "Binaries", "Intermediates", "node_modules", "packages" };
			if (!name.empty() && name[0] == '.')
				return true;
			return std::any_of(skipped.begin(), skipped.end(), [&](const char* entry) { return name == entry; });
		}

		char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
	}

	BeamSearch::BeamSearch()
		: m_Thread("BeamSearch")
	{
	}

	BeamSearch::~BeamSearch()
	{
		Cancel();
		Join();
	}

	void BeamSearch::Join()
	{
		if (m_HasThread)
		{
			m_Thread.Join();
			m_HasThread = false;
		}
	}

	void BeamSearch::Cancel()
	{
		m_Generation.fetch_add(1, std::memory_order_acq_rel); // the running search sees a newer generation and stops
	}

	void BeamSearch::Start(const BeamSearchOptions& options)
	{
		Cancel();
		Join();
		{
			std::scoped_lock lock(m_Mutex);
			m_Pending.clear();
			m_Files = 0;
			m_Truncated = false;
		}
		if (options.Query.empty() || options.Root.empty())
			return;
		const uint64_t generation = m_Generation.load(std::memory_order_acquire);
		m_Running.store(true, std::memory_order_release);
		m_HasThread = true;
		m_Thread.Dispatch([this, options, generation]() { Run(options, generation); });
	}

	void BeamSearch::Poll(std::vector<BeamSearchMatch>& results, size_t& files, bool& truncated)
	{
		std::scoped_lock lock(m_Mutex);
		results.insert(results.end(), std::make_move_iterator(m_Pending.begin()), std::make_move_iterator(m_Pending.end()));
		m_Pending.clear();
		files = m_Files;
		truncated = m_Truncated;
	}

	bool BeamSearch::IsSearchable(const std::filesystem::path& file)
	{
		std::error_code error;
		if (!std::filesystem::is_regular_file(file, error))
			return false;
		if (std::filesystem::file_size(file, error) > k_MaxFileSize || error)
			return false;
		std::ifstream stream(file, std::ios::binary);
		if (!stream)
			return false;
		std::array<char, k_BinaryProbe> probe{};
		stream.read(probe.data(), probe.size());
		return std::find(probe.begin(), probe.begin() + stream.gcount(), '\0') == probe.begin() + stream.gcount();
	}

	bool BeamSearch::FindInLine(std::string_view line, std::string_view query, bool caseSensitive, size_t& column)
	{
		if (query.empty() || line.size() < query.size())
			return false;
		if (caseSensitive)
		{
			const size_t at = line.find(query);
			if (at == std::string_view::npos)
				return false;
			column = at;
			return true;
		}
		const auto found = std::search(line.begin(), line.end(), query.begin(), query.end(), [](char a, char b) { return Lower(a) == Lower(b); });
		if (found == line.end())
			return false;
		column = static_cast<size_t>(found - line.begin());
		return true;
	}

	void BeamSearch::SearchText(const std::filesystem::path& file, std::string_view text, const BeamSearchOptions& options, std::vector<BeamSearchMatch>& out)
	{
		int number = 0;
		size_t begin = 0;
		while (begin <= text.size())
		{
			size_t end = text.find('\n', begin);
			if (end == std::string_view::npos)
				end = text.size();
			std::string_view line = text.substr(begin, end - begin);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);
			++number;

			size_t column = 0;
			if (FindInLine(line, options.Query, options.CaseSensitive, column))
			{
				BeamSearchMatch match;
				match.File = file;
				match.Line = number;
				match.Column = static_cast<int>(column) + 1;
				const size_t first = line.find_first_not_of(" \t");
				std::string_view trimmed = first == std::string_view::npos ? std::string_view() : line.substr(first);
				match.Text = std::string(trimmed.substr(0, k_MaxLineText));
				out.push_back(std::move(match));
			}
			if (end == text.size())
				break;
			begin = end + 1;
		}
	}

	void BeamSearch::Run(BeamSearchOptions options, uint64_t generation)
	{
		LUX_PROFILE_THREAD("BeamSearch");
		const auto stale = [&]() { return m_Generation.load(std::memory_order_acquire) != generation; };

		size_t files = 0, found = 0;
		bool truncated = false;
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator(options.Root, std::filesystem::directory_options::skip_permission_denied, error);
		for (; !error && iterator != std::filesystem::recursive_directory_iterator() && !stale() && !truncated; iterator.increment(error))
		{
			const std::filesystem::directory_entry& entry = *iterator;
			std::error_code entryError;
			if (entry.is_directory(entryError))
			{
				if (IsSkippedDirectory(entry.path().filename().string()))
					iterator.disable_recursion_pending();
				continue;
			}
			if (!IsSearchable(entry.path()))
				continue;

			std::ifstream stream(entry.path(), std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			std::vector<BeamSearchMatch> matches;
			SearchText(entry.path(), text, options, matches);
			++files;
			if (found + matches.size() > MaxMatches)
			{
				matches.resize(MaxMatches - found);
				truncated = true;
			}
			found += matches.size();

			std::scoped_lock lock(m_Mutex);
			if (stale())
				break;
			m_Pending.insert(m_Pending.end(), std::make_move_iterator(matches.begin()), std::make_move_iterator(matches.end()));
			m_Files = files;
			m_Truncated = truncated;
		}
		m_Running.store(false, std::memory_order_release);
	}
}
