// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// Lux::Yaml — the engine's YAML API, over rapidyaml (Core/vendor/rapidyaml).
//
// Shaped like the subset of yaml-cpp the engine used, so serializers port mechanically:
//   reading:  Yaml::Load / LoadFile -> Node; node["Key"], node[0], as<T>() / as<T>(fallback),
//             IsMap/IsSequence/IsScalar/IsNull, iteration (maps yield .first/.second).
//   writing:  Yaml::Writer with the BeginMap/EndMap/BeginSeq/EndSeq/Key/Value/Flow manipulators.
//
// Semantics kept from yaml-cpp on purpose (callers rely on them):
//   - `if (auto n = node["Key"])` is true when the key exists, even with an empty (null) value.
//   - a missing key yields an undefined Node and never allocates; as<T>() on it throws.
//   - as<bool> accepts y/n, yes/no, true/false, on/off, in lower, UPPER or Capitalized case.
//
// Output style (docs/YAML_MIGRATION_PLAN.md, decision "C"): block maps and sequences, inline
// [x, y, z] for glm vectors and after the Flow manipulator, floats in the shortest form that reads
// back to the same value. Output is deterministic, which the undo snapshot diff depends on.
//
// rapidyaml itself stays out of this header (it is 1.7 MB); only Yaml.cpp includes it.
//
// Threading: every Node/Writer is independent. A Node keeps its parsed document alive, so nodes
// may be copied freely and outlive the Load call. rapidyaml's default error handlers abort() on
// malformed input; Load and Writer install throwing ones first (std::call_once, so the first use on
// any thread is safe).

#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"
#include "Lux/Core/UUID.h"

#include <glm/glm.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace c4::yml { class Tree; }

namespace Lux::Yaml {

	// Thrown for malformed YAML (with the line, when known) and by as<T>() when a node is missing
	// or does not convert to T.
	class Exception : public std::runtime_error
	{
	public:
		explicit Exception(const std::string& message) : std::runtime_error(message) {}
	};

	// Routes rapidyaml's error callbacks to Yaml::Exception. Idempotent and thread-safe; Load and
	// Writer call it, so callers never need to.
	void InstallErrorHandlers();

	enum class NodeType { Undefined, Null, Scalar, Sequence, Map };

	namespace Detail {
		// char / signed char / unsigned char (uint8_t, int8_t). yaml-cpp wrote these as a character, not
		// a number, so they get their own conversion and the Writer refuses them (cast explicitly).
		template<typename T>
		inline constexpr bool IsByte = std::is_same_v<T, char> || std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char>;
	}

	template<typename T>
	struct Convert;   // static bool Decode(const Node& node, T& out) — false when it does not convert

	namespace Detail {
		// A parsed document: the text (rapidyaml parses in place, so scalars point into it) and its
		// tree. Nodes hold a Ref to it, so it lives as long as any Node from it.
		struct Document : public RefCounted
		{
			Document();
			~Document() override;

			std::string Buffer;
			Scope<c4::yml::Tree> Tree;
		};
	}

	class Node;
	Node Load(std::string text, std::string_view name = {});
	Node LoadFile(const std::filesystem::path& path);

	class Node
	{
	public:
		struct IteratorValue;
		class Iterator;

		Node() = default;

		bool IsDefined() const { return m_Document && m_Id != s_InvalidId; }
		explicit operator bool() const { return IsDefined(); }

		NodeType Type() const;
		bool IsNull() const { return Type() == NodeType::Null; }
		bool IsScalar() const { return Type() == NodeType::Scalar; }
		bool IsSequence() const { return Type() == NodeType::Sequence; }
		bool IsMap() const { return Type() == NodeType::Map; }

		// Children of a map or sequence; 0 otherwise.
		size_t size() const;

		// The scalar text; empty for anything but a scalar. Valid while this Node (or a copy) lives.
		std::string_view Scalar() const;

		// Undefined Node when the key/index is absent or this is not a map/sequence. On a map an
		// integer index is a key (node[0] finds `0:`), as in yaml-cpp.
		Node operator[](std::string_view key) const;
		Node operator[](const char* key) const { return (*this)[std::string_view(key)]; }
		Node operator[](const std::string& key) const { return (*this)[std::string_view(key)]; }
		Node operator[](size_t index) const;
		Node operator[](int index) const { return index < 0 ? Node{} : (*this)[static_cast<size_t>(index)]; }

		template<typename T>
		T as() const
		{
			T value{};
			if (!IsDefined())
				throw Exception("YAML: value requested from a missing node");
			if (!Convert<T>::Decode(*this, value))
				throw Exception("YAML: could not convert '" + std::string(Scalar()) + "'");
			return value;
		}

		template<typename T>
		T as(const T& fallback) const
		{
			T value{};
			if (IsDefined() && Convert<T>::Decode(*this, value))
				return value;
			return fallback;
		}

		// This subtree as YAML text (e.g. to compare two subtrees).
		std::string Dump() const;

		// Replaces this node's value, which must exist and be a scalar or null — the one in-place
		// edit Lux::Yaml supports, for normalizing a parsed document before comparing it. Visible
		// through every Node of the same document. Throws Yaml::Exception otherwise.
		void SetScalar(std::string_view text);

		template<typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
		void SetScalar(T value)
		{
			char buffer[32];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			SetScalar(std::string_view(buffer, static_cast<size_t>(result.ptr - buffer)));
		}

		Iterator begin() const;
		Iterator end() const;

	private:
		static constexpr size_t s_InvalidId = std::numeric_limits<size_t>::max();

		Node(Ref<Detail::Document> document, size_t id, bool isKey)
			: m_Document(std::move(document)), m_Id(id), m_IsKey(isKey) {}

		const c4::yml::Tree& GetTree() const;

		Ref<Detail::Document> m_Document;
		size_t m_Id = s_InvalidId;
		bool m_IsKey = false;   // a map entry's key, viewed as a scalar node

		friend Node Load(std::string text, std::string_view name);
	};

	// What iterating a Node yields: for a sequence the element itself; for a map, `first` (the key)
	// and `second` (the value) — the same shape yaml-cpp's iterators had.
	struct Node::IteratorValue : Node
	{
		Node first;
		Node second;
	};

	class Node::Iterator
	{
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = IteratorValue;
		using difference_type = std::ptrdiff_t;
		using pointer = const IteratorValue*;
		using reference = const IteratorValue&;

		Iterator() = default;
		Iterator(Node parent, size_t child) : m_Parent(std::move(parent)), m_Child(child) { Load(); }

		reference operator*() const { return m_Value; }
		pointer operator->() const { return &m_Value; }
		Iterator& operator++();
		Iterator operator++(int) { Iterator copy = *this; ++(*this); return copy; }
		bool operator==(const Iterator& other) const { return m_Child == other.m_Child; }
		bool operator!=(const Iterator& other) const { return m_Child != other.m_Child; }

	private:
		void Load();

		Node m_Parent;
		size_t m_Child = s_InvalidId;
		IteratorValue m_Value;
	};

	// Load(text, name) parses YAML text (the Node keeps the document alive); `name` appears in error
	// messages. LoadFile reads then parses. Both throw Yaml::Exception on malformed input or I/O
	// failure. (Declared above Node, which befriends Load.)

	// ── Writing ─────────────────────────────────────────────────────────────────────────────────

	enum class Manipulator { BeginMap, EndMap, BeginSeq, EndSeq, Key, Value, Flow };
	inline constexpr Manipulator BeginMap = Manipulator::BeginMap;
	inline constexpr Manipulator EndMap = Manipulator::EndMap;
	inline constexpr Manipulator BeginSeq = Manipulator::BeginSeq;
	inline constexpr Manipulator EndSeq = Manipulator::EndSeq;
	inline constexpr Manipulator Key = Manipulator::Key;
	inline constexpr Manipulator Value = Manipulator::Value;
	inline constexpr Manipulator Flow = Manipulator::Flow;   // the next map/sequence is written inline

	class Writer
	{
	public:
		Writer();
		~Writer();
		Writer(const Writer&) = delete;
		Writer& operator=(const Writer&) = delete;

		Writer& operator<<(Manipulator manipulator);

		Writer& operator<<(std::string_view text);
		Writer& operator<<(const char* text) { return *this << std::string_view(text ? text : ""); }
		Writer& operator<<(const std::string& text) { return *this << std::string_view(text); }
		Writer& operator<<(std::nullptr_t) { return WriteScalar("~", false); }   // a YAML null
		Writer& operator<<(bool value);
		Writer& operator<<(float value);
		Writer& operator<<(double value);
		Writer& operator<<(const UUID& uuid) { return *this << static_cast<uint64_t>(uuid); }
		Writer& operator<<(const glm::vec2& v) { return *this << Flow << BeginSeq << v.x << v.y << EndSeq; }
		Writer& operator<<(const glm::vec3& v) { return *this << Flow << BeginSeq << v.x << v.y << v.z << EndSeq; }
		Writer& operator<<(const glm::vec4& v) { return *this << Flow << BeginSeq << v.x << v.y << v.z << v.w << EndSeq; }

		// 8-bit types are rejected on purpose: yaml-cpp wrote them as characters (""), so a
		// ported caller must choose, e.g. `static_cast<uint32_t>(value)` to write a number.
		template<typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !Detail::IsByte<T>)
		Writer& operator<<(T value)
		{
			char buffer[32];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			return WriteScalar(std::string_view(buffer, static_cast<size_t>(result.ptr - buffer)), false);
		}

		template<typename T>
		Writer& operator<<(const std::vector<T>& values)
		{
			*this << Flow << BeginSeq;
			for (const T& value : values)
				*this << value;
			return *this << EndSeq;
		}

		// The document written so far, as YAML text.
		const char* c_str() const;
		size_t size() const;
		std::string str() const { return c_str(); }

	private:
		// `quoteIfAmbiguous`: text values that would read back as null (empty, ~, null) are quoted;
		// numbers and booleans the writer formats itself never need it.
		Writer& WriteScalar(std::string_view text, bool quoteIfAmbiguous);

		struct Impl;
		Scope<Impl> m_Impl;
	};

	// ── Conversions (as<T>) ─────────────────────────────────────────────────────────────────────

	namespace Detail {
		bool DecodeBool(std::string_view text, bool& out);
		bool DecodeFloat(std::string_view text, double& out);   // YAML .inf/.nan included

		template<typename T>
		bool DecodeInteger(std::string_view text, T& out)
		{
			if (!text.empty() && text.front() == '+')
				text.remove_prefix(1);
			int base = 10;
			if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
			{
				text.remove_prefix(2);
				base = 16;
			}
			if (text.empty())
				return false;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), out, base);
			return result.ec == std::errc() && result.ptr == text.data() + text.size();
		}
	}

	template<>
	struct Convert<bool>
	{
		static bool Decode(const Node& node, bool& out) { return node.IsScalar() && Detail::DecodeBool(node.Scalar(), out); }
	};

	template<>
	struct Convert<std::string>
	{
		static bool Decode(const Node& node, std::string& out)
		{
			if (!node.IsScalar())
				return false;
			out.assign(node.Scalar());
			return true;
		}
	};

	template<typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !Detail::IsByte<T>)
	struct Convert<T>
	{
		static bool Decode(const Node& node, T& out) { return node.IsScalar() && Detail::DecodeInteger(node.Scalar(), out); }
	};

	// 8-bit integers read a number, or else the single character yaml-cpp wrote for them (`""`).
	// A digit is read as a number, so a byte yaml-cpp wrote as '0'..'9' (48..57) would read wrongly;
	// the engine's 8-bit fields are small enums, which yaml-cpp always escaped.
	template<typename T> requires Detail::IsByte<T>
	struct Convert<T>
	{
		static bool Decode(const Node& node, T& out)
		{
			if (!node.IsScalar())
				return false;
			const std::string_view text = node.Scalar();
			if (Detail::DecodeInteger(text, out))
				return true;
			if (text.size() != 1)
				return false;
			out = static_cast<T>(static_cast<unsigned char>(text.front()));
			return true;
		}
	};

	template<typename T> requires std::is_floating_point_v<T>
	struct Convert<T>
	{
		static bool Decode(const Node& node, T& out)
		{
			double value = 0.0;
			if (!node.IsScalar() || !Detail::DecodeFloat(node.Scalar(), value))
				return false;
			out = static_cast<T>(value);
			return true;
		}
	};

	template<>
	struct Convert<UUID>
	{
		static bool Decode(const Node& node, UUID& out)
		{
			uint64_t value = 0;
			if (!Convert<uint64_t>::Decode(node, value))
				return false;
			out = value;
			return true;
		}
	};

	template<glm::length_t L>
	struct Convert<glm::vec<L, float, glm::defaultp>>
	{
		static bool Decode(const Node& node, glm::vec<L, float, glm::defaultp>& out)
		{
			if (!node.IsSequence() || node.size() != static_cast<size_t>(L))
				return false;
			for (glm::length_t i = 0; i < L; ++i)
				if (!Convert<float>::Decode(node[static_cast<size_t>(i)], out[i]))
					return false;
			return true;
		}
	};

	template<typename T>
	struct Convert<std::vector<T>>
	{
		static bool Decode(const Node& node, std::vector<T>& out)
		{
			if (!node.IsSequence())
				return false;
			out.clear();
			out.reserve(node.size());
			for (const Node& item : node)
			{
				T value{};
				if (!Convert<T>::Decode(item, value))
					return false;
				out.push_back(std::move(value));
			}
			return true;
		}
	};

}
