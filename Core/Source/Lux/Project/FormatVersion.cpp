// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "FormatVersion.h"

#include "Lux/Serialization/Yaml.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

namespace Lux::FormatVersion
{
	namespace
	{
		struct Format
		{
			const char* Extension;
			Kind FileKind;
			uint32_t Current;
			const char* RootKey; // null: the file stamps `FormatVersion`; else the root map that holds its own `Version`
		};

		constexpr Format k_Formats[] = {
			{ ".luxproj", Kind::Project, 1, nullptr },
			{ ".luxscene", Kind::Scene, 1, nullptr },
			{ ".lprefab", Kind::Scene, 1, nullptr },
			{ ".lmat", Kind::Material, 1, nullptr },
			{ ".lsound", Kind::Sound, 2, "Sound" },
			{ ".lbank", Kind::SoundBank, 1, "SoundBank" },
			{ ".lsheet", Kind::SpriteSheet, 1, "SpriteSheet" },
			{ ".lanimgraph", Kind::AnimationGraph, 1, "AnimationGraph" },
		};

		const Format& FormatOf(Kind kind)
		{
			for (const Format& format : k_Formats)
				if (format.FileKind == kind)
					return format;
			return k_Formats[0];
		}

		const char* KindName(Kind kind)
		{
			switch (kind)
			{
				case Kind::Project: return "project";
				case Kind::Scene: return "scene";
				case Kind::Material: return "material";
				case Kind::Sound: return "sound";
				case Kind::SoundBank: return "sound bank";
				case Kind::SpriteSheet: return "sprite sheet";
				case Kind::AnimationGraph: return "animation graph";
			}
			return "file";
		}

		// The text after `key:` on the first line that starts with it, within the first 2 KB (a stamp is always first).
		std::string_view ValueOf(std::string_view text, std::string_view key)
		{
			text = text.substr(0, 2048);
			if (text.starts_with("\xEF\xBB\xBF"))
				text.remove_prefix(3);
			size_t begin = 0;
			while (begin < text.size())
			{
				size_t end = text.find('\n', begin);
				if (end == std::string_view::npos)
					end = text.size();
				std::string_view line = text.substr(begin, end - begin);
				if (!line.empty() && line.back() == '\r')
					line.remove_suffix(1);
				if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == ':')
				{
					std::string_view value = line.substr(key.size() + 1);
					while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
						value.remove_prefix(1);
					return value;
				}
				begin = end + 1;
			}
			return {};
		}

		std::string Eol(const std::string& text)
		{
			return text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
		}
	}

	uint32_t Current(Kind kind)
	{
		return FormatOf(kind).Current;
	}

	bool KindOf(const std::filesystem::path& path, Kind& kind)
	{
		std::string extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const Format& format : k_Formats)
		{
			if (extension == format.Extension)
			{
				kind = format.FileKind;
				return true;
			}
		}
		return false;
	}

	uint32_t Recorded(Kind kind, std::string_view text)
	{
		const Format& format = FormatOf(kind);
		if (!format.RootKey)
		{
			const std::string_view value = ValueOf(text, Key);
			uint32_t version = 0;
			std::from_chars(value.data(), value.data() + value.size(), version);
			return version;
		}

		// The older kinds keep their number inside the root map; a missing number is the current one, as their loaders read it.
		try
		{
			const auto root = Yaml::Load(std::string(text))[format.RootKey];
			return root ? static_cast<uint32_t>(root["Version"].as<int>(static_cast<int>(format.Current))) : format.Current;
		}
		catch (const std::exception&)
		{
			return format.Current; // not readable at all: the loader says why
		}
	}

	std::string RecordedEngine(std::string_view projectText)
	{
		std::string_view value = ValueOf(projectText, "EngineVersion");
		if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
			value = value.substr(1, value.size() - 2);
		return std::string(value);
	}

	std::string NewerMessage(Kind kind, uint32_t recorded, const std::filesystem::path& file, std::string_view engine)
	{
		const std::string maker = engine.empty() ? std::string("a newer Lux") : std::format("Lux {}", engine);
		const std::string what = file.empty() ? std::format("The {} data", KindName(kind)) : std::format("The {} file '{}'", KindName(kind), file.string());
		return std::format("{} was saved by {} (format version {}). This Lux reads format version {} and older: update Lux to open it.",
			what, maker, recorded, Current(kind));
	}

	bool CheckReadable(Kind kind, uint32_t recorded, const std::filesystem::path& file, std::string_view engine)
	{
		if (recorded <= Current(kind))
			return true;
		LUX_CORE_ERROR_TAG("Project", "{}", NewerMessage(kind, recorded, file, engine));
		return false;
	}

	std::string Stamp(const std::string& text, uint32_t version)
	{
		if (!ValueOf(text, Key).empty())
			return text; // already stamped
		const size_t bom = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
		std::string result = text.substr(0, bom);
		result += std::format("{}: {}{}", Key, version, Eol(text));
		result += text.substr(bom);
		return result;
	}

	const std::vector<Step>& Steps()
	{
		// Version 0 is every file written before 2027.1; version 1 changes no data, it only records the number. The next change of a
		// format adds its step here, beside these.
		static const std::vector<Step> s_Steps = {
			{ Kind::Project, 0, [](const std::string& text) { return Stamp(text, 1); } },
			{ Kind::Scene, 0, [](const std::string& text) { return Stamp(text, 1); } },
			{ Kind::Material, 0, [](const std::string& text) { return Stamp(text, 1); } },
		};
		return s_Steps;
	}

	std::string Upgrade(Kind kind, std::string text, uint32_t from, uint32_t target, const std::vector<Step>& steps, uint32_t* reached)
	{
		uint32_t version = from;
		while (version < target)
		{
			const auto step = std::find_if(steps.begin(), steps.end(), [&](const Step& s) { return s.FileKind == kind && s.From == version; });
			if (step == steps.end())
				break;
			text = step->Apply(text);
			version++;
		}
		if (reached)
			*reached = version;
		return text;
	}
}
