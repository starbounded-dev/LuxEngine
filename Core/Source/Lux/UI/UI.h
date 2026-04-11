#pragma once

#include <imgui/imgui.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiUtilities.h"

namespace Lux::UI {

	// =========================================================================
	// Scoped helpers — mirrors the Hazel UI:: namespace conventions
	// =========================================================================

	/// RAII wrapper for ImGui::PushStyleColor / PopStyleColor
	struct ScopedColour
	{
		ScopedColour() = default;

		ScopedColour(ImGuiCol idx, ImVec4 colour, bool predicate = true)
			: m_Set(predicate)
		{
			if (predicate)
				ImGui::PushStyleColor(idx, colour);
		}

		ScopedColour(ImGuiCol idx, ImU32 colour, bool predicate = true)
			: m_Set(predicate)
		{
			if (predicate)
				ImGui::PushStyleColor(idx, colour);
		}

		ScopedColour(ImGuiCol idx, ImColor colour, bool predicate = true)
			: m_Set(predicate)
		{
			if (predicate)
				ImGui::PushStyleColor(idx, colour.Value);
		}

		~ScopedColour()
		{
			if (m_Set)
				ImGui::PopStyleColor();
		}

		ScopedColour(const ScopedColour&) = delete;
		ScopedColour& operator=(const ScopedColour&) = delete;
	private:
		bool m_Set = false;
	};

	// Legacy alias kept for backwards compatibility
	using ScopedStyleColor = ScopedColour;

	/// RAII wrapper for ImGui::PushStyleVar / PopStyleVar
	struct ScopedStyle
	{
		ScopedStyle(ImGuiStyleVar styleVar, float value) { ImGui::PushStyleVar(styleVar, value); }
		ScopedStyle(ImGuiStyleVar styleVar, ImVec2 value) { ImGui::PushStyleVar(styleVar, value); }
		~ScopedStyle() { ImGui::PopStyleVar(); }

		ScopedStyle(const ScopedStyle&) = delete;
		ScopedStyle& operator=(const ScopedStyle&) = delete;
	};

	/// RAII wrapper for ImGui::PushFont / PopFont
	struct ScopedFont
	{
		ScopedFont(ImFont* font) { ImGui::PushFont(font); }
		~ScopedFont() { ImGui::PopFont(); }

		ScopedFont(const ScopedFont&) = delete;
		ScopedFont& operator=(const ScopedFont&) = delete;
	};

	// =========================================================================
	// Cursor helpers
	// =========================================================================

	/// Shift the cursor position along the X axis by the given amount
	inline void ShiftCursorX(float distance)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + distance);
	}

	/// Shift the cursor position along the Y axis by the given amount
	inline void ShiftCursorY(float distance)
	{
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + distance);
	}

	/// Shift the cursor position along both axes
	inline void ShiftCursor(float x, float y)
	{
		const ImVec2 cursor = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(cursor.x + x, cursor.y + y));
	}

	// =========================================================================
	// DrawButtonImage — draws a texture directly into an already-placed item
	// rect, choosing the tint based on the item interaction state.
	// =========================================================================

	/// Draw a button image using separate textures for each interaction state.
	/// Call this *after* an invisible button or any other item that occupies the
	/// desired rect, then the tint colour is picked automatically.
	inline void DrawButtonImage(
		const Ref<Texture2D>& imageNormal,
		const Ref<Texture2D>& imageHovered,
		const Ref<Texture2D>& imagePressed,
		ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
		ImVec2 rectMin, ImVec2 rectMax,
		ImVec2 uv0 = ImVec2(0.0f, 0.0f), ImVec2 uv1 = ImVec2(1.0f, 1.0f))
	{
		// Delegate to ImGuiEx which has the full implementation
		ImGuiEx::DrawButtonImage(imageNormal, imageHovered, imagePressed,
			tintNormal, tintHovered, tintPressed, rectMin, rectMax, uv0, uv1);
	}

	/// Convenience overload — single texture for all states
	inline void DrawButtonImage(
		const Ref<Texture2D>& image,
		ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
		ImVec2 rectMin, ImVec2 rectMax,
		ImVec2 uv0 = ImVec2(0.0f, 0.0f), ImVec2 uv1 = ImVec2(1.0f, 1.0f))
	{
		ImGuiEx::DrawButtonImage(image, tintNormal, tintHovered, tintPressed,
			rectMin, rectMax, uv0, uv1);
	}

	/// Convenience overload — uses the last item rect for the image bounds
	inline void DrawButtonImage(
		const Ref<Texture2D>& image,
		ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
		ImVec2 uv0 = ImVec2(0.0f, 0.0f), ImVec2 uv1 = ImVec2(1.0f, 1.0f))
	{
		ImGuiEx::DrawButtonImage(image, tintNormal, tintHovered, tintPressed, uv0, uv1);
	}

}
