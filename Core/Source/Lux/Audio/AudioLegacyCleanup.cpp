// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioLegacyCleanup.h"

#include "Lux/Serialization/Yaml.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace Lux::AudioLegacyCleanup
{
	namespace
	{
		struct Line
		{
			std::string_view Text; // without its line ending
			std::string_view End;  // "\n", "\r\n", or "" on a last line without one
			size_t Indent = 0;     // the key's column (after "- " on a list item)
			bool Dash = false;     // a list item starts on this line
			std::string Key;       // unquoted; empty on blank, comment, scalar and continuation lines
			std::string_view Value;
			bool Removed = false;
			bool EmptyMap = false; // every child was removed: the key gets "{}"
		};

		constexpr std::array<std::string_view, 9> k_EventMaps = { "Event", "AmbienceEvent", "Snapshot", "FootstepOverride", "ImpactOverride", "Footstep", "Impact", "Scrape", "Roll" };
		constexpr std::array<std::string_view, 3> k_EventFields = { "Guid", "Path", "BankName" };
		constexpr std::array<std::string_view, 3> k_SourceFields = { "EventGuid", "EventPath", "EventBank" };
		constexpr std::array<std::string_view, 5> k_ProjectFields = { "StudioProjectPath", "StudioPlatform", "StudioBankOutputPath", "RebuildBanksOnPlay", "EnableLiveUpdate" };
		constexpr std::array<std::string_view, 2> k_ProfileFields = { "StudioPlatform", "BankOutputPath" };

		template<size_t N>
		bool In(std::string_view key, const std::array<std::string_view, N>& list)
		{
			return std::find(list.begin(), list.end(), key) != list.end();
		}

		std::string_view Trim(std::string_view text)
		{
			const size_t first = text.find_first_not_of(' ');
			if (first == std::string_view::npos)
				return {};
			return text.substr(first, text.find_last_not_of(' ') - first + 1);
		}

		std::string Unquote(std::string_view value)
		{
			value = Trim(value);
			if (value.size() < 2 || (value.front() != '\'' && value.front() != '"') || value.back() != value.front())
				return std::string(value);
			std::string inner(value.substr(1, value.size() - 2));
			if (value.front() == '\'')
				for (size_t at = inner.find("''"); at != std::string::npos; at = inner.find("''", at + 1))
					inner.erase(at, 1);
			return inner;
		}

		void Parse(Line& line)
		{
			size_t indent = line.Text.find_first_not_of(' ');
			if (indent == std::string_view::npos)
				return;
			std::string_view rest = line.Text.substr(indent);
			if (rest.front() == '#')
				return;
			if (rest == "-" || rest.starts_with("- "))
			{
				line.Dash = true;
				const size_t content = rest.find_first_not_of(' ', 1);
				line.Indent = indent;
				if (content == std::string_view::npos)
					return;
				indent += content;
				rest = rest.substr(content);
			}
			line.Indent = indent;

			size_t colon = std::string_view::npos;
			if (rest.front() == '\'' || rest.front() == '"')
			{
				const char quote = rest.front();
				size_t close = 1;
				for (; close < rest.size(); ++close)
				{
					if (rest[close] != quote)
						continue;
					if (quote == '\'' && close + 1 < rest.size() && rest[close + 1] == '\'')
					{
						++close;
						continue;
					}
					break;
				}
				if (close + 1 < rest.size() && rest[close + 1] == ':')
					colon = close + 1;
			}
			else
			{
				colon = rest.find(": ");
				if (colon == std::string_view::npos && rest.back() == ':')
					colon = rest.size() - 1;
			}
			if (colon == std::string_view::npos || (colon + 1 < rest.size() && rest[colon + 1] != ' '))
				return;
			line.Key = Unquote(rest.substr(0, colon));
			line.Value = Trim(rest.substr(colon + 1));
		}

		std::vector<Line> Split(std::string_view text)
		{
			std::vector<Line> lines;
			for (size_t start = 0; start < text.size();)
			{
				size_t end = text.find('\n', start);
				const size_t next = end == std::string_view::npos ? text.size() : end + 1;
				if (end == std::string_view::npos)
					end = text.size();
				size_t body = end;
				if (body > start && text[body - 1] == '\r')
					--body;
				Line line;
				line.Text = text.substr(start, body - start);
				line.End = text.substr(body, next - body);
				Parse(line);
				lines.push_back(std::move(line));
				start = next;
			}
			return lines;
		}

		// The lines after `index` that belong to it: indented deeper, up to the next line at its depth.
		size_t BlockEnd(const std::vector<Line>& lines, size_t index, size_t depth)
		{
			size_t end = index + 1;
			for (; end < lines.size(); ++end)
			{
				const Line& line = lines[end];
				const bool blank = Trim(line.Text).empty();
				const size_t column = line.Text.find_first_not_of(' ');
				if (!blank && column <= depth)
					break;
			}
			return end;
		}
	}

	bool CleanText(std::string_view text, FileKind kind, std::string& cleaned, std::vector<Finding>& findings, std::string& error)
	{
		std::vector<Line> lines = Split(text);
		std::vector<Finding> found;
		// The keys enclosing the current line, outermost first: (column, key).
		std::vector<std::pair<size_t, std::string>> parents;
		const auto parent = [&](size_t up) -> std::string_view { return parents.size() > up ? std::string_view(parents[parents.size() - 1 - up].second) : std::string_view(); };
		const auto note = [&](size_t index, std::string where, std::string_view value)
		{
			std::string unquoted = Unquote(value);
			if (!unquoted.empty())
				found.push_back({ {}, static_cast<uint32_t>(index + 1), std::move(where), std::move(unquoted) });
		};

		for (size_t i = 0; i < lines.size(); ++i)
		{
			Line& line = lines[i];
			if (line.Removed || line.Key.empty())
				continue;
			while (!parents.empty() && parents.back().first >= line.Indent)
				parents.pop_back();

			bool remove = false, block = false;
			std::string where;
			if (kind == FileKind::Registry)
			{
				// A legacy entry is a whole list item; drop it from its "- " line.
				if (line.Key == "Type" && (line.Value == "AudioProject" || line.Value == "AudioBank"))
				{
					size_t start = i;
					while (start > 0 && !lines[start].Dash)
						--start;
					const size_t end = BlockEnd(lines, start, lines[start].Text.find_first_not_of(' '));
					std::string file;
					for (size_t j = start; j < end; ++j)
						if (lines[j].Key == "FilePath")
							file = Unquote(lines[j].Value);
					found.push_back({ {}, static_cast<uint32_t>(start + 1), "AssetRegistry." + std::string(line.Value), file.empty() ? "(no path)" : file });
					for (size_t j = start; j < end; ++j)
						lines[j].Removed = true;
				}
				parents.emplace_back(line.Indent, line.Key);
				continue;
			}
			if (kind == FileKind::Project)
			{
				if (parent(0) == "Audio" && In(line.Key, k_ProjectFields))
					remove = true, where = "Audio." + line.Key;
				else if ((parent(0) == "Windows" || parent(0) == "Linux") && parent(1) == "Audio" && In(line.Key, k_ProfileFields))
					remove = true, where = "Audio." + std::string(parent(0)) + "." + line.Key;
				else if (parent(0) == "Events" && parent(1) == "Accessibility" && line.Key.starts_with('{'))
					remove = block = true, where = "Audio.Accessibility.Events";
				// Project values are settings: listed even when empty.
				if (remove)
					found.push_back({ {}, static_cast<uint32_t>(i + 1), where, block ? line.Key : (line.Value.empty() ? "(empty)" : Unquote(line.Value)) });
			}
			else
			{
				if (parent(0) == "AudioSourceComponent" && In(line.Key, k_SourceFields))
					remove = true, where = "AudioSourceComponent." + line.Key;
				else if (In(parent(0), k_EventMaps) && In(line.Key, k_EventFields))
					remove = true, where = std::string(parent(0)) + "." + line.Key;
				else if (kind == FileKind::Dialogue && line.Key == "AudioKey")
					remove = true, where = "Dialogue.AudioKey";
				if (remove)
					note(i, where, line.Value);
			}

			if (!remove)
			{
				parents.emplace_back(line.Indent, line.Key);
				continue;
			}
			if (line.Dash && !block)
			{
				error = "line " + std::to_string(i + 1) + ": " + line.Key + " starts a list item, so it cannot be removed on its own";
				return false;
			}
			const size_t end = block ? BlockEnd(lines, i, line.Text.find_first_not_of(' ')) : i + 1;
			for (size_t j = i; j < end; ++j)
				lines[j].Removed = true;
		}

		// A reference map left with no keys becomes "{}", so it still reads as an (empty) map.
		for (size_t i = 0; i < lines.size(); ++i)
		{
			Line& line = lines[i];
			if (line.Removed || !line.Value.empty() || !In(std::string_view(line.Key), k_EventMaps))
				continue;
			const size_t end = BlockEnd(lines, i, line.Indent);
			bool removedAny = false, keptAny = false;
			for (size_t j = i + 1; j < end; ++j)
			{
				if (Trim(lines[j].Text).empty())
					continue;
				(lines[j].Removed ? removedAny : keptAny) = true;
			}
			line.EmptyMap = removedAny && !keptAny;
		}

		std::string result;
		result.reserve(text.size());
		for (const Line& line : lines)
		{
			if (line.Removed)
				continue;
			result += line.Text;
			if (line.EmptyMap)
				result += " {}";
			result += line.End;
		}

		try
		{
			Yaml::Load(result, "cleaned file");
		}
		catch (const std::exception& exception)
		{
			error = std::string("the cleaned file would not parse: ") + exception.what();
			return false;
		}
		cleaned = std::move(result);
		findings.insert(findings.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
		return true;
	}

	ScanResult Scan(const std::filesystem::path& projectFile, const std::filesystem::path& assetDirectory, const std::filesystem::path& registryFile)
	{
		ScanResult scan;
		const auto visit = [&](const std::filesystem::path& path, FileKind kind)
		{
			std::error_code ec;
			const auto writeTime = std::filesystem::last_write_time(path, ec);
			std::ifstream stream(path, std::ios::binary);
			if (ec || !stream)
			{
				scan.Errors.push_back(path.generic_string() + ": could not be read");
				return;
			}
			std::ostringstream contents;
			contents << stream.rdbuf();
			const std::string text = std::move(contents).str();

			std::string cleaned, error;
			std::vector<Finding> findings;
			if (!CleanText(text, kind, cleaned, findings, error))
			{
				scan.Errors.push_back(path.generic_string() + ": " + error);
				return;
			}
			if (cleaned == text)
				return;
			for (Finding& finding : findings)
			{
				finding.File = path;
				scan.Findings.push_back(std::move(finding));
			}
			scan.Files.push_back({ path, std::move(cleaned), writeTime });
		};

		if (std::filesystem::exists(projectFile))
			visit(projectFile, FileKind::Project);
		if (std::filesystem::exists(registryFile))
			visit(registryFile, FileKind::Registry);

		std::error_code ec;
		std::vector<std::filesystem::path> assets;
		for (auto it = std::filesystem::recursive_directory_iterator(assetDirectory, std::filesystem::directory_options::skip_permission_denied, ec);
			!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			const std::string extension = it->path().extension().string();
			if (it->is_regular_file() && (extension == ".luxscene" || extension == ".lprefab" || extension == ".lsurfaces" || extension == ".ldialogue"))
				assets.push_back(it->path());
		}
		if (ec)
			scan.Errors.push_back(assetDirectory.generic_string() + ": " + ec.message());
		std::sort(assets.begin(), assets.end());
		for (const auto& path : assets)
			visit(path, path.extension() == ".ldialogue" ? FileKind::Dialogue : FileKind::Asset);
		return scan;
	}

	bool Apply(const ScanResult& scan, std::vector<std::string>& errors)
	{
		bool ok = true;
		for (const CleanedFile& file : scan.Files)
		{
			std::error_code ec;
			if (std::filesystem::last_write_time(file.Path, ec) != file.ScannedWriteTime || ec)
			{
				errors.push_back(file.Path.generic_string() + ": changed since the scan; scan again");
				ok = false;
				continue;
			}
			std::filesystem::path temporary = file.Path;
			temporary += ".tmp";
			{
				std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
				stream.write(file.Text.data(), static_cast<std::streamsize>(file.Text.size()));
				if (!stream.flush())
				{
					errors.push_back(temporary.generic_string() + ": could not be written");
					ok = false;
					continue;
				}
			}
			std::filesystem::rename(temporary, file.Path, ec);
			if (ec)
			{
				std::filesystem::remove(temporary, ec);
				errors.push_back(file.Path.generic_string() + ": could not be replaced");
				ok = false;
			}
		}
		return ok;
	}
}
