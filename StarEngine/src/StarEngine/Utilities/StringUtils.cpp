#include "sepch.h"
#include "StringUtils.h"

#include "choc/text/choc_StringUtilities.h"

#include <filesystem>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <regex>
#include <vector>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>

namespace StarEngine::Utils {

	namespace String {

		bool EqualsIgnoreCase(const std::string_view a, const std::string_view b)
		{
			if (a.size() != b.size())
				return false;

			return std::equal(a.begin(), a.end(), b.begin(), b.end(),
				[](char ac, char bc)
				{
					return std::tolower(static_cast<unsigned char>(ac)) ==
						std::tolower(static_cast<unsigned char>(bc));
				});
		}

		std::string& ToLower(std::string& string)
		{
			std::transform(string.begin(), string.end(), string.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return string;
		}

		std::string ToLowerCopy(const std::string_view string)
		{
			std::string result(string);
			ToLower(result);
			return result;
		}

		void Erase(std::string& str, const char* chars)
		{
			const size_t len = std::strlen(chars);
			for (size_t i = 0; i < len; i++)
				str.erase(std::remove(str.begin(), str.end(), chars[i]), str.end());
		}

		void Erase(std::string& str, const std::string& chars)
		{
			Erase(str, chars.c_str());
		}

		std::string SubStr(const std::string& string, size_t offset, size_t count)
		{
			if (offset == std::string::npos)
				return string;

			if (offset >= string.length())
				return string;

			return string.substr(offset, count);
		}

		const std::string WHITESPACE = " \n\r\t\f\v";

		std::string TrimWhitespace(const std::string& str)
		{
			size_t start = str.find_first_not_of(WHITESPACE);
			std::string trimmed = (start == std::string::npos) ? "" : str.substr(start);

			size_t end = trimmed.find_last_not_of(WHITESPACE);
			return (end == std::string::npos) ? "" : trimmed.substr(0, end + 1);
		}

		std::string RemoveWhitespace(const std::string& str)
		{
			std::string result = str;
			Erase(result, WHITESPACE);
			return result;
		}

		std::string GetCurrentTimeString(bool includeDate, bool useDashes)
		{
			time_t currentTime = time(nullptr);
			std::stringstream timeString;
			tm* timeBuffer = localtime(&currentTime);
			timeString << std::put_time(timeBuffer, includeDate ? "%Y:%m:%d:%T" : "%T");
			std::string str = timeString.str();

			if (useDashes)
				std::replace(str.begin(), str.end(), ':', '-');

			return str;
		}

		int32_t CompareCase(std::string_view a, std::string_view b)
		{
#ifdef SE_PLATFORM_WINDOWS
			return _stricmp(a.data(), b.data());
#else
			return strcasecmp(a.data(), b.data());
#endif
		}

	} // namespace String

	std::string_view GetFilename(const std::string_view filepath)
	{
		const std::vector<std::string> parts = SplitString(filepath, "/\\");

		if (!parts.empty())
			return parts.back();

		return {};
	}

	std::string GetExtension(const std::string& filename)
	{
		std::vector<std::string> parts = SplitString(filename, '.');

		if (parts.size() > 1)
			return parts.back();

		return "";
	}

	std::string RemoveExtension(const std::string& filename)
	{
		return filename.substr(0, filename.find_last_of('.'));
	}

	std::vector<std::string> SplitStringAndKeepDelims(std::string str)
	{
		static const std::regex re(R"((^\W|^\w+)|(\w+)|[:()])", std::regex_constants::optimize);

		std::regex_iterator<std::string::iterator> rit(str.begin(), str.end(), re);
		std::regex_iterator<std::string::iterator> rend;
		std::vector<std::string> result;

		while (rit != rend)
		{
			result.emplace_back(rit->str());
			++rit;
		}
		return result;
	}

	std::vector<std::string> SplitString(const std::string_view string, const std::string_view& delimiters)
	{
		size_t first = 0;

		std::vector<std::string> result;

		while (first <= string.size())
		{
			const auto second = string.find_first_of(delimiters, first);

			if (first != second)
				result.emplace_back(string.substr(first, second - first));

			if (second == std::string_view::npos)
				break;

			first = second + 1;
		}

		return result;
	}

	std::vector<std::string> SplitString(const std::string_view string, const char delimiter)
	{
		return SplitString(string, std::string(1, delimiter));
	}

	std::string SplitAtUpperCase(std::string_view string, std::string_view delimiter, bool ifLowerCaseOnTheRight /*= true*/)
	{
		std::string str(string);
		for (int i = static_cast<int>(string.size()) - 1; i > 0; --i)
		{
			const auto rightIsLower = [&]
				{
					return i + 1 < static_cast<int>(str.size()) && std::islower(static_cast<unsigned char>(str[i + 1]));
				};

			if (std::isupper(static_cast<unsigned char>(str[i])) && (!ifLowerCaseOnTheRight || rightIsLower()))
				str.insert(i, delimiter);
		}

		return str;
	}

	std::string ToLower(const std::string_view& string)
	{
		std::string result;
		result.reserve(string.size());
		for (const auto& character : string)
			result += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

		return result;
	}

	std::string ToUpper(const std::string_view& string)
	{
		std::string result;
		result.reserve(string.size());
		for (const auto& character : string)
			result += static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

		return result;
	}

	std::string BytesToString(uint64_t bytes)
	{
		constexpr uint64_t GB = 1024ull * 1024ull * 1024ull;
		constexpr uint64_t MB = 1024ull * 1024ull;
		constexpr uint64_t KB = 1024ull;

		char buffer[32 + 1]{};

		if (bytes >= GB)
			std::snprintf(buffer, 32, "%.2f GB", static_cast<double>(bytes) / static_cast<double>(GB));
		else if (bytes >= MB)
			std::snprintf(buffer, 32, "%.2f MB", static_cast<double>(bytes) / static_cast<double>(MB));
		else if (bytes >= KB)
			std::snprintf(buffer, 32, "%.2f KB", static_cast<double>(bytes) / static_cast<double>(KB));
		else
			std::snprintf(buffer, 32, "%.2f bytes", static_cast<double>(bytes));

		return std::string(buffer);
	}

	std::string DurationToString(std::chrono::duration<double> duration)
	{
		const auto durations = BreakDownDuration<std::chrono::minutes, std::chrono::seconds, std::chrono::milliseconds>(duration);

		std::stringstream durSs;
		durSs << std::setfill('0') << std::setw(1) << std::get<0>(durations).count() << ':'
			<< std::setfill('0') << std::setw(2) << std::get<1>(durations).count() << '.'
			<< std::setfill('0') << std::setw(3) << std::get<2>(durations).count();
		return durSs.str();
	}

	std::string TemplateToParenthesis(std::string_view name)
	{
		std::string str(name);

		if (!choc::text::contains(name, "<") || !choc::text::contains(name, ">"))
			return str;

		const auto i = str.find('<');
		if (i > 1 && str[i - 1] != ' ')
			str.insert(i, " ");

		if (i + 2 < str.size())
			str[i + 2] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[i + 2])));

		return choc::text::replace(str, "<", "(", ">", ")");
	}

	std::string CreateUserFriendlyTypeName(std::string_view name)
	{
		return TemplateToParenthesis(SplitAtUpperCase(RemoveNamespace(name)));
	}

	int SkipBOM(std::istream& in)
	{
		char test[4] = { 0 };
		in.seekg(0, std::ios::beg);
		in.read(test, 3);
		if (std::strcmp(test, "\xEF\xBB\xBF") == 0)
		{
			in.seekg(3, std::ios::beg);
			return 3;
		}
		in.seekg(0, std::ios::beg);
		return 0;
	}

	// Returns an empty string when failing.
	std::string ReadFileAndSkipBOM(const std::filesystem::path& filepath)
	{
		std::string result;
		std::ifstream in(filepath, std::ios::in | std::ios::binary);
		if (in)
		{
			in.seekg(0, std::ios::end);
			std::streamoff fileSize = in.tellg();
			const int skippedChars = SkipBOM(in);

			if (fileSize < skippedChars)
				fileSize = 0;
			else
				fileSize -= skippedChars;

			// +1 for the dummy tab at the beginning
			result.resize(static_cast<size_t>(fileSize) + 1);

			// read the file contents after BOM into result[1..]
			if (fileSize > 0)
				in.read(result.data() + 1, fileSize);

			// Add a dummy tab to beginning of file.
			result[0] = '\t';
		}
		in.close();
		return result;
	}

}
