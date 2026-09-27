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
//
// Built twice by run.py: once over yaml-cpp (the reference), and once over the engine's Lux::Yaml
// (LUX_ORACLE_LUXYAML), which also offers:
//   --roundtrip   parse, re-write with Yaml::Writer, re-parse, then --check the result
//   --selftest    targeted checks of Lux::Yaml's semantics (no file list needed)

#ifdef LUX_ORACLE_LUXYAML
	#include "Lux/Serialization/Yaml.h"
#else
	#include <yaml-cpp/yaml.h>
#endif

#include <charconv>
#include <cmath>
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

#ifndef LUX_ORACLE_LUXYAML
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

	Value ParseFile(const std::filesystem::path& path) { return FromYamlCpp(YAML::LoadFile(path.string())); }
#else
	// ── Lux::Yaml front end ────────────────────────────────────────────────────────────────
	Value FromLuxYaml(const Lux::Yaml::Node& node)
	{
		Value value;
		switch (node.Type())
		{
		case Lux::Yaml::NodeType::Undefined:
		case Lux::Yaml::NodeType::Null:
			value.Type = Value::Kind::Null;
			break;
		case Lux::Yaml::NodeType::Scalar:
			value.Type = Value::Kind::Scalar;
			value.Text = std::string(node.Scalar());
			break;
		case Lux::Yaml::NodeType::Sequence:
			value.Type = Value::Kind::Sequence;
			for (const auto& item : node)
				value.Items.push_back(FromLuxYaml(item));
			break;
		case Lux::Yaml::NodeType::Map:
			value.Type = Value::Kind::Map;
			for (const auto& entry : node)
				value.Entries.emplace_back(FromLuxYaml(entry.first), FromLuxYaml(entry.second));
			break;
		}
		return value;
	}

	// Re-writes a parsed tree through Yaml::Writer: sequences of scalars inline (as the engine's
	// vectors are), everything else block.
	void WriteNode(Lux::Yaml::Writer& out, const Lux::Yaml::Node& node)
	{
		using namespace Lux;
		switch (node.Type())
		{
		case Yaml::NodeType::Undefined:
		case Yaml::NodeType::Null:
			out << nullptr;
			break;
		case Yaml::NodeType::Scalar:
			out << node.Scalar();
			break;
		case Yaml::NodeType::Sequence:
		{
			bool allScalars = node.size() > 0;
			for (const auto& item : node)
				allScalars &= item.IsScalar();
			if (allScalars)
				out << Yaml::Flow;
			out << Yaml::BeginSeq;
			for (const auto& item : node)
				WriteNode(out, item);
			out << Yaml::EndSeq;
			break;
		}
		case Yaml::NodeType::Map:
			out << Yaml::BeginMap;
			for (const auto& entry : node)
			{
				out << Yaml::Key << entry.first.Scalar() << Yaml::Value;
				WriteNode(out, entry.second);
			}
			out << Yaml::EndMap;
			break;
		}
	}

	bool g_RoundTrip = false;

	Value ParseFile(const std::filesystem::path& path)
	{
		Lux::Yaml::Node document = Lux::Yaml::LoadFile(path);
		if (!g_RoundTrip)
			return FromLuxYaml(document);

		Lux::Yaml::Writer out;
		WriteNode(out, document);
		return FromLuxYaml(Lux::Yaml::Load(out.str(), path.string() + " (re-written)"));
	}

	int SelfTest();
#endif

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
#ifdef LUX_ORACLE_LUXYAML
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--selftest") == 0)
		{
			try
			{
				return SelfTest();
			}
			catch (const std::exception& e)
			{
				std::cerr << "SELFTEST FAIL: uncaught exception: " << e.what() << '\n';
				return 1;
			}
		}
		if (std::strcmp(argv[i], "--roundtrip") == 0)
			g_RoundTrip = true;
	}
#endif
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
			document = ParseFile(root / relative);
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

#ifdef LUX_ORACLE_LUXYAML
namespace
{
	int g_Failures = 0;

	void Expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::cerr << "FAIL " << what << '\n';
			++g_Failures;
		}
	}

	template<typename Fn>
	bool Throws(Fn&& fn)
	{
		try { fn(); }
		catch (const Lux::Yaml::Exception&) { return true; }
		return false;
	}

	int SelfTest()
	{
		using namespace Lux;

		// yaml-cpp semantics the engine relies on.
		const Yaml::Node doc = Yaml::Load("a: 1\nb:\nc: ~\nd: [1, 2, 3]\ne: [1, 2]\n");
		Expect(static_cast<bool>(doc["a"]), "existing key is truthy");
		Expect(static_cast<bool>(doc["b"]) && doc["b"].IsNull(), "empty value: defined and null");
		Expect(doc["c"].IsNull(), "~ is null");
		Expect(!doc["missing"], "missing key is falsy");
		Expect(doc["missing"].as<int>(-1) == -1, "as<T>(fallback) on a missing key");
		Expect(Throws([&] { (void)doc["missing"].as<int>(); }), "as<T>() on a missing key throws");
		Expect(doc["b"].as<std::string>("fb") == "fb", "as<string> on null uses the fallback");
		Expect(!doc["a"]["nested"], "indexing a scalar yields an undefined node");
		Expect(doc["d"].as<glm::vec3>() == glm::vec3(1, 2, 3), "vec3 decode");
		Expect(doc["e"].as<glm::vec3>(glm::vec3(9)) == glm::vec3(9), "vec3 of the wrong size falls back");
		Expect(doc["d"][1].as<int>() == 2 && doc["d"].size() == 3, "sequence index and size");
		{
			const Yaml::Node table = Yaml::Load("T: {2: two, 0: zero}")["T"];
			Expect(table[0].as<std::string>("") == "zero" && table[2].as<std::string>("") == "two", "integer index on a map is a key");
			Expect(!table[1], "absent integer key on a map");
		}

		for (const char* yes : { "y", "Y", "yes", "Yes", "YES", "true", "True", "TRUE", "on", "On", "ON" })
			Expect(Yaml::Load(std::string("v: ") + yes)["v"].as<bool>(false), yes);
		for (const char* no : { "n", "N", "no", "No", "false", "False", "FALSE", "off", "OFF" })
			Expect(!Yaml::Load(std::string("v: ") + no)["v"].as<bool>(true), no);
		Expect(Yaml::Load("v: yEs")["v"].as<bool>(false) == false, "mixed-case is not a bool");

		Expect(Yaml::Load("v: +7")["v"].as<int>() == 7, "leading + integer");
		Expect(Yaml::Load("v: 0x10")["v"].as<int>() == 16, "hex integer");
		Expect(Yaml::Load("v: 1.5")["v"].as<int>(-1) == -1, "float text is not an int");
		Expect(Yaml::Load("v: -1")["v"].as<uint32_t>(7) == 7, "negative is not unsigned");
		Expect(Yaml::Load("v: 18446744073709551615")["v"].as<uint64_t>() == UINT64_MAX, "uint64 max");
		Expect(Yaml::Load("v: 0.00800000038")["v"].as<float>() == 0.008f, "padded float reads as 0.008f");
		Expect(Yaml::Load("v: 1")["v"].as<float>() == 1.0f, "integer text as float");
		Expect(std::isinf(Yaml::Load("v: -.inf")["v"].as<float>()), ".inf");
		Expect(std::isnan(Yaml::Load("v: .nan")["v"].as<double>()), ".nan");
		Expect(Yaml::Load("v: 13186803375098413055")["v"].as<UUID>() == UUID(13186803375098413055ull), "UUID");

		// Iteration: maps yield first/second in document order, sequences yield items.
		{
			std::string keys;
			for (const auto& entry : doc)
				keys += std::string(entry.first.Scalar());
			Expect(keys == "abcde", "map iteration order");
			int sum = 0;
			for (const auto& item : doc["d"])
				sum += item.as<int>();
			Expect(sum == 6, "sequence iteration");
		}

		// Malformed input throws instead of aborting.
		Expect(Throws([] { (void)Yaml::Load("a: [1, 2"); }), "unclosed [ throws");
		Expect(Throws([] { (void)Yaml::Load("a: \"unterminated"); }), "unclosed quote throws");
		Expect(Throws([] { (void)Yaml::LoadFile("does/not/exist.yaml"); }), "missing file throws");

		// Writer: style C, exact round-trip of values, determinism.
		const std::vector<std::string> strings = { "", "~", "null", "Null", "NULL", "a: b", "#hash", " lead", "trail ",
			"multi\nline", "true", "123", "-", "[x]", "{y}", "'q'", "\"dq\"", "tab\there", "unicode é" };
		auto write = [&]
		{
			Yaml::Writer out;
			out << Yaml::BeginMap;
			out << Yaml::Key << "Pos" << Yaml::Value << glm::vec3(1.0f, 2.5f, -3.0f);
			out << Yaml::Key << "Gamma" << Yaml::Value << 2.2f;
			out << Yaml::Key << "Tenth" << Yaml::Value << 0.1f;
			out << Yaml::Key << "Whole" << Yaml::Value << 1.0f;
			out << Yaml::Key << "Big" << Yaml::Value << uint64_t(13186803375098413055ull);
			out << Yaml::Key << "Flag" << Yaml::Value << true;
			out << Yaml::Key << "Nothing" << Yaml::Value << nullptr;
			out << Yaml::Key << "Indices" << Yaml::Value << std::vector<uint32_t>{ 0, 1, 2 };
			out << Yaml::Key << "Strings" << Yaml::Value << Yaml::BeginSeq;
			for (const std::string& text : strings)
				out << text;
			out << Yaml::EndSeq;
			out << Yaml::Key << "Empty" << Yaml::Value << Yaml::BeginSeq << Yaml::EndSeq;
			out << Yaml::EndMap;
			return out.str();
		};

		const std::string text = write();
		Expect(text == write(), "writer output is deterministic");
		Expect(text.find("Pos: [1, 2.5, -3]") != std::string::npos, "vec3 written inline");
		Expect(text.find("Gamma: 2.2\n") != std::string::npos, "shortest float 2.2");
		Expect(text.find("Tenth: 0.1\n") != std::string::npos, "shortest float 0.1");
		Expect(text.find("Whole: 1\n") != std::string::npos, "integral float written as 1");
		if (g_Failures)
			std::cerr << "--- writer output ---\n" << text << "---\n";

		const Yaml::Node back = Yaml::Load(text);
		Expect(back["Pos"].as<glm::vec3>() == glm::vec3(1.0f, 2.5f, -3.0f), "vec3 round-trip");
		Expect(back["Gamma"].as<float>() == 2.2f && back["Tenth"].as<float>() == 0.1f, "float round-trip");
		Expect(back["Big"].as<uint64_t>() == 13186803375098413055ull, "uint64 round-trip");
		Expect(back["Flag"].as<bool>() && back["Nothing"].IsNull(), "bool and null round-trip");
		Expect(back["Indices"].as<std::vector<uint32_t>>() == std::vector<uint32_t>{ 0, 1, 2 }, "vector<uint32_t> round-trip");
		Expect(back["Empty"].IsSequence() && back["Empty"].size() == 0, "empty sequence round-trip");
		for (size_t i = 0; i < strings.size(); ++i)
		{
			const Yaml::Node item = back["Strings"][i];
			const bool ok = item.IsScalar() && item.as<std::string>() == strings[i];
			if (!ok)
				std::cerr << "  string #" << i << " read back as '" << std::string(item.Scalar()) << "'\n";
			Expect(ok, "string round-trip");
		}

		// SetScalar edits a parsed value in place, visible through other Nodes of the document.
		{
			Yaml::Node edited = Yaml::Load("C: {Target: 1, Other: x}");
			Yaml::Node target = edited["C"]["Target"];
			target.SetScalar(uint64_t(13186803375098413055ull));
			Expect(edited["C"]["Target"].as<uint64_t>() == 13186803375098413055ull, "SetScalar value visible");
			Expect(edited["C"]["Other"].as<std::string>() == "x", "SetScalar leaves siblings alone");
			Expect(Throws([&] { edited["C"].SetScalar("nope"); }), "SetScalar on a map throws");
		}

		// Dump() of a subtree re-parses to the same values.
		Expect(Yaml::Load(back["Pos"].Dump()).as<glm::vec3>() == glm::vec3(1.0f, 2.5f, -3.0f), "Dump() round-trip");

		std::cout << (g_Failures ? "SELFTEST FAIL " : "SELFTEST OK ") << g_Failures << " failure(s)\n";
		return g_Failures ? 1 : 0;
	}
}
#endif
