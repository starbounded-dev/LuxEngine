#include "lpch.h"
#include "SceneRendererPanel.h"

#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Lux {

	namespace {

		void DrawStat(const char* label, const char* value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("%s", label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value);
		}

		void DrawStat(const char* label, uint32_t value)
		{
			DrawStat(label, std::to_string(value).c_str());
		}

		void DrawStat(const char* label, float value, const char* suffix = "")
		{
			const std::string text = std::to_string(value) + suffix;
			DrawStat(label, text.c_str());
		}

	}

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
		auto& bloom = m_Context->GetBloomSettings();
		auto& dof = m_Context->GetDOFSettings();
		auto& ssr = m_Context->GetSSROptions();

		if (ImGui::BeginTable("##scene_renderer_summary", 2, ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Ready", m_Context->IsReady() ? "Yes" : "No");
			DrawStat("Technique", RenderingTechniqueToString(m_Context->GetRenderingTechnique()));
			DrawStat("Viewport", (std::to_string(m_Context->GetViewportWidth()) + " x " + std::to_string(m_Context->GetViewportHeight())).c_str());
			DrawStat("GPU Time", stats.TotalGPUTime, " ms");
			ImGui::EndTable();
		}

		ImGui::Spacing();
		if (ImGui::Button("Reload All Shaders"))
			m_LastReloadedShaderCount = Renderer::ReloadShaders(true);
		if (m_LastReloadedShaderCount > 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Reloaded %u shader%s", m_LastReloadedShaderCount, m_LastReloadedShaderCount == 1 ? "" : "s");
		}

		ImGui::Spacing();

		if (ImGuiEx::PropertyGridHeader("Shaders", false))
		{
			Ref<ShaderLibrary> shaderLibrary = Renderer::GetShaderLibrary();
			if (!shaderLibrary)
			{
				ImGui::TextDisabled("Shader library is not available.");
				ImGui::TreePop();
			}
			else
			{
				if (ImGui::Button("Reload All Shaders##section"))
					m_LastReloadedShaderCount = Renderer::ReloadShaders(true);

				ImGui::SameLine();
				ImGui::TextDisabled("Forces every loaded shader to recompile and reload.");

				ImGuiEx::Widgets::SearchWidget(m_ShaderSearch, "Search shaders...");

				std::vector<std::pair<std::string, Ref<Shader>>> shaders;
				shaders.reserve(shaderLibrary->GetShaders().size());
				for (const auto& [name, shader] : shaderLibrary->GetShaders())
				{
					if (!shader)
						continue;
					if (!ImGuiEx::IsMatchingSearch(name, m_ShaderSearch, false, false, true))
						continue;
					shaders.emplace_back(name, shader);
				}

				std::sort(shaders.begin(), shaders.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

				if (ImGui::BeginTable("##shader_table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("Shader");
					ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 86.0f);
					ImGui::TableHeadersRow();

					for (auto& [name, shader] : shaders)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(name.c_str());

						ImGui::TableSetColumnIndex(1);
						ImGui::PushID(name.c_str());
						if (ImGui::SmallButton("Reload"))
						{
							shader->Reload(true);
							m_LastReloadedShaderCount = 1;
						}
						ImGui::PopID();
					}

					ImGui::EndTable();
				}

				if (shaders.empty())
					ImGui::TextDisabled("No matching shaders.");

				ImGui::TreePop();
			}
		}

		if (ImGuiEx::PropertyGridHeader("Visualization", true))
		{
			ImGuiEx::BeginPropertyGrid();
			ImGuiEx::Property("Show Grid", options.ShowGrid);
			ImGuiEx::Property("Show Selected Wireframe", options.ShowSelectedInWireframe);
			ImGuiEx::Property("Show Physics Colliders", options.ShowPhysicsColliders);
			ImGuiEx::Property("Show Shadow Cascades", options.ShowShadowCascades);
			ImGuiEx::Property("Show Light Complexity", options.ShowLightComplexity);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Rendering", true))
		{
			ImGuiEx::BeginPropertyGrid();
			ImGuiEx::Property("Frustum Culling", options.EnableFrustumCulling);
			ImGuiEx::Property("GPU Driven Indirect", options.EnableGPUDrivenRendering);
			ImGuiEx::Property("GTAO", options.EnableGTAO);
			ImGuiEx::Property("SSR", options.EnableSSR);
			ImGuiEx::Property("Jump Flood Outline", options.EnableJumpFlood);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Shadows", true))
		{
			ImGuiEx::BeginPropertyGrid();
			ImGuiEx::Property("Soft Shadows", options.SoftShadows);
			ImGuiEx::Property("Max Distance", options.MaxShadowDistance, 1.0f, 1.0f, 1000.0f);
			ImGuiEx::Property("Distance Fade", options.ShadowFade, 0.25f, 0.01f, 250.0f);
			ImGuiEx::Property("Split Lambda", options.ShadowCascadeSplitLambda, 0.01f, 0.0f, 1.0f);
			ImGuiEx::Property("Near Offset", options.ShadowCascadeNearPlaneOffset, 0.1f, 0.0f, 200.0f);
			ImGuiEx::Property("Far Offset", options.ShadowCascadeFarPlaneOffset, 0.5f, 0.0f, 500.0f);
			ImGuiEx::Property("Cascade Fade", options.ShadowCascadeTransitionFade, 0.05f, 0.0f, 25.0f);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Post FX", false))
		{
			ImGuiEx::BeginPropertyGrid();
			ImGuiEx::Property("Bloom", bloom.Enabled);
			ImGuiEx::Property("Bloom Threshold", bloom.Threshold, 0.01f, 0.0f, 25.0f);
			ImGuiEx::Property("Bloom Knee", bloom.Knee, 0.01f, 0.0f, 1.0f);
			ImGuiEx::Property("Bloom Intensity", bloom.Intensity, 0.01f, 0.0f, 10.0f);
			ImGuiEx::Property("DOF", dof.Enabled);
			ImGuiEx::Property("DOF Focus Distance", dof.FocusDistance, 0.1f, 0.0f, 1000.0f);
			ImGuiEx::Property("DOF Blur Size", dof.BlurSize, 0.05f, 0.0f, 20.0f);
			ImGuiEx::Property("SSR Half Res", ssr.HalfRes);
			int32_t ssrMaxSteps = ssr.MaxSteps;
			if (ImGuiEx::Property("SSR Max Steps", ssrMaxSteps, 1, 256))
				ssr.MaxSteps = ssrMaxSteps;
			ImGuiEx::Property("SSR Brightness", ssr.Brightness, 0.01f, 0.0f, 5.0f);
			ImGuiEx::Property("SSR Depth Tolerance", ssr.DepthTolerance, 0.01f, 0.0f, 5.0f);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Render Statistics", true))
		{
			if (ImGui::BeginTable("##renderer_stats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				DrawStat("Draw Calls", stats.DrawCalls);
				DrawStat("Meshes", stats.Meshes);
				DrawStat("Instances", stats.Instances);
				DrawStat("Visible Instances", stats.VisibleInstances);
				DrawStat("Culled Instances", stats.CulledInstances);
				DrawStat("Saved Draws", stats.SavedDraws);
				DrawStat("Indirect Draws", stats.IndirectDraws);
				DrawStat("GPU Time", stats.TotalGPUTime, " ms");
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}

		ImGui::End();
	}

}
