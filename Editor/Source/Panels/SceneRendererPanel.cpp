#include "lpch.h"
#include "SceneRendererPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <format>
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

		void PushHistorySample(std::vector<float>& history, float value)
		{
			constexpr size_t maxHistorySamples = 240;
			history.push_back(value);
			if (history.size() > maxHistorySamples)
				history.erase(history.begin(), history.begin() + (history.size() - maxHistorySamples));
		}

		float GetHistoryMax(const std::vector<float>& a, const std::vector<float>& b, float fallback)
		{
			float maxValue = fallback;
			for (float value : a)
				maxValue = std::max(maxValue, value);
			for (float value : b)
				maxValue = std::max(maxValue, value);
			return maxValue;
		}

		bool DrawComboProperty(const char* label, int& value, const char* const* options, int optionCount)
		{
			bool modified = false;
			ImGuiEx::ShiftCursor(10.0f, 9.0f);
			ImGui::TextUnformatted(label);
			ImGui::NextColumn();
			ImGuiEx::ShiftCursorY(4.0f);
			ImGui::PushItemWidth(-1);

			const int clampedValue = std::clamp(value, 0, optionCount - 1);
			if (ImGui::BeginCombo(std::format("##{0}", label).c_str(), options[clampedValue]))
			{
				for (int i = 0; i < optionCount; i++)
				{
					const bool selected = value == i;
					if (ImGui::Selectable(options[i], selected))
					{
						value = i;
						modified = true;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::PopItemWidth();
			ImGui::NextColumn();
			return modified;
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

	void SceneRendererPanel::UpdateProfilingHistory(const SceneRenderer::Statistics& stats)
	{
		PushHistorySample(m_FrameCPUHistory, stats.TotalCPUTime);
		PushHistorySample(m_FrameGPUHistory, stats.TotalGPUTime);

		for (const auto& passProfile : stats.PassProfiles)
		{
			PassHistory& history = m_PassHistory[passProfile.Name];
			PushHistorySample(history.CPU, passProfile.CPUTime);
			PushHistorySample(history.GPU, passProfile.GPUTime);
		}
	}

	void SceneRendererPanel::DrawProfilingHistory(const SceneRenderer::Statistics& stats)
	{
		if (!m_FrameCPUHistory.empty())
		{
			ImGui::Spacing();
			ImGui::TextUnformatted("Frame History");
			const float frameMax = GetHistoryMax(m_FrameCPUHistory, m_FrameGPUHistory, 16.67f);
			ImGui::PlotLines("CPU##frame_history", m_FrameCPUHistory.data(), (int)m_FrameCPUHistory.size(), 0, nullptr, 0.0f, frameMax, ImVec2(-FLT_MIN, 48.0f));
			ImGui::PlotLines("GPU##frame_history", m_FrameGPUHistory.data(), (int)m_FrameGPUHistory.size(), 0, nullptr, 0.0f, frameMax, ImVec2(-FLT_MIN, 48.0f));
		}

		if (stats.PassProfiles.empty())
			return;

		ImGui::Spacing();
		ImGui::TextUnformatted("Pass History");
		if (ImGui::BeginTable("##profiling_pass_history", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Pass");
			ImGui::TableSetupColumn("CPU");
			ImGui::TableSetupColumn("GPU");
			ImGui::TableHeadersRow();

			for (const auto& passProfile : stats.PassProfiles)
			{
				auto historyIt = m_PassHistory.find(passProfile.Name);
				if (historyIt == m_PassHistory.end())
					continue;

				const PassHistory& history = historyIt->second;
				const float passMax = GetHistoryMax(history.CPU, history.GPU, 1.0f);

				ImGui::PushID(passProfile.Name);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(passProfile.Name);
				ImGui::TableSetColumnIndex(1);
				if (!history.CPU.empty())
					ImGui::PlotLines("##cpu_pass_history", history.CPU.data(), (int)history.CPU.size(), 0, nullptr, 0.0f, passMax, ImVec2(-FLT_MIN, 42.0f));
				ImGui::TableSetColumnIndex(2);
				if (!history.GPU.empty())
					ImGui::PlotLines("##gpu_pass_history", history.GPU.data(), (int)history.GPU.size(), 0, nullptr, 0.0f, passMax, ImVec2(-FLT_MIN, 42.0f));
				ImGui::PopID();
			}

			ImGui::EndTable();
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
		const auto& appTimers = Application::Get().GetPerformanceTimers();
		bool projectSettingsChanged = false;
		UpdateProfilingHistory(stats);

		if (ImGui::BeginTable("##scene_renderer_summary", 2, ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Ready", m_Context->IsReady() ? "Yes" : "No");
			DrawStat("Technique", RenderingTechniqueToString(m_Context->GetRenderingTechnique()));
			DrawStat("Viewport", (std::to_string(m_Context->GetViewportWidth()) + " x " + std::to_string(m_Context->GetViewportHeight())).c_str());
			DrawStat("Output Viewport", (std::to_string(m_Context->GetOutputViewportWidth()) + " x " + std::to_string(m_Context->GetOutputViewportHeight())).c_str());
			DrawStat("Render Scale", m_Context->GetRenderResolutionScale() * 100.0f, "%");
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

		ImGui::SameLine();
		if (ImGui::Button("Warm Up Pipelines"))
			m_LastWarmedPipelineCount = Renderer::WarmUpShaderPipelines();
		if (m_LastWarmedPipelineCount > 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Warmed %u pipeline%s", m_LastWarmedPipelineCount, m_LastWarmedPipelineCount == 1 ? "" : "s");
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

			DrawProfilingHistory(stats);

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
			ImGui::TextUnformatted("GPU Memory Budget");
			if (ImGui::BeginTable("##profiling_gpu_memory", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				const auto& memory = stats.MemoryStats;
				const float usedPercent = memory.BudgetBytes > 0
					? ((float)memory.UsedBytes / (float)memory.BudgetBytes) * 100.0f
					: 0.0f;
				DrawStat("Used / Budget", std::format("{} / {} ({:.1f}%)", Utils::BytesToString(memory.UsedBytes), Utils::BytesToString(memory.BudgetBytes), usedPercent).c_str());
				DrawStat("Textures", std::format("{} ({})", Utils::BytesToString(memory.TextureBytes), memory.TextureCount).c_str());
				DrawStat("Buffers", std::format("{} ({})", Utils::BytesToString(memory.BufferBytes), memory.BufferCount).c_str());
				DrawStat("Render Targets", std::format("{} ({})", Utils::BytesToString(memory.RenderTargetBytes), memory.RenderTargetCount).c_str());
				DrawStat("Framebuffers", memory.FramebufferCount);
				DrawStat("Descriptor Sets", memory.DescriptorSetCount);
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

				if (ImGui::Button("Warm Up Pipelines##section"))
					m_LastWarmedPipelineCount = Renderer::WarmUpShaderPipelines();
				ImGui::SameLine();
				ImGui::TextDisabled("Permutation cache: %u, warmed last run: %u", Renderer::GetShaderPermutationCacheSize(), m_LastWarmedPipelineCount);

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
			const char* renderScaleLabels[] = { "100%", "75%", "50%", "Dynamic" };
			int renderScaleMode = static_cast<int>(options.ResolutionScaleMode);
			if (DrawComboProperty("Render Scale", renderScaleMode, renderScaleLabels, IM_ARRAYSIZE(renderScaleLabels)))
			{
				options.ResolutionScaleMode = static_cast<SceneRendererOptions::RenderResolutionScaleMode>(renderScaleMode);
				m_Context->RefreshRenderResolutionScale();
				projectSettingsChanged = true;
			}
			if (options.ResolutionScaleMode == SceneRendererOptions::RenderResolutionScaleMode::Dynamic)
			{
				bool dynamicScaleChanged = false;
				dynamicScaleChanged |= ImGuiEx::Property("Dynamic Min Scale", options.DynamicResolutionMinScale, 0.01f, 0.25f, 1.0f);
				dynamicScaleChanged |= ImGuiEx::Property("Dynamic Max Scale", options.DynamicResolutionMaxScale, 0.01f, 0.25f, 1.0f);
				dynamicScaleChanged |= ImGuiEx::Property("Target GPU ms", options.DynamicResolutionTargetGPUTime, 0.1f, 1.0f, 100.0f);
				if (dynamicScaleChanged)
				{
					options.DynamicResolutionMinScale = std::clamp(options.DynamicResolutionMinScale, 0.25f, 1.0f);
					options.DynamicResolutionMaxScale = std::clamp(options.DynamicResolutionMaxScale, options.DynamicResolutionMinScale, 1.0f);
					options.DynamicResolutionScale = std::clamp(options.DynamicResolutionScale, options.DynamicResolutionMinScale, options.DynamicResolutionMaxScale);
					m_Context->RefreshRenderResolutionScale();
					projectSettingsChanged = true;
				}
			}
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
