#include "lpch.h"
#include "SceneRendererPanel.h"

#include <imgui/imgui.h>
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Renderer/Renderer.h"

namespace Lux {

	void SceneRendererPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Scene Renderer", &isOpen))
		{
			ImGui::End();
			return;
		}

		if (!m_Context)
		{
			ImGui::TextDisabled("No SceneRenderer context.");
			ImGui::End();
			return;
		}

		auto& options = m_Context->GetOptions();
		const auto& stats = m_Context->GetStatistics();

		ImGui::Text("Ready: %s", m_Context->IsReady() ? "Yes" : "No");
		ImGui::Text("Technique: %s", RenderingTechniqueToString(m_Context->GetRenderingTechnique()));
		ImGui::Text("Viewport: %u x %u", m_Context->GetViewportWidth(), m_Context->GetViewportHeight());
		if (ImGui::Button("Reload Shaders"))
			Renderer::ReloadShaders(true);

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("Show Grid", options.ShowGrid);
		ImGuiEx::Property("Show Selected In Wireframe", options.ShowSelectedInWireframe);
		ImGuiEx::Property("Show Physics Colliders", options.ShowPhysicsColliders);
		ImGuiEx::Property("Frustum Culling", options.EnableFrustumCulling);
		ImGuiEx::Property("GPU Driven Indirect", options.EnableGPUDrivenRendering);
		ImGuiEx::Property("GTAO", options.EnableGTAO);
		ImGuiEx::Property("SSR", options.EnableSSR);
		ImGuiEx::Property("Jump Flood Outline", options.EnableJumpFlood);
		ImGuiEx::EndPropertyGrid();

		ImGui::Separator();
		ImGui::Text("Draw Calls: %u", stats.DrawCalls);
		ImGui::Text("Meshes: %u", stats.Meshes);
		ImGui::Text("Instances: %u", stats.Instances);
		ImGui::Text("Visible Instances: %u", stats.VisibleInstances);
		ImGui::Text("Culled Instances: %u", stats.CulledInstances);
		ImGui::Text("Indirect Draws: %u", stats.IndirectDraws);

		ImGui::End();
	}

}
