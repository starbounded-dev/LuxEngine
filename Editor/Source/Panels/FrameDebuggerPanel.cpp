#include "lpch.h"
#include "FrameDebuggerPanel.h"

#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiCore.h"

#include <imgui/imgui.h>

#include <algorithm>

namespace Lux {

	namespace {

		using DebugViewMode = SceneRenderer::DebugViewMode;

		struct DebugTarget
		{
			const char* Name;
			DebugViewMode Mode;
			ImGuiEx::ImageMode Display;
		};

		// A curated view over SceneRenderer::GetDebugViewImage — the public accessor that maps each
		// mode to its (renderer-owned) image and returns null when the feature is off.
		constexpr DebugTarget kTargets[] = {
			{ "Final Composite",        DebugViewMode::Final,            ImGuiEx::ImageMode::Normal },
			{ "Scene Colour (HDR)",     DebugViewMode::Geometry,         ImGuiEx::ImageMode::Normal },
			{ "Deferred Lighting",      DebugViewMode::DeferredLighting, ImGuiEx::ImageMode::Normal },
			{ "G-Buffer: Base Colour",  DebugViewMode::GBufferBaseColor, ImGuiEx::ImageMode::Normal },
			{ "G-Buffer: Normals",      DebugViewMode::GBufferNormal,    ImGuiEx::ImageMode::Normal },
			{ "G-Buffer: Metal / Rough",DebugViewMode::GBufferMetalRough,ImGuiEx::ImageMode::Normal },
			{ "G-Buffer: Material ID",  DebugViewMode::GBufferMaterialID,ImGuiEx::ImageMode::Normal },
			{ "G-Buffer: Object ID",    DebugViewMode::GBufferObjectID,  ImGuiEx::ImageMode::Normal },
			{ "Depth",                  DebugViewMode::Depth,            ImGuiEx::ImageMode::Depth },
			{ "Ambient Occlusion",      DebugViewMode::AO,               ImGuiEx::ImageMode::Normal },
			{ "Screen-Space Reflections", DebugViewMode::SSR,            ImGuiEx::ImageMode::Normal },
			{ "Bloom",                  DebugViewMode::Bloom,            ImGuiEx::ImageMode::Normal },
		};

		constexpr int kTargetCount = static_cast<int>(sizeof(kTargets) / sizeof(kTargets[0]));

	}

	Ref<Image2D> FrameDebuggerPanel::FetchTarget(int target)
	{
		if (!m_Context || target < 0 || target >= kTargetCount)
			return nullptr;
		return m_Context->GetDebugViewImage(kTargets[target].Mode);
	}

	void FrameDebuggerPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		if (!ImGui::Begin(LUX_ICON_PICTURE_O "  Frame Debugger", &isOpen))
		{
			ImGui::End();
			return;
		}

		if (!m_Context || !m_Context->IsReady())
		{
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "Renderer not ready.");
			ImGui::End();
			return;
		}

		m_SelectedTarget = std::clamp(m_SelectedTarget, 0, kTargetCount - 1);

		// ---- Controls ----
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo("##fd_target", kTargets[m_SelectedTarget].Name))
		{
			for (int i = 0; i < kTargetCount; i++)
			{
				if (ImGui::Selectable(kTargets[i].Name, i == m_SelectedTarget))
					m_SelectedTarget = i;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::Checkbox("Fit", &m_FitToWindow);
		if (!m_FitToWindow)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::SliderFloat("Zoom", &m_Zoom, 0.1f, 8.0f, "%.2fx");
		}

		Ref<Image2D> image = FetchTarget(m_SelectedTarget);
		if (!image)
		{
			ImGui::Spacing();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker),
				"Target unavailable (its feature may be off, or the renderer hasn't produced it this frame).");
			ImGui::End();
			return;
		}

		const float imageWidth = static_cast<float>(image->GetWidth());
		const float imageHeight = static_cast<float>(image->GetHeight());

		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "%.0f x %.0f", imageWidth, imageHeight);
		ImGui::Spacing();

		// ---- Image, in a scrollable child so a zoomed-in target can be panned ----
		if (ImGui::BeginChild("##fd_image", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
		{
			if (imageWidth > 0.0f && imageHeight > 0.0f)
			{
				float scale = m_Zoom;
				if (m_FitToWindow)
				{
					const ImVec2 avail = ImGui::GetContentRegionAvail();
					scale = std::min(avail.x / imageWidth, avail.y / imageHeight);
					if (scale <= 0.0f)
						scale = 1.0f;
				}

				const ImVec2 drawSize(imageWidth * scale, imageHeight * scale);

				// Centre horizontally when the scaled image is narrower than the region.
				const float avail = ImGui::GetContentRegionAvail().x;
				if (drawSize.x < avail)
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - drawSize.x) * 0.5f);

				// uv (0,0)-(1,1), no Y-flip — matches how the viewport blits the final image.
				ImGuiEx::Image(image, drawSize, ImVec2(0, 0), ImVec2(1, 1), kTargets[m_SelectedTarget].Display);
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}

}
