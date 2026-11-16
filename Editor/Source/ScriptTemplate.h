// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <filesystem>

namespace Lux::ScriptTemplate
{
	inline bool IsIdentifier(std::string_view name)
	{
		const auto isLetter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
		if (name.empty() || !isLetter(name.front()))
			return false;
		for (char c : name)
		{
			if (!isLetter(c) && (c < '0' || c > '9'))
				return false;
		}

		static constexpr std::string_view keywords[] = {
			"abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class",
			"const", "continue", "decimal", "default", "delegate", "do", "double", "else", "enum", "event",
			"explicit", "extern", "false", "finally", "fixed", "float", "for", "foreach", "goto", "if",
			"implicit", "in", "int", "interface", "internal", "is", "lock", "long", "namespace", "new",
			"null", "object", "operator", "out", "override", "params", "private", "protected", "public",
			"readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static",
			"string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong",
			"unchecked", "unsafe", "ushort", "using", "virtual", "void", "volatile", "while",
			"__arglist", "__makeref", "__reftype", "__refvalue"
		};
		return std::find(std::begin(keywords), std::end(keywords), name) == std::end(keywords);
	}

	inline bool IsClassName(std::string_view name)
	{
		if (!IsIdentifier(name))
			return false;
		// C# forbids a member with the same name as its enclosing class.
		if (name == "OnCreate" || name == "OnUpdate" || name == "MoveSpeed")
			return false;
		// Class names also become filenames. Keep generated projects portable to Windows.
		std::string lower(name);
		for (char& c : lower)
		{
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}
		return lower != "con" && lower != "prn" && lower != "aux" && lower != "nul"
			&& !(lower.size() == 4 && (lower.starts_with("com") || lower.starts_with("lpt")) && lower[3] >= '1' && lower[3] <= '9');
	}

	inline bool IsNamespace(std::string_view name)
	{
		while (!name.empty())
		{
			const size_t dot = name.find('.');
			if (!IsIdentifier(name.substr(0, dot)))
				return false;
			if (dot == std::string_view::npos)
				return true;
			name.remove_prefix(dot + 1);
		}
		return false;
	}

	inline bool Load(const std::filesystem::path& path, std::string& text, std::string& error)
	{
		text.clear();
		error.clear();
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
		{
			error = "Could not open the C# starter template: " + path.string();
			return false;
		}
		text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		if (stream.bad() || text.empty())
		{
			text.clear();
			error = "Could not read the C# starter template: " + path.string();
			return false;
		}
		return true;
	}

	inline bool Render(std::string_view text, std::string_view className, std::string_view nameSpace,
		std::string& source, std::string& error)
	{
		source.clear();
		error.clear();
		if (!IsClassName(className))
		{
			error = "Enter a C# class name: letters, digits or underscores, starting with a letter or underscore. Keywords, reserved filenames and starter member names (OnCreate, OnUpdate, MoveSpeed) are not allowed.";
			return false;
		}
		if (!IsNamespace(nameSpace))
		{
			error = "The project namespace must contain C# names separated by dots. Change it in Project Settings.";
			return false;
		}
		if (text.find("$CLASS_NAME$") == std::string_view::npos || text.find("$NAMESPACE_NAME$") == std::string_view::npos)
		{
			error = "The C# starter template must contain $CLASS_NAME$ and $NAMESPACE_NAME$.";
			return false;
		}
		source = text;
		const auto replace = [&source](std::string_view token, std::string_view value)
		{
			size_t position = 0;
			while ((position = source.find(token, position)) != std::string::npos)
			{
				source.replace(position, token.size(), value);
				position += value.size();
			}
		};
		replace("$CLASS_NAME$", className);
		replace("$NAMESPACE_NAME$", nameSpace);
		return true;
	}
}
