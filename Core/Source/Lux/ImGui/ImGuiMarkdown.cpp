// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ImGuiMarkdown.h"

#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiFonts.h"
#include "Lux/ImGui/ImGuiUtilities.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace Lux::ImGuiEx {

	namespace {

		using Span = MarkdownDocument::Span;
		using Block = MarkdownDocument::Block;
		using BlockType = MarkdownDocument::BlockType;

		constexpr float k_HeadingScale[3] = { 1.65f, 1.3f, 1.1f };
		constexpr float k_CodeScale = 0.92f;
		constexpr float k_LineSpacing = 1.4f;   // line height as a multiple of the font size
		constexpr float k_ListIndent = 20.0f;
		constexpr float k_QuoteIndent = 14.0f;
		constexpr float k_CodePadding = 8.0f;

		std::string_view Trim(std::string_view text)
		{
			const size_t first = text.find_first_not_of(" \t");
			if (first == std::string_view::npos)
				return {};
			const size_t last = text.find_last_not_of(" \t");
			return text.substr(first, last - first + 1);
		}

		size_t LeadingSpaces(std::string_view line)
		{
			size_t count = 0;
			while (count < line.size() && (line[count] == ' ' || line[count] == '\t'))
				count++;
			return count;
		}

		// **bold**, *italic*, `code`, [text](target) and backslash escapes; everything else is text.
		std::vector<Span> ParseSpans(std::string_view text)
		{
			std::vector<Span> spans;
			Span current;
			auto flush = [&]()
			{
				if (!current.Text.empty())
					spans.push_back(current);
				current.Text.clear();
			};

			for (size_t i = 0; i < text.size(); i++)
			{
				const char c = text[i];
				if (c == '\\' && i + 1 < text.size())
				{
					current.Text += text[++i];
				}
				else if (c == '`')
				{
					const size_t end = text.find('`', i + 1);
					if (end == std::string_view::npos)
					{
						current.Text += c;
						continue;
					}
					flush();
					spans.push_back({ std::string(text.substr(i + 1, end - i - 1)), {}, current.Bold, current.Italic, true });
					i = end;
				}
				else if (c == '*' && i + 1 < text.size() && text[i + 1] == '*')
				{
					flush();
					current.Bold = !current.Bold;
					i++;
				}
				else if (c == '*')
				{
					flush();
					current.Italic = !current.Italic;
				}
				else if (c == '[')
				{
					const size_t close = text.find("](", i + 1);
					const size_t end = close == std::string_view::npos ? close : text.find(')', close + 2);
					if (end == std::string_view::npos)
					{
						current.Text += c;
						continue;
					}
					flush();
					Span link{ std::string(text.substr(i + 1, close - i - 1)), std::string(Trim(text.substr(close + 2, end - close - 2))), current.Bold, current.Italic, false };
					spans.push_back(std::move(link));
					i = end;
				}
				else
				{
					current.Text += c;
				}
			}
			flush();
			return spans;
		}

		std::vector<std::string_view> SplitTableRow(std::string_view line)
		{
			line = Trim(line);
			if (!line.empty() && line.front() == '|')
				line.remove_prefix(1);
			if (!line.empty() && line.back() == '|')
				line.remove_suffix(1);

			std::vector<std::string_view> cells;
			size_t start = 0;
			for (size_t i = 0; i <= line.size(); i++)
			{
				if (i == line.size() || line[i] == '|')
				{
					cells.push_back(Trim(line.substr(start, i - start)));
					start = i + 1;
				}
			}
			return cells;
		}

		bool IsTableSeparator(std::string_view line)
		{
			for (std::string_view cell : SplitTableRow(line))
			{
				if (cell.empty() || cell.find_first_not_of("-: ") != std::string_view::npos)
					return false;
			}
			return true;
		}

		// A list item's marker: "-", "*" or "+" (bullet) or digits and "." (ordered). Returns the
		// length of the marker including the space after it, or zero.
		size_t ListMarkerLength(std::string_view text, std::string& orderedMarker)
		{
			if (text.size() >= 2 && (text[0] == '-' || text[0] == '*' || text[0] == '+') && text[1] == ' ')
				return 2;

			size_t digits = 0;
			while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits])))
				digits++;
			if (digits > 0 && digits + 1 < text.size() && text[digits] == '.' && text[digits + 1] == ' ')
			{
				orderedMarker = std::string(text.substr(0, digits + 1));
				return digits + 2;
			}
			return 0;
		}

		struct TextStyle
		{
			ImFont* Font = nullptr;
			float Size = 0.0f;
			ImU32 Color = 0;
		};

		ImFont* FontOr(const char* name, ImFont* fallback)
		{
			ImFont* font = Fonts::Get(name);
			return font ? font : fallback;
		}

		// Lays the spans out word by word from the cursor, wrapping at the available width, and moves
		// the cursor below them. Links are drawn as accent text and reported through `callbacks`.
		void RenderSpans(const std::vector<Span>& spans, const TextStyle& style, const MarkdownCallbacks& callbacks)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImFont* boldFont = FontOr("BoldTitle", style.Font);
			ImFont* monoFont = FontOr("Mono", style.Font);

			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float maxX = origin.x + glm::max(ImGui::GetContentRegionAvail().x, 1.0f);
			const float lineHeight = style.Size * k_LineSpacing;
			const float spaceWidth = style.Font->CalcTextSizeA(style.Size, FLT_MAX, 0.0f, " ").x;
			const bool windowHovered = ImGui::IsWindowHovered();

			float x = origin.x;
			float y = origin.y;
			bool spacePending = false;

			for (const Span& span : spans)
			{
				ImFont* font = span.Code ? monoFont : span.Bold ? boldFont : style.Font;
				const float size = span.Code ? style.Size * k_CodeScale : style.Size;
				const bool isLink = !span.Link.empty();
				const ImU32 color = isLink ? Colors::Theme::accent : span.Italic ? Colors::Theme::textBrighter : style.Color;
				const float yOffset = (style.Size - size) * 0.5f;

				std::string_view text = span.Text;
				size_t position = 0;
				while (position < text.size())
				{
					if (text[position] == ' ')
					{
						spacePending = true;
						position++;
						continue;
					}

					size_t end = text.find(' ', position);
					if (end == std::string_view::npos)
						end = text.size();
					const char* wordBegin = text.data() + position;
					const char* wordEnd = text.data() + end;
					const float wordWidth = font->CalcTextSizeA(size, FLT_MAX, 0.0f, wordBegin, wordEnd).x;

					float advance = spacePending && x > origin.x ? spaceWidth : 0.0f;
					if (x > origin.x && x + advance + wordWidth > maxX)
					{
						x = origin.x;
						y += lineHeight;
						advance = 0.0f;
					}
					x += advance;
					spacePending = false;

					const ImVec2 wordMin(x, y + yOffset);
					const ImVec2 wordMax(x + wordWidth, y + yOffset + size);
					if (span.Code)
						drawList->AddRectFilled(ImVec2(wordMin.x - 2.0f, wordMin.y - 1.0f), ImVec2(wordMax.x + 2.0f, wordMax.y + 1.0f), Colors::Theme::groupHeader, 3.0f);
					drawList->AddText(font, size, wordMin, color, wordBegin, wordEnd);

					if (isLink)
					{
						drawList->AddLine(ImVec2(wordMin.x, wordMax.y), ImVec2(wordMax.x, wordMax.y), color);
						if (windowHovered && ImGui::IsMouseHoveringRect(wordMin, wordMax))
						{
							ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
							ImGui::SetTooltip("%s", span.Link.c_str());
							if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && callbacks.OnLink)
								callbacks.OnLink(span.Link);
						}
					}

					x += wordWidth;
					position = end;
				}
			}

			ImGui::SetCursorScreenPos(origin);
			ImGui::Dummy(ImVec2(maxX - origin.x, y + lineHeight - origin.y));
		}

		void RenderCodeBlock(const Block& block, size_t index)
		{
			ImFont* monoFont = FontOr("Mono", ImGui::GetFont());
			ImVec2 textSize;
			{
				ScopedFont font(monoFont);
				textSize = ImGui::CalcTextSize(block.Code.data(), block.Code.data() + block.Code.size());
			}
			const float width = ImGui::GetContentRegionAvail().x;
			const bool scrolls = textSize.x + k_CodePadding * 2.0f > width;
			const float height = textSize.y + k_CodePadding * 2.0f + (scrolls ? ImGui::GetStyle().ScrollbarSize : 0.0f);

			ScopedID blockID(static_cast<int>(index));
			const ImVec2 blockMin = ImGui::GetCursorScreenPos();
			{
				ScopedColour background(ImGuiCol_ChildBg, Colors::Theme::backgroundDark);
				ScopedStyle padding(ImGuiStyleVar_WindowPadding, ImVec2(k_CodePadding, k_CodePadding));
				ImGui::BeginChild("##code", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
				{
					ScopedFont font(monoFont);
					ImGui::TextUnformatted(block.Code.data(), block.Code.data() + block.Code.size());
				}
				ImGui::EndChild();
			}
			const ImVec2 blockMax = ImGui::GetItemRectMax();

			// Copy sits over the block's top-right corner; the cursor then returns below the block.
			const char* copyLabel = LUX_ICON_FILES_O "##copy";
			const ImVec2 buttonSize = ImGui::CalcTextSize(LUX_ICON_FILES_O);
			ImGui::SetCursorScreenPos(ImVec2(blockMax.x - buttonSize.x - k_CodePadding * 1.5f, blockMin.y + k_CodePadding * 0.5f));
			if (ImGui::SmallButton(copyLabel))
				ImGui::SetClipboardText(block.Code.c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Copy");

			ImGui::SetCursorScreenPos(ImVec2(blockMin.x, blockMax.y + ImGui::GetStyle().ItemSpacing.y));
			ImGui::Dummy(ImVec2(0.0f, 0.0f));
		}

		void RenderTable(const Block& block, size_t index, const TextStyle& body, const MarkdownCallbacks& callbacks)
		{
			size_t columns = 0;
			for (const auto& row : block.Rows)
				columns = glm::max(columns, row.size());
			if (columns == 0)
				return;

			ScopedID blockID(static_cast<int>(index));
			if (!ImGui::BeginTable("##table", static_cast<int>(columns), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				return;

			TextStyle header = body;
			header.Font = FontOr("BoldTitle", body.Font);
			header.Color = Colors::Theme::textBrighter;
			for (size_t row = 0; row < block.Rows.size(); row++)
			{
				ImGui::TableNextRow(row == 0 ? ImGuiTableRowFlags_Headers : ImGuiTableRowFlags_None);
				for (size_t column = 0; column < block.Rows[row].size(); column++)
				{
					ImGui::TableSetColumnIndex(static_cast<int>(column));
					RenderSpans(block.Rows[row][column], row == 0 ? header : body, callbacks);
				}
			}
			ImGui::EndTable();
		}

	}

	MarkdownDocument::MarkdownDocument(std::string_view text)
	{
		std::vector<std::string_view> lines;
		for (size_t start = 0; start <= text.size();)
		{
			size_t end = text.find('\n', start);
			if (end == std::string_view::npos)
				end = text.size();
			std::string_view line = text.substr(start, end - start);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);
			lines.push_back(line);
			start = end + 1;
		}

		std::string paragraph;
		auto flushParagraph = [&]()
		{
			if (paragraph.empty())
				return;
			Block& block = m_Blocks.emplace_back();
			block.Type = BlockType::Paragraph;
			block.Spans = ParseSpans(paragraph);
			paragraph.clear();
		};

		for (size_t i = 0; i < lines.size(); i++)
		{
			const std::string_view line = lines[i];
			const std::string_view trimmed = Trim(line);

			if (trimmed.starts_with("```"))
			{
				flushParagraph();
				Block& block = m_Blocks.emplace_back();
				block.Type = BlockType::Code;
				for (i++; i < lines.size() && !Trim(lines[i]).starts_with("```"); i++)
				{
					if (!block.Code.empty())
						block.Code += '\n';
					block.Code += lines[i];
				}
				continue;
			}

			if (trimmed.empty())
			{
				flushParagraph();
				continue;
			}

			if (trimmed.starts_with('#'))
			{
				const size_t level = trimmed.find_first_not_of('#');
				if (level != std::string_view::npos && level <= 3 && trimmed[level] == ' ')
				{
					flushParagraph();
					Block& block = m_Blocks.emplace_back();
					block.Type = BlockType::Heading;
					block.Level = static_cast<uint32_t>(level);
					block.Spans = ParseSpans(Trim(trimmed.substr(level)));
					if (level == 1 && m_Title.empty())
					{
						for (const Span& span : block.Spans)
							m_Title += span.Text;
					}
					continue;
				}
			}

			if (trimmed == "---" || trimmed == "***")
			{
				flushParagraph();
				m_Blocks.emplace_back().Type = BlockType::Rule;
				continue;
			}

			if (trimmed.starts_with('>'))
			{
				flushParagraph();
				std::string quote(Trim(trimmed.substr(1)));
				while (i + 1 < lines.size() && Trim(lines[i + 1]).starts_with('>'))
					quote += " " + std::string(Trim(Trim(lines[++i]).substr(1)));
				Block& block = m_Blocks.emplace_back();
				block.Type = BlockType::Quote;
				block.Spans = ParseSpans(quote);
				continue;
			}

			if (trimmed.starts_with('|'))
			{
				flushParagraph();
				Block& block = m_Blocks.emplace_back();
				block.Type = BlockType::Table;
				for (; i < lines.size() && Trim(lines[i]).starts_with('|'); i++)
				{
					if (IsTableSeparator(lines[i]))
						continue;
					auto& row = block.Rows.emplace_back();
					for (std::string_view cell : SplitTableRow(lines[i]))
						row.push_back(ParseSpans(cell));
				}
				i--;
				continue;
			}

			std::string orderedMarker;
			if (const size_t markerLength = ListMarkerLength(trimmed, orderedMarker))
			{
				flushParagraph();
				Block& block = m_Blocks.emplace_back();
				block.Type = BlockType::ListItem;
				block.Level = static_cast<uint32_t>(LeadingSpaces(line) / 2);
				block.Marker = orderedMarker;
				std::string item(trimmed.substr(markerLength));
				// Lines indented under the item continue it.
				while (i + 1 < lines.size() && !Trim(lines[i + 1]).empty() && LeadingSpaces(lines[i + 1]) > LeadingSpaces(line))
				{
					std::string unused;
					if (ListMarkerLength(Trim(lines[i + 1]), unused))
						break;
					item += " " + std::string(Trim(lines[++i]));
				}
				block.Spans = ParseSpans(item);
				continue;
			}

			if (!paragraph.empty())
				paragraph += ' ';
			paragraph += trimmed;
		}
		flushParagraph();
	}

	void RenderMarkdown(const MarkdownDocument& document, const MarkdownCallbacks& callbacks)
	{
		const float baseSize = ImGui::GetFontSize();
		const ImGuiStyle& style = ImGui::GetStyle();
		TextStyle body{ ImGui::GetFont(), baseSize, Colors::Theme::text };

		const auto& blocks = document.GetBlocks();
		for (size_t index = 0; index < blocks.size(); index++)
		{
			const Block& block = blocks[index];
			switch (block.Type)
			{
				case BlockType::Heading:
				{
					const uint32_t level = glm::clamp(block.Level, 1u, 3u);
					TextStyle heading{ FontOr(level == 1 ? "Display" : "Bold", body.Font), baseSize * k_HeadingScale[level - 1], Colors::Theme::textBrighter };
					if (index > 0)
						ImGui::Dummy(ImVec2(0.0f, baseSize * (level == 1 ? 1.0f : 0.6f)));
					RenderSpans(block.Spans, heading, callbacks);
					if (level < 3)
					{
						const ImVec2 lineStart = ImGui::GetCursorScreenPos();
						ImGui::GetWindowDrawList()->AddLine(lineStart, ImVec2(lineStart.x + ImGui::GetContentRegionAvail().x, lineStart.y), Colors::Theme::groupHeader);
						ImGui::Dummy(ImVec2(0.0f, style.ItemSpacing.y));
					}
					break;
				}
				case BlockType::Paragraph:
					RenderSpans(block.Spans, body, callbacks);
					ImGui::Dummy(ImVec2(0.0f, baseSize * 0.35f));
					break;
				case BlockType::ListItem:
				{
					const float indent = k_ListIndent * static_cast<float>(block.Level + 1);
					const ImVec2 itemStart = ImGui::GetCursorScreenPos();
					const float centerY = itemStart.y + baseSize * k_LineSpacing * 0.5f;
					if (block.Marker.empty())
						ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(itemStart.x + indent - 9.0f, centerY), 2.5f, Colors::Theme::textDarker);
					else
						ImGui::GetWindowDrawList()->AddText(body.Font, baseSize, ImVec2(itemStart.x + indent - 16.0f, centerY - baseSize * 0.5f), Colors::Theme::textDarker, block.Marker.c_str());

					ImGui::SetCursorScreenPos(ImVec2(itemStart.x + indent, itemStart.y));
					RenderSpans(block.Spans, body, callbacks);
					ImGui::SetCursorScreenPos(ImVec2(itemStart.x, ImGui::GetCursorScreenPos().y));
					const bool lastOfList = index + 1 == blocks.size() || blocks[index + 1].Type != BlockType::ListItem;
					ImGui::Dummy(ImVec2(0.0f, lastOfList ? baseSize * 0.35f : 0.0f));
					break;
				}
				case BlockType::Quote:
				{
					const ImVec2 quoteStart = ImGui::GetCursorScreenPos();
					ImGui::SetCursorScreenPos(ImVec2(quoteStart.x + k_QuoteIndent, quoteStart.y));
					TextStyle quote = body;
					quote.Color = Colors::Theme::textBrighter;
					RenderSpans(block.Spans, quote, callbacks);
					const float quoteEnd = ImGui::GetCursorScreenPos().y - style.ItemSpacing.y;
					ImGui::GetWindowDrawList()->AddRectFilled(quoteStart, ImVec2(quoteStart.x + 3.0f, quoteEnd), Colors::Theme::accent, 1.5f);
					ImGui::SetCursorScreenPos(ImVec2(quoteStart.x, ImGui::GetCursorScreenPos().y));
					ImGui::Dummy(ImVec2(0.0f, baseSize * 0.35f));
					break;
				}
				case BlockType::Code:
					RenderCodeBlock(block, index);
					break;
				case BlockType::Table:
					RenderTable(block, index, body, callbacks);
					ImGui::Dummy(ImVec2(0.0f, baseSize * 0.35f));
					break;
				case BlockType::Rule:
				{
					const ImVec2 ruleStart = ImGui::GetCursorScreenPos();
					const float y = ruleStart.y + style.ItemSpacing.y;
					ImGui::GetWindowDrawList()->AddLine(ImVec2(ruleStart.x, y), ImVec2(ruleStart.x + ImGui::GetContentRegionAvail().x, y), Colors::Theme::groupHeader);
					ImGui::Dummy(ImVec2(0.0f, style.ItemSpacing.y * 2.0f));
					break;
				}
			}
		}
	}

}
