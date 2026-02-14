#include "lpch.h"
#include "StringUtils.h"

namespace Lux::Utils
{
	std::string BytesToString(uint64_t bytes)
	{
		constexpr uint64_t GB = 1024 * 1024 * 1024;
		constexpr uint64_t MB = 1024 * 1024;
		constexpr uint64_t KB = 1024;

		char buffer[32 + 1]{};

		if (bytes >= GB)
			snprintf(buffer, 32, "%.2f GB", (float)bytes / (float)GB);
		else if (bytes >= MB)
			snprintf(buffer, 32, "%.2f MB", (float)bytes / (float)MB);
		else if (bytes >= KB)
			snprintf(buffer, 32, "%.2f KB", (float)bytes / (float)KB);
		else
			snprintf(buffer, 32, "%.2f bytes", (double)bytes);

		return std::string(buffer);
	}

	std::vector<std::string> SplitStringAndKeepDelims(std::string_view str)
	{
		std::vector<std::string> result;
		std::string current;

		for (char c : str)
		{
			if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
			{
				if (!current.empty())
				{
					result.push_back(current);
					current.clear();
				}
				result.push_back(std::string(1, c));
			}
			else
			{
				current += c;
			}
		}

		if (!current.empty())
		{
			result.push_back(current);
		}

		return result;
	}
}
