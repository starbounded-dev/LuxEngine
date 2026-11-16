// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ImGuiFonts.h"

namespace Lux::ImGuiEx {

	static std::unordered_map<std::string, ImFont*> s_Fonts;
	static float s_Scale = 1.0f;

	void Fonts::SetScale(float scale)
	{
		if (scale > 0.0f)
			s_Scale = scale;
	}

	float Fonts::GetScale()
	{
		return s_Scale;
	}

	void Fonts::Add(const FontConfiguration& config, bool isDefault)
	{
		if (s_Fonts.find(config.FontName) != s_Fonts.end())
		{
			LUX_CORE_WARN_TAG("EditorUI", "Tried to add font with name '{0}' but that name is already taken!", config.FontName);
			return;
		}

		ImFontConfig imguiFontConfig;
		imguiFontConfig.MergeMode = config.MergeWithLast;
		auto& io = ImGui::GetIO();
		// Register the base size; ImGui 1.92 bakes at the size selected by viewport DPI.
		const float baseSize = config.Size * s_Scale;
		ImFont* font = io.Fonts->AddFontFromFileTTF(config.FilePath.data(), baseSize, &imguiFontConfig, config.GlyphRanges == nullptr ? io.Fonts->GetGlyphRangesDefault() : config.GlyphRanges);
		LUX_CORE_VERIFY(font, "Failed to load font file!");
		s_Fonts[config.FontName] = font;

		if (isDefault)
			io.FontDefault = font;
	}

	ImFont* Fonts::Get(const std::string& fontName)
	{
		LUX_CORE_VERIFY(s_Fonts.find(fontName) != s_Fonts.end(), "Failed to find font with that name!");
		return s_Fonts.at(fontName);
	}

	void Fonts::PushFont(const std::string& fontName)
	{
		const auto& io = ImGui::GetIO();

		if (s_Fonts.find(fontName) == s_Fonts.end())
		{
			ImGui::PushFont(io.FontDefault);
			return;
		}

		ImGui::PushFont(s_Fonts.at(fontName));
	}

	void Fonts::PopFont()
	{
		ImGui::PopFont();
	}

}

