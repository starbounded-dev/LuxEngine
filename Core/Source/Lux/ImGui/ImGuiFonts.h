// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <imgui/imgui.h>

namespace Lux::ImGuiEx {

	struct FontConfiguration
	{
		std::string FontName;
		std::string_view FilePath;
		float Size = 16.0f;
		const ImWchar* GlyphRanges = nullptr;
		bool MergeWithLast = false;
	};

	class Fonts
	{
	public:
		// Multiplier for registered logical sizes, set before Add(). The editor uses 1:
		// ImGui 1.92 dynamically bakes each viewport's requested physical font size.
		static void SetScale(float scale);
		static float GetScale();

		static void Add(const FontConfiguration& config, bool isDefault = false);
		static void PushFont(const std::string& fontName);
		static void PopFont();
		static ImFont* Get(const std::string& fontName);
	};

}
