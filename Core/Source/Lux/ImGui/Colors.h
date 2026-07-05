#pragma once
#include <imgui/imgui.h>

namespace Colors
{
	// To experiment with editor theme live you can change these constexpr into static
	// members of a static "Theme" class and add a quick ImGui window to adjust the colour values
	namespace Theme
	{
		// LuxEngine editor theme: a cool-graphite dark base with a single luminous
		// indigo-violet accent ("Lux" = light). The accent is used sparingly — selection,
		// active separators, focus, checkmarks — so the UI reads calm and clean rather
		// than busy. Deliberately distinct from Hazel's orange/cyan palette.

		// Signature accent + its cooler/brighter relatives.
		constexpr auto accent = IM_COL32(124, 131, 248, 255);      // #7C83F8 indigo-violet
		constexpr auto highlight = IM_COL32(139, 156, 255, 255);   // brighter periwinkle
		constexpr auto niceBlue = IM_COL32(150, 165, 255, 255);
		constexpr auto compliment = IM_COL32(116, 126, 168, 255);

		// Neutral graphite surfaces (a faint cool/blue tint reads more premium than flat gray).
		constexpr auto background = IM_COL32(30, 31, 37, 255);
		constexpr auto backgroundDark = IM_COL32(21, 22, 27, 255);
		constexpr auto titlebar = IM_COL32(17, 18, 22, 255);
		constexpr auto propertyField = IM_COL32(13, 14, 17, 255);
		constexpr auto groupHeader = IM_COL32(38, 40, 48, 255);
		constexpr auto backgroundPopup = IM_COL32(40, 42, 50, 255);

		// Play/pause/stop and status titlebars (kept semantic, just refined).
		constexpr auto titlebarOrange = IM_COL32(197, 121, 45, 255);
		constexpr auto titlebarGreen = IM_COL32(46, 121, 78, 255);
		constexpr auto titlebarRed = IM_COL32(192, 64, 64, 255);

		// Text: slightly cool whites with clear hierarchy.
		constexpr auto text = IM_COL32(199, 202, 213, 255);
		constexpr auto textBrighter = IM_COL32(226, 228, 238, 255);
		constexpr auto textDarker = IM_COL32(118, 122, 138, 255);
		constexpr auto textError = IM_COL32(232, 84, 84, 255);
		constexpr auto muted = IM_COL32(70, 73, 86, 255);

		// Selection derives from the accent so highlighted rows/text stay on-brand.
		constexpr auto selection = IM_COL32(124, 131, 248, 255);
		constexpr auto selectionMuted = IM_COL32(124, 131, 248, 38);

		// Asset-status semantic colours.
		constexpr auto validPrefab = IM_COL32(124, 156, 232, 255);
		constexpr auto invalidPrefab = IM_COL32(222, 64, 64, 255);
		constexpr auto missingMesh = IM_COL32(226, 112, 86, 255);
		constexpr auto meshNotSet = IM_COL32(232, 146, 60, 255);
	}
}
