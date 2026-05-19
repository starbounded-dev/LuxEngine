#include "lpch.h"
#include "SceneRendererPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdio>
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

		void DrawStat(const char* label, uint64_t value)
		{
			DrawStat(label, std::to_string(value).c_str());
		}

		void DrawStat(const char* label, float value, const char* suffix = "")
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.3f%s", value, suffix);
			DrawStat(label, buffer);
		}

		float PercentOf(float value, float total)
		{
			return total > 0.0f ? (value / total) * 100.0f : 0.0f;
		}

	}

	void SceneRendererPanel::SetContext(const Ref<SceneRenderer>& context)
	{
		m_Context = context;
		ApplyProjectSettingsToContext();
	}

	void SceneRendererPanel::ApplyProjectSettingsToContext()
	{
		Ref<Project> project = Project::GetActive();
		if (!m_Context || !project)
			return;

		m_Context->ApplyProjectSettings(project->GetConfig().SceneRenderer);
	}

	void SceneRendererPanel::SyncProjectSettingsFromContext()
	{
		Ref<Project> project = Project::GetActive();
		if (!m_Context || !project)
			return;

		m_Context->WriteProjectSettings(project->GetConfig().SceneRenderer);
		m_ProjectRendererSettingsDirty = true;
	}

	bool SceneRendererPanel::SaveProjectRendererSettings()
	{
		Ref<Project> project = Project::GetActive();
		if (!m_Context || !project || project->GetProjectFilePath().empty())
			return false;

		SyncProjectSettingsFromContext();
		if (!Project::SaveActive(project->GetProjectFilePath()))
			return false;

		m_ProjectRendererSettingsDirty = false;
		return true;
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
		const auto& appTimers = Application::Get().GetPerformanceTimers();
		bool projectSettingsChanged = false;

		if (ImGui::BeginTable("##scene_renderer_summary", 2, ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Ready", m_Context->IsReady() ? "Yes" : "No");
			DrawStat("Technique", RenderingTechniqueToString(m_Context->GetRenderingTechnique()));
			DrawStat("Viewport", (std::to_string(m_Context->GetViewportWidth()) + " x " + std::to_string(m_Context->GetViewportHeight())).c_str());
			DrawStat("CPU Time", stats.TotalCPUTime, " ms");
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

		if (Ref<Project> project = Project::GetActive())
		{
			ImGui::SameLine();
			const bool canSaveProject = !project->GetProjectFilePath().empty();
			if (!canSaveProject)
				ImGui::BeginDisabled();

			if (ImGui::Button("Save Renderer Settings"))
				SaveProjectRendererSettings();

			if (!canSaveProject)
				ImGui::EndDisabled();

			if (m_ProjectRendererSettingsDirty)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("Unsaved renderer settings");
			}
		}

		ImGui::Spacing();

		if (ImGuiEx::PropertyGridHeader("Profiling", true))
		{
			uint32_t activePassCount = 0;
			float profiledGPUTime = 0.0f;
			for (const auto& passProfile : stats.PassProfiles)
			{
				if (!passProfile.Active && passProfile.GPUTime <= 0.0f)
					continue;

				activePassCount++;
				profiledGPUTime += passProfile.GPUTime;
			}

			if (ImGui::BeginTable("##profiling_frame", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				DrawStat("Scene CPU Pass Sum", stats.TotalCPUTime, " ms");
				DrawStat("Scene GPU Command Buffer", stats.TotalGPUTime, " ms");
				DrawStat("Profiled GPU Pass Sum", profiledGPUTime, " ms");
				DrawStat("CPU/GPU Delta", stats.TotalCPUTime - stats.TotalGPUTime, " ms");
				DrawStat("Profiled Passes", activePassCount);
				DrawStat("Main Thread Work", appTimers.MainThreadWorkTime, " ms");
				DrawStat("Main Thread Wait", appTimers.MainThreadWaitTime, " ms");
				DrawStat("Render Thread Work", appTimers.RenderThreadWorkTime, " ms");
				DrawStat("Render Thread Wait", appTimers.RenderThreadWaitTime, " ms");
				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Pass Timings");
			if (ImGui::BeginTable("##profiling_passes", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Pass");
				ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 82.0f);
				ImGui::TableSetupColumn("CPU %", ImGuiTableColumnFlags_WidthFixed, 62.0f);
				ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 82.0f);
				ImGui::TableSetupColumn("GPU %", ImGuiTableColumnFlags_WidthFixed, 62.0f);
				ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableHeadersRow();

				for (const auto& passProfile : stats.PassProfiles)
				{
					const bool active = passProfile.Active || passProfile.GPUTime > 0.0f;
					ImGui::TableNextRow();
					if (!active)
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(passProfile.Name);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%.3f", passProfile.CPUTime);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%.1f%%", PercentOf(passProfile.CPUTime, stats.TotalCPUTime));
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%.3f", passProfile.GPUTime);
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%.1f%%", PercentOf(passProfile.GPUTime, stats.TotalGPUTime));
					ImGui::TableSetColumnIndex(5);
					ImGui::TextUnformatted(active ? "Active" : "Idle");

					if (!active)
						ImGui::PopStyleColor();
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Pipeline Counters");
			if (ImGui::BeginTable("##profiling_pipeline", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				DrawStat("Input Vertices", stats.PipelineStats.InputAssemblyVertices);
				DrawStat("Input Primitives", stats.PipelineStats.InputAssemblyPrimitives);
				DrawStat("Vertex Shader Invocations", stats.PipelineStats.VertexShaderInvocations);
				DrawStat("Clipping Invocations", stats.PipelineStats.ClippingInvocations);
				DrawStat("Clipping Primitives", stats.PipelineStats.ClippingPrimitives);
				DrawStat("Fragment Shader Invocations", stats.PipelineStats.FragmentShaderInvocations);
				DrawStat("Compute Shader Invocations", stats.PipelineStats.ComputeShaderInvocations);
				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Scene Workload");
			if (ImGui::BeginTable("##profiling_workload", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				DrawStat("Submitted Instances", stats.SubmittedInstances);
				DrawStat("Draw Calls", stats.DrawCalls);
				DrawStat("Meshes", stats.Meshes);
				DrawStat("Instances", stats.Instances);
				DrawStat("Visible Instances", stats.VisibleInstances);
				DrawStat("GPU Visible", stats.GPUVisibleInstances);
				DrawStat("Culled Instances", stats.CulledInstances);
				DrawStat("Frustum Culled", stats.FrustumCulledInstances);
				DrawStat("Shadow Culled", stats.ShadowCulledInstances);
				DrawStat("Occlusion Culled", stats.OcclusionCulledInstances);
				DrawStat("Fully Culled", stats.FullyCulledInstances);
				DrawStat("Saved Draws", stats.SavedDraws);
				DrawStat("Indirect Draws", stats.IndirectDraws);
				DrawStat("Spot Shadowcasters", stats.SpotlightShadowcasters);
				ImGui::EndTable();
			}

			if (Ref<Renderer2D> renderer2D = m_Context->GetRenderer2D())
			{
				Renderer2D::DrawStatistics renderer2DDrawStats = renderer2D->GetDrawStats();
				const Renderer2D::MemoryStatistics renderer2DMemoryStats = renderer2D->GetMemoryStats();

				ImGui::Spacing();
				ImGui::TextUnformatted("Renderer2D");
				if (ImGui::BeginTable("##profiling_renderer2d", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					DrawStat("2D Draw Calls", renderer2DDrawStats.DrawCalls);
					DrawStat("Quads", renderer2DDrawStats.QuadCount);
					DrawStat("Lines", renderer2DDrawStats.LineCount);
					DrawStat("2D Vertices", renderer2DDrawStats.GetTotalVertexCount());
					DrawStat("2D Indices", renderer2DDrawStats.GetTotalIndexCount());
					DrawStat("Memory Used", renderer2DMemoryStats.Used);
					DrawStat("Memory Allocated/Frame", renderer2DMemoryStats.GetAllocatedPerFrame());
					ImGui::EndTable();
				}
			}

			ImGui::TreePop();
		}

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
			projectSettingsChanged |= ImGuiEx::Property("Frustum Culling", options.EnableFrustumCulling);
			options.EnableOcclusionCulling = false;
			bool occlusionCulling = false;
			ImGui::BeginDisabled();
			ImGuiEx::Property("Occlusion Culling", occlusionCulling);
			ImGui::EndDisabled();
			ImGuiEx::SetTooltip("Disabled until Lux has a conservative occlusion depth pyramid.");
			projectSettingsChanged |= ImGuiEx::Property("GPU Driven Indirect", options.EnableGPUDrivenRendering);
			bool gtaoSettingsChanged = false;
			gtaoSettingsChanged |= ImGuiEx::Property("GTAO", options.EnableGTAO);
			gtaoSettingsChanged |= ImGuiEx::Property("GTAO Bent Normals", options.GTAOBentNormals);
			projectSettingsChanged |= gtaoSettingsChanged;
			projectSettingsChanged |= ImGuiEx::Property("GTAO Denoise Passes", options.GTAODenoisePasses, 0, 8);
			projectSettingsChanged |= ImGuiEx::Property("AO Shadow Tolerance", options.AOShadowTolerance, 0.01f, 0.0f, 4.0f);
			gtaoSettingsChanged |= ImGuiEx::Property("SSR", options.EnableSSR);
			projectSettingsChanged |= gtaoSettingsChanged;
			if (gtaoSettingsChanged)
				m_Context->UpdateGTAOData();
			projectSettingsChanged |= ImGuiEx::Property("Jump Flood Outline", options.EnableJumpFlood);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Shadows", true))
		{
			ImGuiEx::BeginPropertyGrid();
			projectSettingsChanged |= ImGuiEx::Property("Soft Shadows", options.SoftShadows);
			projectSettingsChanged |= ImGuiEx::Property("Shadow Culling", options.EnableShadowCulling);
			projectSettingsChanged |= ImGuiEx::Property("Max Distance", options.MaxShadowDistance, 1.0f, 1.0f, 1000.0f);
			projectSettingsChanged |= ImGuiEx::Property("Distance Fade", options.ShadowFade, 0.25f, 0.01f, 250.0f);
			projectSettingsChanged |= ImGuiEx::Property("Split Lambda", options.ShadowCascadeSplitLambda, 0.01f, 0.0f, 1.0f);
			projectSettingsChanged |= ImGuiEx::Property("Near Offset", options.ShadowCascadeNearPlaneOffset, 0.1f, 0.0f, 200.0f);
			projectSettingsChanged |= ImGuiEx::Property("Far Offset", options.ShadowCascadeFarPlaneOffset, 0.5f, 0.0f, 500.0f);
			projectSettingsChanged |= ImGuiEx::Property("Cascade Fade", options.ShadowCascadeTransitionFade, 0.05f, 0.0f, 25.0f);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (ImGuiEx::PropertyGridHeader("Post FX", false))
		{
			ImGuiEx::BeginPropertyGrid();
			projectSettingsChanged |= ImGuiEx::Property("Bloom", bloom.Enabled);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Threshold", bloom.Threshold, 0.01f, 0.0f, 25.0f);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Knee", bloom.Knee, 0.01f, 0.0f, 1.0f);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Upsample Scale", bloom.UpsampleScale, 0.01f, 0.0f, 10.0f);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Intensity", bloom.Intensity, 0.01f, 0.0f, 10.0f);
			projectSettingsChanged |= ImGuiEx::Property("Bloom Dirt Intensity", bloom.DirtIntensity, 0.01f, 0.0f, 10.0f);
			projectSettingsChanged |= ImGuiEx::Property("DOF", dof.Enabled);
			projectSettingsChanged |= ImGuiEx::Property("DOF Focus Distance", dof.FocusDistance, 0.1f, 0.0f, 1000.0f);
			projectSettingsChanged |= ImGuiEx::Property("DOF Blur Size", dof.BlurSize, 0.05f, 0.0f, 20.0f);
			projectSettingsChanged |= ImGuiEx::Property("SSR Half Res", ssr.HalfRes);
			int32_t ssrMaxSteps = ssr.MaxSteps;
			if (ImGuiEx::Property("SSR Max Steps", ssrMaxSteps, 1, 256))
			{
				ssr.MaxSteps = ssrMaxSteps;
				projectSettingsChanged = true;
			}
			projectSettingsChanged |= ImGuiEx::Property("SSR Brightness", ssr.Brightness, 0.01f, 0.0f, 5.0f);
			projectSettingsChanged |= ImGuiEx::Property("SSR Depth Tolerance", ssr.DepthTolerance, 0.01f, 0.0f, 5.0f);
			ImGuiEx::EndPropertyGrid();
			ImGui::TreePop();
		}

		if (projectSettingsChanged)
			SyncProjectSettingsFromContext();

		if (ImGuiEx::PropertyGridHeader("Render Statistics", true))
		{
			if (ImGui::BeginTable("##renderer_stats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				DrawStat("Draw Calls", stats.DrawCalls);
				DrawStat("Meshes", stats.Meshes);
				DrawStat("Instances", stats.Instances);
				DrawStat("Visible Instances", stats.VisibleInstances);
				DrawStat("GPU Visible", stats.GPUVisibleInstances);
				DrawStat("Culled Instances", stats.CulledInstances);
				DrawStat("Frustum Culled", stats.FrustumCulledInstances);
				DrawStat("Shadow Culled", stats.ShadowCulledInstances);
				DrawStat("Occlusion Culled", stats.OcclusionCulledInstances);
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
