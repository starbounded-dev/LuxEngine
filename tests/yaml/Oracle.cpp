// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// YAML value oracle for the yaml-cpp -> rapidyaml migration (docs/YAML_MIGRATION_PLAN.md).
//
// Parses every file in a list, reduces each document to a library-neutral value tree, normalizes
// it, and records a hash per file. A later phase adds a second parser front end; both must produce
// the same hashes, which proves they read identical values.
//
// Normalization:
//   - key order is kept (the engine's writers and readers are order-stable);
//   - integers compare by value ("+007" == "7");
//   - other numbers compare at float precision ("2.20000005" == "2.2"), matching the engine, which
//     reads them as float - so the planned shortest-float reformat is value-neutral;
//   - every other scalar compares as exact text; null (~, null, empty) is one value.
//
// Usage:
//   Oracle --list <files.txt> --root <dir> --write <manifest>   record the reference
//   Oracle --list <files.txt> --root <dir> --check <manifest>   exit 1 on any difference
//   Oracle --list <files.txt> --root <dir> --dump <dir>         one "path = value" line per scalar

#include <yaml-cpp/yaml.h>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	// Library-neutral parsed value. A parser front end fills this; nothing below depends on it.
	struct Value
	{
		enum class Kind { Null, Scalar, Map, Sequence };
		Kind Type = Kind::Null;
		std::string Text;                                     // Scalar
		std::vector<std::pair<Value, Value>> Entries;         // Map, in document order
		std::vector<Value> Items;                             // Sequence
	};

	// ── yaml-cpp front end ─────────────────────────────────────────────────────────────────
	Value FromYamlCpp(const YAML::Node& node)
	{
		Value value;
		switch (node.Type())
		{
		case YAML::NodeType::Null:
		case YAML::NodeType::Undefined:
			value.Type = Value::Kind::Null;
			break;
		case YAML::NodeType::Scalar:
			value.Type = Value::Kind::Scalar;
			value.Text = node.Scalar();
			break;
		case YAML::NodeType::Sequence:
			value.Type = Value::Kind::Sequence;
			for (const auto& item : node)
				value.Items.push_back(FromYamlCpp(item));
			break;
		case YAML::NodeType::Map:
			value.Type = Value::Kind::Map;
			for (const auto& entry : node)
				value.Entries.emplace_back(FromYamlCpp(entry.first), FromYamlCpp(entry.second));
			break;
		}
		return value;
	}

	// ── Normalization ──────────────────────────────────────────────────────────────────────
	bool IsInteger(const std::string& text)
	{
		size_t i = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
		if (i >= text.size())
			return false;
		for (; i < text.size(); ++i)
			if (text[i] < '0' || text[i] > '9')
				return false;
		return true;
	}

	std::string NormalizeInteger(const std::string& text)
	{
		const bool negative = text[0] == '-';
		size_t start = (text[0] == '-' || text[0] == '+') ? 1 : 0;
		while (start + 1 < text.size() && text[start] == '0')
			++start;
		std::string digits = text.substr(start);
		return (negative && digits != "0") ? "-" + digits : digits;
	}

	// Non-integer numbers, reduced to the shortest text that round-trips the float value.
	bool NormalizeFloat(const std::string& text, std::string& out)
	{
		if (text.empty())
			return false;
		double parsed = 0.0;
		const char* first = text.data();
		const char* last = text.data() + text.size();
		if (*first == '+')
			++first;
		const auto result = std::from_chars(first, last, parsed);
		if (result.ec != std::errc() || result.ptr != last)
			return false;

		char buffer[64];
		const auto written = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(parsed));
		out.assign(buffer, written.ptr);
		return true;
	}

	std::string NormalizeScalar(const std::string& text)
	{
		if (text == "~" || text == "null" || text == "Null" || text == "NULL")
			return "~";
		if (IsInteger(text))
			return "i:" + NormalizeInteger(text);
		std::string number;
		if (NormalizeFloat(text, number))
		{
			// "1.0" and "1" are the same value to the engine; fold integral floats into the integer
			// form within float's exact-integer range, so a writer's choice of spelling is neutral.
			if (IsInteger(number) && number.size() <= 8)
				return "i:" + NormalizeInteger(number);
			return "f:" + number;
		}
		return "s" + std::to_string(text.size()) + ":" + text;   // length-prefixed: no escaping needed
	}

	struct Counts
	{
		uint64_t Scalars = 0, Maps = 0, Sequences = 0;
	};

	void Canonical(const Value& value, std::string& out, Counts& counts)
	{
		switch (value.Type)
		{
		case Value::Kind::Null:
			out += "~";
			++counts.Scalars;
			break;
		case Value::Kind::Scalar:
			out += NormalizeScalar(value.Text);
			++counts.Scalars;
			break;
		case Value::Kind::Sequence:
			++counts.Sequences;
			out += '[';
			for (const Value& item : value.Items)
			{
				Canonical(item, out, counts);
				out += ',';
			}
			out += ']';
			break;
		case Value::Kind::Map:
			++counts.Maps;
			out += '{';
			for (const auto& [key, item] : value.Entries)
			{
				Canonical(key, out, counts);
				out += '=';
				Canonical(item, out, counts);
				out += ',';
			}
			out += '}';
			break;
		}
	}

	void Dump(const Value& value, const std::string& path, std::ostream& out)
	{
		switch (value.Type)
		{
		case Value::Kind::Null:
			out << path << " = ~\n";
			break;
		case Value::Kind::Scalar:
			out << path << " = " << NormalizeScalar(value.Text) << '\n';
			break;
		case Value::Kind::Sequence:
			if (value.Items.empty())
				out << path << " = []\n";
			for (size_t i = 0; i < value.Items.size(); ++i)
				Dump(value.Items[i], path + "[" + std::to_string(i) + "]", out);
			break;
		case Value::Kind::Map:
			if (value.Entries.empty())
				out << path << " = {}\n";
			for (const auto& [key, item] : value.Entries)
				Dump(item, path + "." + (key.Type == Value::Kind::Scalar ? key.Text : std::string("?")), out);
			break;
		}
	}

	uint64_t Fnv1a64(const std::string& text)
	{
		uint64_t hash = 14695981039346656037ull;
		for (unsigned char c : text)
		{
			hash ^= c;
			hash *= 1099511628211ull;
		}
		return hash;
	}

	struct FileResult
	{
		std::string Path;
		uint64_t Hash = 0;
		Counts Stats;
	};

	std::string FormatLine(const FileResult& r)
	{
		char hash[17];
		std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(r.Hash));
		std::ostringstream line;
		line << hash << ' ' << r.Stats.Scalars << ' ' << r.Stats.Maps << ' ' << r.Stats.Sequences << ' ' << r.Path;
		return line.str();
	}

	std::string ArgValue(int argc, char** argv, const char* name)
	{
		for (int i = 1; i + 1 < argc; ++i)
			if (std::strcmp(argv[i], name) == 0)
				return argv[i + 1];
		return {};
	}
}

int main(int argc, char** argv)
{
	const std::string listPath = ArgValue(argc, argv, "--list");
	const std::filesystem::path root = ArgValue(argc, argv, "--root");
	const std::string writePath = ArgValue(argc, argv, "--write");
	const std::string checkPath = ArgValue(argc, argv, "--check");
	const std::string dumpDir = ArgValue(argc, argv, "--dump");
	if (listPath.empty() || root.empty() || (writePath.empty() && checkPath.empty() && dumpDir.empty()))
	{
		std::cerr << "usage: Oracle --list <files.txt> --root <dir> (--write <manifest> | --check <manifest> | --dump <dir>)\n";
		return 2;
	}

	std::vector<std::string> files;
	{
		std::ifstream list(listPath);
		for (std::string line; std::getline(list, line);)
			if (!line.empty())
				files.push_back(line);
	}

	std::vector<FileResult> results;
	int failures = 0;
	for (const std::string& relative : files)
	{
		Value document;
		try
		{
			document = FromYamlCpp(YAML::LoadFile((root / relative).string()));
		}
		catch (const std::exception& e)
		{
			std::cerr << "PARSE ERROR " << relative << ": " << e.what() << '\n';
			++failures;
			continue;
		}

		FileResult result;
		result.Path = relative;
		std::string canonical;
		Canonical(document, canonical, result.Stats);
		result.Hash = Fnv1a64(canonical);
		results.push_back(result);

		if (!dumpDir.empty())
		{
			const std::filesystem::path target = std::filesystem::path(dumpDir) / (relative + ".txt");
			std::filesystem::create_directories(target.parent_path());
			std::ofstream out(target);
			Dump(document, "$", out);
		}
	}

	if (!writePath.empty())
	{
		std::ofstream out(writePath);
		out << "# YAML value oracle reference: <fnv1a64 of normalized tree> <scalars> <maps> <sequences> <path>\n";
		out << "# Regenerate only when a data change is intended: python tests/yaml/run.py --write\n";
		for (const FileResult& r : results)
			out << FormatLine(r) << '\n';
		std::cout << "wrote " << results.size() << " entries to " << writePath << '\n';
	}

	if (!checkPath.empty())
	{
		std::map<std::string, std::string> expected;
		std::ifstream in(checkPath);
		for (std::string line; std::getline(in, line);)
		{
			if (line.empty() || line[0] == '#')
				continue;
			const size_t lastSpace = line.find(' ', line.find(' ', line.find(' ', line.find(' ') + 1) + 1) + 1);
			expected[line.substr(lastSpace + 1)] = line;
		}

		for (const FileResult& r : results)
		{
			auto it = expected.find(r.Path);
			const std::string actual = FormatLine(r);
			if (it == expected.end())
			{
				std::cerr << "NEW      " << actual << '\n';
				++failures;
			}
			else if (it->second != actual)
			{
				std::cerr << "MISMATCH expected " << it->second << "\n         actual   " << actual << '\n';
				++failures;
			}
			if (it != expected.end())
				expected.erase(it);
		}
		for (const auto& [path, line] : expected)
		{
			std::cerr << "MISSING  " << line << '\n';
			++failures;
		}
		std::cout << (failures ? "FAIL " : "OK ") << results.size() << " files checked, " << failures << " problem(s)\n";
	}

	return failures ? 1 : 0;
}
