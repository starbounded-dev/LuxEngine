// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UIAccessibility.h"

#include "Lux/Renderer/FrameRenderPacket.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/UI/Font.h"

#include <algorithm>
#include <cmath>

namespace Lux::UI
{
	std::vector<CaptionEntry> CollectCaptions(const std::vector<AccessibleSubtitle>& subtitles, const AudioAccessibilityPreferences& preferences,
		const std::function<int(const std::string&)>& lineCount, const std::function<glm::vec3(const glm::vec3&)>& directionTo)
	{
		std::vector<CaptionEntry> newestFirst;
		int used = 0;
		for (auto entry = subtitles.rbegin(); entry != subtitles.rend(); ++entry)
		{
			CaptionEntry caption;
			if (preferences.DirectionIndicators && entry->Event.IsOffScreen)
			{
				const glm::vec3 direction = directionTo(entry->Event.SpeakerPosition);
				caption.Text += direction.x < -0.25f ? "< " : direction.x > 0.25f ? "> " : direction.z < 0.0f ? "[behind] " : "[ahead] ";
			}
			if (preferences.SpeakerNames && !entry->Event.SpeakerName.empty())
				caption.Text += entry->Event.SpeakerName + ": ";
			caption.Text += entry->Event.Text;
			caption.Color = entry->SpeakerColor;
			caption.Lines = std::max(lineCount(caption.Text), 1);

			// A line that no longer fits ends the list, but the newest one always shows.
			if (!newestFirst.empty() && used + caption.Lines > static_cast<int>(preferences.MaxLines))
				break;
			used += caption.Lines;
			newestFirst.push_back(std::move(caption));
		}
		return { newestFirst.rbegin(), newestFirst.rend() };
	}

	void CaptureAccessibility(const glm::vec2& screenSize, FrameRenderPacket& packet)
	{
		using Draw2DItem = FrameRenderPacket::Draw2DItem;
		if (!AudioAccessibility::IsActive() || !AudioAccessibility::GetConfig().BuiltInUI || screenSize.x < 1.0f || screenSize.y < 1.0f)
			return;
		const Ref<Font> font = Font::GetDefaultFont();
		if (!font)
			return;

		const AudioAccessibilityPreferences& preferences = AudioAccessibility::GetPreferences();
		const float em = std::max(preferences.TextSize, 1.0f);
		const float width = std::max(1.0f, screenSize.x * 0.8f - 24.0f);
		const float wrap = width / em;

		const auto quad = [&packet](const glm::vec2& min, const glm::vec2& max, const glm::vec4& color)
		{
			Draw2DItem item;
			item.Type = Draw2DItem::Kind::Quad;
			item.Transform = glm::translate(glm::mat4(1.0f), glm::vec3((min + max) * 0.5f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(max - min, 1.0f));
			item.Color = color;
			packet.UI.push_back(std::move(item));
		};
		const auto text = [&packet, &font](const std::string& content, const glm::vec2& topLeft, float emPixels, float maxWidth, const glm::vec4& color)
		{
			Draw2DItem item;
			item.Type = Draw2DItem::Kind::Text;
			item.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(topLeft.x, topLeft.y - Renderer2D::GetStringAscent(font) * emPixels, 0.0f))
				* glm::scale(glm::mat4(1.0f), glm::vec3(emPixels));
			item.Color = color;
			item.FontAsset = font;
			item.Text = content;
			item.MaxWidth = maxWidth;
			packet.UI.push_back(std::move(item));
		};

		const std::vector<CaptionEntry> captions = CollectCaptions(AudioAccessibility::GetSubtitles(), preferences,
			[&](const std::string& content)
			{
				int lines = 1;
				Renderer2D::MeasureString(content, font, wrap, 0.0f, 0.0f, &lines);
				return lines;
			},
			[](const glm::vec3& position) { return AudioAccessibility::DirectionTo(position); });

		if (!captions.empty())
		{
			// A box above the bottom edge with one block of wrapped text per entry, the newest at the bottom.
			std::vector<float> heights;
			float total = 16.0f;
			for (const CaptionEntry& entry : captions)
			{
				heights.push_back(Renderer2D::MeasureString(entry.Text, font, wrap).y * em);
				total += heights.back() + 4.0f;
			}
			total -= 4.0f;
			const float left = screenSize.x * 0.1f;
			const float bottom = screenSize.y * 0.06f;
			quad({ left, bottom }, { left + width + 24.0f, bottom + total }, { 0.08f, 0.08f, 0.1f, preferences.BackgroundOpacity });
			float top = bottom + total - 8.0f;
			for (size_t i = 0; i < captions.size(); ++i)
			{
				text(captions[i].Text, { left + 12.0f, top }, em, wrap, captions[i].Color);
				top -= heights[i] + 4.0f;
			}
		}

		if (preferences.VisualCues)
		{
			// One marker per active sound, on an ellipse around the middle of the screen in its direction.
			const glm::vec2 center = screenSize * 0.5f;
			for (const SoundEvent& cue : AudioAccessibility::GetSoundCues())
			{
				glm::vec2 direction{ cue.Direction.x, cue.Direction.z };
				const float length = glm::length(direction);
				direction = length < 0.001f ? glm::vec2(0.0f, 1.0f) : direction / length;
				const glm::vec2 point = center + direction * glm::vec2(screenSize.x * 0.4f, screenSize.y * 0.35f);
				const glm::vec4 color{ 0.25f, 0.6f, 1.0f, std::clamp(cue.Intensity, 0.2f, 1.0f) };
				const float half = 4.0f + cue.Intensity * 6.0f;
				quad(point - glm::vec2(half), point + glm::vec2(half), color);
				text(AudioCategoryNames[static_cast<size_t>(cue.Category)], { point.x + half + 8.0f, point.y + 8.0f }, 16.0f, 1.0e9f, color);
			}
		}
	}
}
