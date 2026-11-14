// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Lux::ImGuiEx
{

	class DpiStyle
	{
	public:
		void Capture(const ImGuiStyle& style)
		{
			m_Base = style;
			m_Scale = 1.0f;
		}

		// Outside a frame. Recompute metrics from the baseline, retaining live theme colours.
		void Apply(ImGuiStyle& style, float scale)
		{
			if (!std::isfinite(scale) || scale <= 0.0f || scale == m_Scale)
				return;
			ImGuiStyle scaled = m_Base;
			scaled.ScaleAllSizes(scale);
			std::copy(std::begin(style.Colors), std::end(style.Colors), std::begin(scaled.Colors));
			scaled.FontScaleMain = style.FontScaleMain;
			scaled.FontScaleDpi = style.FontScaleDpi;
			style = scaled;
			m_Scale = scale;
		}

	private:
		ImGuiStyle m_Base;
		float m_Scale = 1.0f;
	};

	// Scope a whole panel, before its Begin. Restoring the exact style avoids accumulated
	// rounding and keeps a detached window from changing metrics in the main editor.
	class ScopedWindowScale
	{
	public:
		explicit ScopedWindowScale(const char* windowName) : m_Saved(ImGui::GetStyle())
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(windowName);
			const ImGuiViewport* main = ImGui::GetMainViewport();
			if (ImGui::GetIO().ConfigDpiScaleFonts && window && window->Viewport && main->DpiScale > 0.0f)
			{
				const float ratio = window->Viewport->DpiScale / main->DpiScale;
				if (ratio > 0.0f && ratio != 1.0f)
					ImGui::GetStyle().ScaleAllSizes(ratio);
			}
		}

		~ScopedWindowScale()
		{
			ImGui::GetStyle() = m_Saved;
		}
		ScopedWindowScale(const ScopedWindowScale&) = delete;
		ScopedWindowScale& operator=(const ScopedWindowScale&) = delete;

	private:
		ImGuiStyle m_Saved;
	};

} // namespace Lux::ImGuiEx
