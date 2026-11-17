// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <string_view>

namespace Lux::EditorSearch
{
	// Subsequence fuzzy match with contiguity and word-start bonuses. Returns false when the pattern
	// is not a subsequence of the haystack; otherwise fills `outScore` (higher is better).
	inline bool FuzzyMatch(std::string_view pattern, std::string_view haystack, int& outScore)
	{
		if (pattern.empty())
		{
			outScore = 0;
			return true;
		}

		int score = 0;
		int patternIndex = 0;
		int lastMatch = -2;
		for (int i = 0; i < static_cast<int>(haystack.size()) && patternIndex < static_cast<int>(pattern.size()); i++)
		{
			if (haystack[i] != pattern[patternIndex])
				continue;

			score += (i == lastMatch + 1) ? 6 : 1;                        // contiguous run
			if (i == 0 || haystack[i - 1] == ' ' || haystack[i - 1] == '/')
				score += 4;                                              // start of a word
			lastMatch = i;
			patternIndex++;
		}

		if (patternIndex != static_cast<int>(pattern.size()))
			return false;

		outScore = score - static_cast<int>(haystack.size()) / 8;                     // slight bias toward shorter names
		return true;
	}
}
