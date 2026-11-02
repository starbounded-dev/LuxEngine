// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Lux::ImGuiEx {

	// A Markdown document parsed for RenderMarkdown: parse it once, draw it every frame. The subset
	// is what the editor's manual uses: # headings (three levels), paragraphs with **bold**,
	// *italic*, `code` and [links](target), "-" / "1." lists (two spaces per nesting level),
	// "> " quotes, ``` code blocks, "|" tables and --- rules. Anything else is plain text.
	class MarkdownDocument
	{
	public:
		struct Span
		{
			std::string Text;
			std::string Link; // empty unless the span is a link
			bool Bold = false;
			bool Italic = false;
			bool Code = false;
		};

		enum class BlockType { Heading, Paragraph, ListItem, Quote, Code, Table, Rule };

		struct Block
		{
			BlockType Type = BlockType::Paragraph;
			uint32_t Level = 0;   // heading level (1-3), or list nesting level
			std::string Marker;   // ordered list item number ("3."), empty for a bullet
			std::vector<Span> Spans;
			std::string Code;     // code block text
			std::vector<std::vector<std::vector<Span>>> Rows; // table rows of cells; row 0 is the header
		};

		MarkdownDocument() = default;
		explicit MarkdownDocument(std::string_view text);

		const std::vector<Block>& GetBlocks() const { return m_Blocks; }
		// The text of the first level-1 heading, or empty.
		const std::string& GetTitle() const { return m_Title; }
	private:
		std::vector<Block> m_Blocks;
		std::string m_Title;
	};

	struct MarkdownCallbacks
	{
		// A link was clicked; `target` is the link as written.
		std::function<void(std::string_view target)> OnLink;
	};

	// Draws `document` at the cursor, wrapped to the available width. Main thread, inside a window.
	void RenderMarkdown(const MarkdownDocument& document, const MarkdownCallbacks& callbacks = {});

}
