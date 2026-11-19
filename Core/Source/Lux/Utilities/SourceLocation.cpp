// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SourceLocation.h"

#include <algorithm>
#include <cctype>

namespace Lux
{
	namespace
	{
		bool IsDigit(char c) { return c >= '0' && c <= '9'; }
		bool IsPathSeparator(char c) { return c == '/' || c == '\\'; }

		// Reads digits at `at`; false when there are none or the number does not fit.
		bool ReadNumber(std::string_view text, size_t& at, int& value)
		{
			if (at >= text.size() || !IsDigit(text[at]))
				return false;
			long long number = 0;
			while (at < text.size() && IsDigit(text[at]))
			{
				number = number * 10 + (text[at] - '0');
				if (number > 100000000)
					return false;
				++at;
			}
			value = static_cast<int>(number);
			return true;
		}

		// "ab.cs" before `end`: 1-8 letters or digits, a dot, and a name character before it.
		bool EndsWithExtension(std::string_view text, size_t end)
		{
			size_t at = end;
			size_t length = 0;
			bool hasLetter = false;
			while (at > 0 && std::isalnum(static_cast<unsigned char>(text[at - 1])))
			{
				--at;
				++length;
				hasLetter |= std::isalpha(static_cast<unsigned char>(text[at])) != 0; // "2.5" is a number, not a file
			}
			if (length < 1 || length > 8 || !hasLetter || at < 2 || text[at - 1] != '.')
				return false;
			const char before = text[at - 2];
			return std::isalnum(static_cast<unsigned char>(before)) || before == '_' || before == '-';
		}

		// Where the path that ends at `end` starts. A path may hold spaces ("Program Files"), so this looks for an absolute-path start
		// (a drive letter, a leading slash) and falls back to the last word.
		size_t PathStart(std::string_view text, size_t end)
		{
			// Never cross a quote, a bracket or a line break.
			size_t floor = 0;
			for (size_t i = end; i > 0; --i)
			{
				const char c = text[i - 1];
				if (c == '"' || c == '\'' || c == '[' || c == '<' || c == '\n' || c == '\r' || c == '\t' || c == '(')
				{
					floor = i;
					break;
				}
			}

			size_t drive = std::string_view::npos;
			for (size_t i = floor; i + 2 < end; ++i)
			{
				if (std::isalpha(static_cast<unsigned char>(text[i])) && text[i + 1] == ':' && IsPathSeparator(text[i + 2])
					&& (i == floor || !std::isalnum(static_cast<unsigned char>(text[i - 1]))))
				{
					drive = i;
					break;
				}
			}
			if (drive != std::string_view::npos)
				return drive;

			// POSIX absolute path: a slash that starts a word, taking the earliest, so "/home/my dir/a.cs" keeps its space.
			for (size_t i = floor; i < end; ++i)
			{
				if (text[i] == '/' && (i == floor || text[i - 1] == ' '))
					return i;
			}

			// A relative path is the last word.
			size_t start = end;
			while (start > floor && text[start - 1] != ' ')
				--start;
			return start;
		}
	}

	bool FindSourceLocation(std::string_view text, SourceLocation& out)
	{
		for (size_t i = 1; i < text.size(); ++i)
		{
			const char c = text[i];
			if (c != '(' && c != ':')
				continue;

			size_t at = i + 1;
			if (c == ':' && text.substr(at, 5) == "line ")
				at += 5; // ".NET stack trace: path:line 27"
			int line = 0, column = 0;
			if (!ReadNumber(text, at, line) || line <= 0 || !EndsWithExtension(text, i))
				continue;

			if (c == '(')
			{
				// (line) or (line,col): the closing bracket decides
				if (at < text.size() && text[at] == ',')
				{
					++at;
					if (!ReadNumber(text, at, column))
						continue;
				}
				if (at >= text.size() || text[at] != ')')
					continue;
			}
			else if (at < text.size() && text[at] == ':')
			{
				size_t next = at + 1;
				if (ReadNumber(text, next, column))
					at = next;
			}

			const size_t start = PathStart(text, i);
			if (start >= i)
				continue;
			out.File = std::filesystem::path(std::string(text.substr(start, i - start))).lexically_normal();
			out.Line = line;
			out.Column = column;
			return true;
		}
		return false;
	}

	bool ParseCompilerDiagnostic(std::string_view line, BuildDiagnostic& out)
	{
		// "path(12,34): error CS1002: message [project]"
		const size_t colon = line.find("): ");
		if (colon == std::string_view::npos)
			return false;

		SourceLocation location;
		if (!FindSourceLocation(line.substr(0, colon + 1), location) || location.Line <= 0)
			return false;

		std::string_view rest = line.substr(colon + 3);
		DiagnosticSeverity severity;
		if (rest.starts_with("error"))
		{
			severity = DiagnosticSeverity::Error;
			rest.remove_prefix(5);
		}
		else if (rest.starts_with("warning"))
		{
			severity = DiagnosticSeverity::Warning;
			rest.remove_prefix(7);
		}
		else if (rest.starts_with("info"))
		{
			severity = DiagnosticSeverity::Info;
			rest.remove_prefix(4);
		}
		else
		{
			return false;
		}

		BuildDiagnostic diagnostic;
		diagnostic.Location = std::move(location);
		diagnostic.Severity = severity;
		// " CS1002: message"
		while (!rest.empty() && rest.front() == ' ')
			rest.remove_prefix(1);
		const size_t codeEnd = rest.find(':');
		if (codeEnd != std::string_view::npos && codeEnd <= 12 && rest.find(' ') > codeEnd)
		{
			diagnostic.Code = std::string(rest.substr(0, codeEnd));
			rest.remove_prefix(codeEnd + 1);
			while (!rest.empty() && rest.front() == ' ')
				rest.remove_prefix(1);
		}

		// The trailing " [C:\\game\\Scripts.csproj]" names the project, not the problem.
		while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\r'))
			rest.remove_suffix(1);
		if (!rest.empty() && rest.back() == ']')
		{
			const size_t open = rest.rfind(" [");
			if (open != std::string_view::npos)
				rest = rest.substr(0, open);
		}
		diagnostic.Message = std::string(rest);
		out = std::move(diagnostic);
		return true;
	}
}
