// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include <imgui/imgui.h>

namespace Colors
{
	// To experiment with editor theme live you can change these constexpr into static
	// members of a static "Theme" class and add a quick ImGui window to adjust the colour values
	namespace Theme
	{
		// LuxEngine editor theme: a warm-graphite dark base ("Monolith, warmed") with a
		// single luminous lime accent ("Lux" = light). The accent is used sparingly —
		// selection, active separators, focus, checkmarks — so the UI reads calm and
		// restrained rather than busy. Deliberately distinct from Hazel's orange/cyan
		// palette, and from this engine's own earlier cool-graphite/indigo theme.

		// Signature accent + its brighter relative. Monochrome otherwise — no secondary hue.
		constexpr auto accent = IM_COL32(200, 255, 77, 255);        // #C8FF4D acid lime
		constexpr auto highlight = IM_COL32(214, 255, 140, 255);    // brighter lime

		// Neutral warm-graphite surfaces (a faint warm/amber tint reads more crafted than flat gray).
		constexpr auto background = IM_COL32(34, 31, 28, 255);
		constexpr auto backgroundDark = IM_COL32(24, 22, 20, 255);
		constexpr auto titlebar = IM_COL32(20, 18, 16, 255);
		constexpr auto propertyField = IM_COL32(16, 14, 12, 255);
		constexpr auto groupHeader = IM_COL32(43, 39, 35, 255);
		constexpr auto backgroundPopup = IM_COL32(45, 41, 36, 255);
		constexpr auto backgroundHover = IM_COL32(51, 46, 41, 255);     // hovered row/button on a popup surface
		constexpr auto backgroundFooter = IM_COL32(41, 37, 31, 255);    // action strip at the bottom of a popup
		constexpr auto borderSubtle = IM_COL32(58, 52, 46, 255);        // hairline between surfaces
		constexpr auto backdropDim = IM_COL32(12, 11, 10, 158);         // dims the editor behind a modal surface

		// Play/pause/stop and status titlebars (kept semantic, just refined).
		constexpr auto titlebarOrange = IM_COL32(197, 121, 45, 255);
		constexpr auto titlebarGreen = IM_COL32(46, 121, 78, 255);
		constexpr auto titlebarRed = IM_COL32(192, 64, 64, 255);

		// Text: warm-tinted whites with clear hierarchy.
		constexpr auto text = IM_COL32(210, 204, 196, 255);
		constexpr auto textBrighter = IM_COL32(236, 230, 220, 255);
		constexpr auto textDarker = IM_COL32(138, 128, 116, 255);
		constexpr auto textCaption = IM_COL32(163, 152, 137, 255);      // secondary text that must stay readable (4.5:1 on popups)
		constexpr auto textWarning = IM_COL32(240, 138, 108, 255);
		constexpr auto textError = IM_COL32(232, 84, 84, 255);
		constexpr auto muted = IM_COL32(86, 78, 68, 255);

		// Selection derives from the accent so highlighted rows/text stay on-brand.
		constexpr auto selection = IM_COL32(200, 255, 77, 255);
		constexpr auto selectionMuted = IM_COL32(200, 255, 77, 38);

		// Accent-tinted overlays for non-widget hover/active states (tabs, separators) —
		// same derivation as selectionMuted, different alphas per use site.
		constexpr auto accentTabHovered = IM_COL32(200, 255, 77, 45);
		constexpr auto accentTabActive = IM_COL32(200, 255, 77, 90);
		constexpr auto accentSeparatorHovered = IM_COL32(200, 255, 77, 150);

		// Asset-status semantic colours.
		constexpr auto validPrefab = IM_COL32(124, 156, 232, 255);
		constexpr auto invalidPrefab = IM_COL32(222, 64, 64, 255);
		constexpr auto missingMesh = IM_COL32(226, 112, 86, 255);
		constexpr auto meshNotSet = IM_COL32(232, 146, 60, 255);
	}
}
