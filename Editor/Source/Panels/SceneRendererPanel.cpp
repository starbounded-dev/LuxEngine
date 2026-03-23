#include "lpch.h"
#include "SceneRendererPanel.h"

#include <imgui/imgui.h>

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
		ImGui::Text("Viewport: %u x %u", m_Context->GetViewportWidth(), m_Context->GetViewportHeight());

		ImGui::Checkbox("Show Grid", &options.ShowGrid);
		ImGui::Checkbox("Show Selected In Wireframe", &options.ShowSelectedInWireframe);
		ImGui::Checkbox("Show Physics Colliders", &options.ShowPhysicsColliders);

		ImGui::Separator();
		ImGui::Text("Draw Calls: %u", stats.DrawCalls);
		ImGui::Text("Meshes: %u", stats.Meshes);
		ImGui::Text("Instances: %u", stats.Instances);

		ImGui::End();
	}

}
