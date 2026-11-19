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
				ImGui::SetTooltip("This category needs a mixer bus mapped in Project Settings > Audio.");
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
}
