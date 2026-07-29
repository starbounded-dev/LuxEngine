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
		// Multiplier applied to every FontConfiguration::Size at bake time, so glyphs are
		// rasterised at the physical pixel size they will actually be drawn at on a HiDPI
		// display. Must be set before any Add() call - the atlas is static, so sizes cannot
		// change afterwards. See ImGuiLayer::OnAttach for how the value is derived.
		static void SetScale(float scale);
		static float GetScale();

		static void Add(const FontConfiguration& config, bool isDefault = false);
		static void PushFont(const std::string& fontName);
		static void PopFont();
		static ImFont* Get(const std::string& fontName);
	};

}
