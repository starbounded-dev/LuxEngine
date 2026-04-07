#include "lpch.h"
#include "RenderStatsPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/ImGui/ImGuiEx.h"

#include <imgui/imgui.h>

namespace Lux {

	void RenderStatsPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		ImGui::Begin("Render Statistics", &isOpen);

		ImGuiIO& io = ImGui::GetIO();

		// Frame time and FPS
		ImGui::Text("Performance");
		ImGui::Separator();
		ImGui::Text("Frame Time: %.3f ms", 1000.0f / io.Framerate);
		ImGui::Text("FPS: %.1f", io.Framerate);

		ImGui::Spacing();
		ImGui::Spacing();

		// Renderer2D Stats
		if (m_Renderer2D)
		{
			ImGui::Text("Renderer2D");
			ImGui::Separator();
			
			auto stats = m_Renderer2D->GetDrawStats();
			ImGui::Text("Draw Calls: %d", stats.DrawCalls);
			ImGui::Text("Quads: %d", stats.QuadCount);
			ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
			ImGui::Text("Indices: %d", stats.GetTotalIndexCount());
		}

		ImGui::Spacing();
		ImGui::Spacing();

		// SceneRenderer Stats
		if (m_SceneRenderer)
		{
			ImGui::Text("SceneRenderer (3D)");
			ImGui::Separator();
			
			const auto& stats = m_SceneRenderer->GetStatistics();
			ImGui::Text("Draw Calls: %d", stats.DrawCalls);
			ImGui::Text("Meshes: %d", stats.Meshes);
			ImGui::Text("Instances: %d", stats.Instances);
			ImGui::Text("GPU Time: %.3f ms", stats.TotalGPUTime);

			ImGui::Spacing();
			
			// SceneRenderer Options
			if (ImGui::CollapsingHeader("Renderer Options"))
			{
				ImGuiEx::BeginPropertyGrid();
				auto& options = m_SceneRenderer->GetOptions();
				ImGuiEx::Property("Show Grid", options.ShowGrid);
				ImGuiEx::Property("Show Physics Colliders", options.ShowPhysicsColliders);
				ImGuiEx::Property("Show Selected Wireframe", options.ShowSelectedInWireframe);
				ImGuiEx::Property("Soft Shadows", options.SoftShadows);
				ImGuiEx::Property("Max Shadow Distance", options.MaxShadowDistance, 1.0f, 0.0f, 1000.0f);
				ImGuiEx::Property("Shadow Fade", options.ShadowFade, 0.01f, 0.0f, 1.0f);
				ImGuiEx::EndPropertyGrid();
			}
		}

		ImGui::Spacing();
		ImGui::Spacing();

		// Memory Stats (placeholder - would need integration with allocator)
		ImGui::Text("Memory");
		ImGui::Separator();
		ImGui::Text("(Memory tracking not yet implemented)");

		ImGui::End();
	}

}
