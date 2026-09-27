// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Yaml.h"

// Declarations only; the library bodies are compiled once in Core/vendor/rapidyaml/ryml.cpp.
#include <ryml_all.hpp>

#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>

namespace Lux::Yaml {

	namespace {

		std::string ToString(c4::csubstr text) { return std::string(text.str ? text.str : "", text.len); }

		[[noreturn]] void OnBasicError(c4::csubstr message, const c4::yml::ErrorDataBasic&, void*)
		{
			throw Exception("YAML: " + ToString(message));
		}

		[[noreturn]] void OnParseError(c4::csubstr message, const c4::yml::ErrorDataParse& error, void*)
		{
			// ymlloc.line is 1-based.
			throw Exception("YAML parse error: " + ToString(message) + " (" + ToString(error.ymlloc.name)
				+ " line " + std::to_string(error.ymlloc.line) + ")");
		}

		[[noreturn]] void OnVisitError(c4::csubstr message, const c4::yml::ErrorDataVisit&, void*)
		{
			throw Exception("YAML: " + ToString(message));
		}

		std::once_flag s_InstallOnce;

		// Text that YAML reads back as null, so a string with this content must be quoted.
		bool ReadsAsNull(std::string_view text)
		{
			return text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL";
		}

		template<typename T>
		std::string_view FormatFloat(T value, char (&buffer)[64])
		{
			if (std::isnan(value))
				return ".nan";
			if (std::isinf(value))
				return value > 0 ? ".inf" : "-.inf";
			// Shortest text that reads back to exactly this value.
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			return std::string_view(buffer, static_cast<size_t>(result.ptr - buffer));
		}

	}

	void InstallErrorHandlers()
	{
		std::call_once(s_InstallOnce, []
		{
			c4::yml::Callbacks callbacks = c4::yml::get_callbacks();
			callbacks.set_error_basic(OnBasicError).set_error_parse(OnParseError).set_error_visit(OnVisitError);
			c4::yml::set_callbacks(callbacks);
		});
	}

	// ── Document ────────────────────────────────────────────────────────────────────────────────

	Detail::Document::Document() : Tree(CreateScope<c4::yml::Tree>()) {}
	Detail::Document::~Document() = default;

	Node Load(std::string text, std::string_view name)
	{
		InstallErrorHandlers();

		Ref<Detail::Document> document = Ref<Detail::Document>::Create();
		document->Buffer = std::move(text);
		// Parsed in place: scalars are views into Buffer, which the Document owns and never resizes.
		*document->Tree = c4::yml::parse_in_place(c4::csubstr(name.data(), name.size()), c4::to_substr(document->Buffer));

		const c4::yml::Tree& tree = *document->Tree;
		size_t root = tree.root_id();
		if (tree.is_stream(root) && tree.has_children(root))
			root = tree.first_child(root);   // single-document files: the document node
		return Node(std::move(document), root, false);
	}

	Node LoadFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			throw Exception("YAML: could not open " + path.string());
		std::ostringstream text;
		text << stream.rdbuf();
		return Load(std::move(text).str(), path.string());
	}

	// ── Node ────────────────────────────────────────────────────────────────────────────────────

	const c4::yml::Tree& Node::GetTree() const { return *m_Document->Tree; }

	NodeType Node::Type() const
	{
		if (!IsDefined())
			return NodeType::Undefined;
		if (m_IsKey)
			return NodeType::Scalar;

		const c4::yml::Tree& tree = GetTree();
		if (tree.is_map(m_Id))
			return NodeType::Map;
		if (tree.is_seq(m_Id))
			return NodeType::Sequence;
		if (tree.has_val(m_Id) && !tree.val_is_null(m_Id))
			return NodeType::Scalar;
		return NodeType::Null;   // `Key:` with no value, ~, null, or an empty document
	}

	size_t Node::size() const
	{
		if (!IsDefined() || m_IsKey)
			return 0;
		const c4::yml::Tree& tree = GetTree();
		return tree.is_container(m_Id) ? tree.num_children(m_Id) : 0;
	}

	std::string_view Node::Scalar() const
	{
		if (!IsDefined())
			return {};
		const c4::yml::Tree& tree = GetTree();
		const c4::csubstr text = m_IsKey ? tree.key(m_Id) : (Type() == NodeType::Scalar ? tree.val(m_Id) : c4::csubstr{});
		return std::string_view(text.str ? text.str : "", text.len);
	}

	Node Node::operator[](std::string_view key) const
	{
		if (!IsMap())
			return {};
		const size_t child = GetTree().find_child(m_Id, c4::csubstr(key.data(), key.size()));
		return child == c4::yml::NONE ? Node{} : Node(m_Document, child, false);
	}

	Node Node::operator[](size_t index) const
	{
		// On a map an integer is a key, as in yaml-cpp (`MaterialTable: {0: ...}`), not a position.
		if (IsMap())
			return (*this)[std::string_view(std::to_string(index))];
		if (!IsSequence())
			return {};
		const size_t child = GetTree().child(m_Id, index);
		return child == c4::yml::NONE ? Node{} : Node(m_Document, child, false);
	}

	std::string Node::Dump() const
	{
		// The value only, without its key (as yaml-cpp's Dump did): copy the children under the root
		// of a scratch tree, so `Key: [..]` dumps as `[..]`.
		switch (Type())
		{
		case NodeType::Undefined:
			return {};
		case NodeType::Null:
			return "~";
		case NodeType::Scalar:
			return std::string(Scalar());
		case NodeType::Sequence:
		case NodeType::Map:
			break;
		}

		c4::yml::Tree value;
		const size_t root = value.root_id();
		if (IsMap())
			value.set_map(root);
		else
			value.set_seq(root);
		value.duplicate_children(&GetTree(), m_Id, root, c4::yml::NONE);
		return c4::yml::emitrs_yaml<std::string>(value);
	}

	Node::Iterator Node::begin() const
	{
		if (!IsMap() && !IsSequence())
			return end();
		return Iterator(*this, GetTree().first_child(m_Id));
	}

	Node::Iterator Node::end() const
	{
		return Iterator(*this, s_InvalidId);
	}

	void Node::Iterator::Load()
	{
		if (m_Child == s_InvalidId || m_Child == c4::yml::NONE)
		{
			m_Child = s_InvalidId;
			m_Value = {};
			return;
		}

		const Node child(m_Parent.m_Document, m_Child, false);
		static_cast<Node&>(m_Value) = child;
		if (m_Parent.IsMap())
		{
			m_Value.first = Node(m_Parent.m_Document, m_Child, true);
			m_Value.second = child;
		}
		else
		{
			m_Value.first = {};
			m_Value.second = {};
		}
	}

	Node::Iterator& Node::Iterator::operator++()
	{
		if (m_Child != s_InvalidId)
			m_Child = m_Parent.GetTree().next_sibling(m_Child);
		Load();
		return *this;
	}

	// ── Writer ──────────────────────────────────────────────────────────────────────────────────

	struct Writer::Impl
	{
		struct Frame
		{
			size_t Id = c4::yml::NONE;
			bool IsMap = false;
			bool HasKey = false;
			std::string PendingKey;   // owned: arena copies can move when the arena grows
		};

		c4::yml::Tree Tree;
		std::vector<Frame> Stack;
		bool RootUsed = false;
		bool NextFlow = false;
		mutable std::string Output;
		mutable bool Dirty = false;

		// The node the next value (scalar or container) goes into.
		size_t NextNode()
		{
			if (Stack.empty())
			{
				if (RootUsed)
					throw Exception("YAML writer: more than one root value");
				RootUsed = true;
				return Tree.root_id();
			}

			Frame& top = Stack.back();
			const size_t node = Tree.append_child(top.Id);
			if (top.IsMap)
			{
				if (!top.HasKey)
					throw Exception("YAML writer: map value without a key");
				Tree.set_key(node, Tree.copy_to_arena(c4::to_csubstr(top.PendingKey)));
				top.HasKey = false;
			}
			return node;
		}
	};

	Writer::Writer() : m_Impl(CreateScope<Impl>())
	{
		InstallErrorHandlers();
	}

	Writer::~Writer() = default;

	Writer& Writer::operator<<(Manipulator manipulator)
	{
		Impl& impl = *m_Impl;
		impl.Dirty = true;
		switch (manipulator)
		{
		case Manipulator::Key:
			if (!impl.Stack.empty())
				impl.Stack.back().HasKey = false;   // the next scalar is the key
			break;
		case Manipulator::Value:
			break;                                  // values follow keys implicitly
		case Manipulator::Flow:
			impl.NextFlow = true;
			break;
		case Manipulator::BeginMap:
		case Manipulator::BeginSeq:
		{
			const bool isMap = manipulator == Manipulator::BeginMap;
			const size_t node = impl.NextNode();
			if (isMap)
				impl.Tree.set_map(node);
			else
				impl.Tree.set_seq(node);
			impl.Tree.set_container_style(node, impl.NextFlow ? (c4::yml::FLOW_SL | c4::yml::FLOW_SPC) : c4::yml::BLOCK);   // [x, y, z]
			impl.NextFlow = false;
			impl.Stack.push_back({ node, isMap });
			break;
		}
		case Manipulator::EndMap:
		case Manipulator::EndSeq:
			if (impl.Stack.empty())
				throw Exception("YAML writer: End without a matching Begin");
			impl.Stack.pop_back();
			break;
		}
		return *this;
	}

	Writer& Writer::WriteScalar(std::string_view text, bool quoteIfAmbiguous)
	{
		Impl& impl = *m_Impl;
		impl.Dirty = true;

		if (!impl.Stack.empty() && impl.Stack.back().IsMap && !impl.Stack.back().HasKey)
		{
			impl.Stack.back().PendingKey.assign(text);
			impl.Stack.back().HasKey = true;
			return *this;
		}

		const size_t node = impl.NextNode();
		impl.Tree.set_val(node, impl.Tree.copy_to_arena(c4::csubstr(text.data(), text.size())));
		if (quoteIfAmbiguous && ReadsAsNull(text))
			impl.Tree.set_val_style(node, c4::yml::VAL_DQUO);
		return *this;
	}

	Writer& Writer::operator<<(std::string_view text) { return WriteScalar(text, true); }
	Writer& Writer::operator<<(bool value) { return WriteScalar(value ? "true" : "false", false); }

	Writer& Writer::operator<<(float value)
	{
		char buffer[64];
		return WriteScalar(FormatFloat(value, buffer), false);
	}

	Writer& Writer::operator<<(double value)
	{
		char buffer[64];
		return WriteScalar(FormatFloat(value, buffer), false);
	}

	const char* Writer::c_str() const
	{
		Impl& impl = *m_Impl;
		if (impl.Dirty)
		{
			impl.Output = impl.RootUsed ? c4::yml::emitrs_yaml<std::string>(impl.Tree) : std::string();
			impl.Dirty = false;
		}
		return impl.Output.c_str();
	}

	size_t Writer::size() const
	{
		c_str();
		return m_Impl->Output.size();
	}

	// ── Conversions ─────────────────────────────────────────────────────────────────────────────

	namespace Detail {

		bool DecodeBool(std::string_view text, bool& out)
		{
			// yaml-cpp's rules: y/n, yes/no, true/false, on/off, in lower, UPPER or Capitalized case.
			if (text.empty() || text.size() > 5)
				return false;
			char lower[6] = {};
			bool allLower = true, allUpper = true, restLower = true, restUpper = true;
			for (size_t i = 0; i < text.size(); ++i)
			{
				const char c = text[i];
				const bool isLower = c >= 'a' && c <= 'z';
				const bool isUpper = c >= 'A' && c <= 'Z';
				allLower &= isLower;
				allUpper &= isUpper;
				if (i > 0)
				{
					restLower &= isLower;
					restUpper &= isUpper;
				}
				lower[i] = isUpper ? static_cast<char>(c - 'A' + 'a') : c;
			}
			const bool firstUpper = text[0] >= 'A' && text[0] <= 'Z';
			if (!(allLower || allUpper || (firstUpper && (restLower || restUpper))))
				return false;

			const std::string_view word(lower, text.size());
			if (word == "y" || word == "yes" || word == "true" || word == "on")
			{
				out = true;
				return true;
			}
			if (word == "n" || word == "no" || word == "false" || word == "off")
			{
				out = false;
				return true;
			}
			return false;
		}

		bool DecodeFloat(std::string_view text, double& out)
		{
			if (text.empty())
				return false;

			const bool negative = text.front() == '-';
			std::string_view magnitude = (text.front() == '-' || text.front() == '+') ? text.substr(1) : text;
			if (magnitude == ".inf" || magnitude == ".Inf" || magnitude == ".INF")
			{
				out = negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
				return true;
			}
			if (text == ".nan" || text == ".NaN" || text == ".NAN")
			{
				out = std::numeric_limits<double>::quiet_NaN();
				return true;
			}

			if (text.front() == '+')
				text.remove_prefix(1);
			const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
			return result.ec == std::errc() && result.ptr == text.data() + text.size();
		}

	}

}
