// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioAccessibilityWidgets.h"
#include "ImGuiUtilities.h"
#include "Colors.h"
#include "Lux/Audio/AudioAccessibility.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Lux::ImGuiEx
{
	bool AudioAccessibilityOptions(AudioAccessibilityPreferences& preferences, bool runtime)
	{
		bool changed = false;
		changed |= ImGui::Checkbox("Subtitles", &preferences.Subtitles);
		changed |= ImGui::Checkbox("Closed captions", &preferences.Captions);
		changed |= ImGui::Checkbox("Visual sound cues", &preferences.VisualCues);
		changed |= ImGui::Checkbox("Speaker names", &preferences.SpeakerNames);
		changed |= ImGui::Checkbox("Directional indicators", &preferences.DirectionIndicators);
		changed |= ImGui::SliderFloat("Text size", &preferences.TextSize, 12, 72, "%.0f px");
		changed |= ImGui::SliderFloat("Background opacity", &preferences.BackgroundOpacity, 0, 1, "%.2f");
		changed |= ImGui::SliderFloat("Display duration", &preferences.DurationMultiplier, 0.5f, 3, "%.1fx");
		int lines = static_cast<int>(preferences.MaxLines);
		if (ImGui::SliderInt("Maximum lines", &lines, 1, 10))
		{
			preferences.MaxLines = static_cast<uint32_t>(lines);
			changed = true;
		}
		ImGui::SeparatorText("Audio");
		for (size_t i = 0; i < AudioCategoryCount; ++i)
		{
			const bool available = !runtime || AudioAccessibility::HasBus(static_cast<AudioCategory>(i));
			ScopedDisable disabled(!available);
			changed |= ImGui::SliderFloat(AudioCategoryNames[i], &preferences.Volumes[i], 0, 1, "%.2f");
			if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("This category needs a loaded FMOD bus mapped in Project Settings > Audio.");
		}
		changed |= ImGui::Checkbox("Mono audio", &preferences.Mono);
		int range = static_cast<int>(preferences.DynamicRange);
		if (ImGui::Combo("Dynamic range", &range, "Full\0Reduced\0Night\0"))
		{
			preferences.DynamicRange = static_cast<AudioDynamicRange>(range);
			changed = true;
		}
		changed |= ImGui::SliderFloat("Dialogue boost", &preferences.DialogueBoost, 1, 2, "%.1fx");
		changed |= ImGui::Checkbox("Audio descriptions", &preferences.AudioDescriptions);
		return changed;
	}

	void AudioAccessibilityMenu(bool& open)
	{
		if (!open || !AudioAccessibility::IsActive() || !AudioAccessibility::GetConfig().BuiltInUI)
			return;
		ImGui::SetNextWindowSize({ 440, 640 }, ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Audio accessibility", &open))
		{
			auto preferences = AudioAccessibility::GetPreferences();
			if (AudioAccessibilityOptions(preferences, true))
				AudioAccessibility::ApplyPreferences(preferences);
			if (ImGui::Button("Save preferences"))
				AudioAccessibility::SavePreferences();
			ImGui::SameLine();
			if (ImGui::Button("Restore project defaults"))
				AudioAccessibility::ApplyPreferences(AudioAccessibility::GetConfig().Defaults);
		}
		ImGui::End();
	}

	void AudioAccessibilityOverlay(const glm::vec2& minimum, const glm::vec2& maximum)
	{
		if (!AudioAccessibility::IsActive() || !AudioAccessibility::GetConfig().BuiltInUI || maximum.x <= minimum.x || maximum.y <= minimum.y)
			return;
		LUX_PROFILE_FUNCTION_AUTO;
		const auto& preferences = AudioAccessibility::GetPreferences();
		auto* draw = ImGui::GetForegroundDrawList();
		draw->PushClipRect({ minimum.x, minimum.y }, { maximum.x, maximum.y }, true);
		const glm::vec2 size = maximum - minimum;
		const float fontSize = preferences.TextSize;
		const float width = std::max(1.0f, size.x * 0.8f - 24.0f);
		const auto& subtitles = AudioAccessibility::GetSubtitles();
		struct Line { std::string Text; glm::vec4 Color; };
		// Retain bounded text storage across frames; ImGui runs on the main thread.
		static std::array<Line, 10> lines;
		static std::string text;
		size_t lineCount = 0;
		// Keep the newest entries. Wrap on font boundaries so maximum lines is an actual line limit.
		for (auto entry = subtitles.rbegin(); entry != subtitles.rend() && lineCount < preferences.MaxLines; ++entry)
		{
			text.clear();
			if (preferences.DirectionIndicators && entry->Event.IsOffScreen)
			{
				const auto direction = AudioAccessibility::DirectionTo(entry->Event.SpeakerPosition);
				text += direction.x < -0.25f ? "< " : direction.x > 0.25f ? "> " : direction.z < 0 ? "[behind] " : "[ahead] ";
			}
			if (preferences.SpeakerNames && !entry->Event.SpeakerName.empty())
				text += entry->Event.SpeakerName + ": ";
			text += entry->Event.Text;
			const char* cursor = text.data();
			const char* end = cursor + text.size();
			while (cursor < end && lineCount < preferences.MaxLines)
			{
				const char* paragraphEnd = std::find(cursor, end, '\n');
				const char* wrap = ImGui::GetFont()->CalcWordWrapPosition(fontSize, cursor, paragraphEnd, width);
				if (wrap <= cursor && cursor < paragraphEnd)
				{
					// Even a viewport narrower than one glyph must advance by a full UTF-8 character.
					wrap = cursor + 1;
					while (wrap < paragraphEnd && (static_cast<unsigned char>(*wrap) & 0xc0) == 0x80)
						++wrap;
				}
				lines[lineCount].Text.assign(cursor, wrap);
				lines[lineCount++].Color = entry->SpeakerColor;
				cursor = wrap;
				while (cursor < end && (*cursor == ' ' || *cursor == '\n' || *cursor == '\r'))
					++cursor;
			}
		}
		if (lineCount != 0)
		{
			const float height = lineCount * (fontSize + 4.0f) + 16.0f;
			const ImVec2 start{ minimum.x + size.x * 0.1f, maximum.y - height - size.y * 0.06f };
			const auto background = ImGui::ColorConvertU32ToFloat4(Colors::Theme::background);
			draw->AddRectFilled(start, { start.x + width + 24.0f, start.y + height }, ImGui::GetColorU32(ImVec4(background.x, background.y, background.z, preferences.BackgroundOpacity)), 6.0f);
			float y = start.y + 8;
			for (size_t i = 0; i < lineCount; ++i)
			{
				const auto& line = lines[i];
				draw->AddText(ImGui::GetFont(), fontSize, { start.x + 12, y }, ImGui::GetColorU32({ line.Color.r, line.Color.g, line.Color.b, line.Color.a }), line.Text.c_str());
				y += fontSize + 4;
			}
		}
		if (preferences.VisualCues)
		{
			const glm::vec2 center = (minimum + maximum) * 0.5f;
			for (const auto& cue : AudioAccessibility::GetSoundCues())
			{
				glm::vec2 direction{ cue.Direction.x, -cue.Direction.z };
				const float length = glm::length(direction);
				if (length < 0.001f)
					direction = { 0, -1 };
				else
					direction /= length;
				const glm::vec2 point = center + direction * glm::vec2(size.x * 0.4f, size.y * 0.35f);
				const auto color = ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent);
				const auto tint = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, std::clamp(cue.Intensity, 0.2f, 1.0f)));
				draw->AddCircleFilled({ point.x, point.y }, 4 + cue.Intensity * 6, tint);
				draw->AddText({ point.x + 12, point.y - 8 }, tint, AudioCategoryNames[static_cast<size_t>(cue.Category)]);
			}
		}
		draw->PopClipRect();
	}
}
