#include "lpch.h"
#include "RendererDebuggerPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
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

		const char* DebugViewModeToString(SceneRenderer::DebugViewMode mode)
		{
			switch (mode)
			{
				case SceneRenderer::DebugViewMode::Final: return "Final";
				case SceneRenderer::DebugViewMode::Geometry: return "Geometry";
				case SceneRenderer::DebugViewMode::Depth: return "Depth";
				case SceneRenderer::DebugViewMode::Normals: return "Normals";
				case SceneRenderer::DebugViewMode::SSR: return "SSR";
				case SceneRenderer::DebugViewMode::AO: return "AO";
				case SceneRenderer::DebugViewMode::Bloom: return "Bloom";
				case SceneRenderer::DebugViewMode::Composite: return "Composite";
			}

			return "Unknown";
		}

		const char* ImageUsageToString(ImageUsage usage)
		{
			switch (usage)
			{
				case ImageUsage::None: return "None";
				case ImageUsage::Texture: return "Texture";
				case ImageUsage::Attachment: return "Attachment";
				case ImageUsage::Storage: return "Storage";
				case ImageUsage::HostRead: return "HostRead";
			}
			return "Unknown";
		}

		const char* TextureDimensionToString(nvrhi::TextureDimension dimension)
		{
			switch (dimension)
			{
				case nvrhi::TextureDimension::Unknown: return "Unknown";
				case nvrhi::TextureDimension::Texture1D: return "1D";
				case nvrhi::TextureDimension::Texture1DArray: return "1DArray";
				case nvrhi::TextureDimension::Texture2D: return "2D";
				case nvrhi::TextureDimension::Texture2DArray: return "2DArray";
				case nvrhi::TextureDimension::TextureCube: return "Cube";
				case nvrhi::TextureDimension::TextureCubeArray: return "CubeArray";
				case nvrhi::TextureDimension::Texture2DMS: return "2DMS";
				case nvrhi::TextureDimension::Texture2DMSArray: return "2DMSArray";
				case nvrhi::TextureDimension::Texture3D: return "3D";
			}
			return "Unknown";
		}

		bool HasState(nvrhi::ResourceStates state, nvrhi::ResourceStates flag)
		{
			return (static_cast<uint32_t>(state) & static_cast<uint32_t>(flag)) != 0;
		}

		std::string ResourceStateToString(nvrhi::ResourceStates state)
		{
			if (state == nvrhi::ResourceStates::Unknown)
				return "Unknown";

			std::string result;
			auto append = [&](nvrhi::ResourceStates flag, const char* name)
				{
					if (!HasState(state, flag))
						return;
					if (!result.empty())
						result += " | ";
					result += name;
				};

			append(nvrhi::ResourceStates::Common, "Common");
			append(nvrhi::ResourceStates::ConstantBuffer, "ConstantBuffer");
			append(nvrhi::ResourceStates::VertexBuffer, "VertexBuffer");
			append(nvrhi::ResourceStates::IndexBuffer, "IndexBuffer");
			append(nvrhi::ResourceStates::IndirectArgument, "IndirectArgument");
			append(nvrhi::ResourceStates::ShaderResource, "ShaderResource");
			append(nvrhi::ResourceStates::UnorderedAccess, "UnorderedAccess");
			append(nvrhi::ResourceStates::RenderTarget, "RenderTarget");
			append(nvrhi::ResourceStates::DepthWrite, "DepthWrite");
			append(nvrhi::ResourceStates::DepthRead, "DepthRead");
			append(nvrhi::ResourceStates::CopyDest, "CopyDest");
			append(nvrhi::ResourceStates::CopySource, "CopySource");
			append(nvrhi::ResourceStates::Present, "Present");

			return result.empty() ? std::format("0x{:08X}", static_cast<uint32_t>(state)) : result;
		}

		std::string LifetimeToString(const SceneRenderer::RenderGraphTextureDebugInfo& texture)
		{
			if (texture.FirstPass == UINT32_MAX)
				return "Unused";
			return std::format("{}-{}", texture.FirstPass, texture.LastPass);
		}

		std::string AliasToString(uint32_t aliasGroup)
		{
			if (aliasGroup == UINT32_MAX)
				return "-";
			return std::format("#{}", aliasGroup);
		}

		bool MatchesRenderGraphSearch(const SceneRenderer::RenderGraphDebugSnapshot& snapshot, const SceneRenderer::RenderGraphPassDebugInfo& pass, const std::string& search)
		{
			if (search.empty() || ImGuiEx::IsMatchingSearch(pass.Name, search, false, false, true))
				return true;

			auto resourceMatches = [&](const SceneRenderer::RenderGraphResourceAccessDebugInfo& access)
				{
					if (access.Resource >= snapshot.Textures.size())
						return false;
					return ImGuiEx::IsMatchingSearch(snapshot.Textures[access.Resource].Name, search, false, false, true);
				};

			return std::any_of(pass.Inputs.begin(), pass.Inputs.end(), resourceMatches)
				|| std::any_of(pass.Outputs.begin(), pass.Outputs.end(), resourceMatches);
		}

		std::string ResourceSummary(const SceneRenderer::RenderGraphTextureDebugInfo& texture, const std::string& accessState)
		{
			return std::format("#{} {} [{}] {} {}x{} mips {} layers {} life {} alias {} state {}",
				texture.Resource,
				texture.Name,
				accessState,
				Utils::ImageFormatToString(texture.Format),
				texture.Width,
				texture.Height,
				texture.Mips,
				texture.Layers,
				LifetimeToString(texture),
				AliasToString(texture.AliasGroup),
				ResourceStateToString(texture.CurrentState));
		}

		void DrawResourceList(const SceneRenderer::RenderGraphDebugSnapshot& snapshot, const std::vector<SceneRenderer::RenderGraphResourceAccessDebugInfo>& resources)
		{
			if (resources.empty())
			{
				ImGui::TextDisabled("-");
				return;
			}

			for (const auto& resource : resources)
			{
				if (resource.Resource >= snapshot.Textures.size())
					continue;
				const auto& texture = snapshot.Textures[resource.Resource];
				ImGui::TextWrapped("%s", ResourceSummary(texture, resource.State).c_str());
			}
		}

	}

	void RendererDebuggerPanel::SetContext(const Ref<SceneRenderer>& context)
	{
		m_Context = context;
	}

	void RendererDebuggerPanel::UpdateProfilingHistory(const SceneRenderer::Statistics& stats)
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

	void RendererDebuggerPanel::DrawProfilingHistory(const SceneRenderer::Statistics& stats)
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
		if (ImGui::BeginTable("##renderer_debugger_pass_history", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
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

	void RendererDebuggerPanel::DrawOverview(const SceneRenderer::Statistics& stats)
	{
		if (!ImGuiEx::PropertyGridHeader("Overview", true))
			return;

		if (ImGui::BeginTable("##renderer_debugger_overview", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Ready", m_Context->IsReady() ? "Yes" : "No");
			DrawStat("Technique", RenderingTechniqueToString(m_Context->GetRenderingTechnique()));
			DrawStat("Viewport", std::format("{} x {}", m_Context->GetViewportWidth(), m_Context->GetViewportHeight()).c_str());
			DrawStat("Output Viewport", std::format("{} x {}", m_Context->GetOutputViewportWidth(), m_Context->GetOutputViewportHeight()).c_str());
			DrawStat("Render Scale", m_Context->GetRenderResolutionScale() * 100.0f, "%");
			DrawStat("CPU Time", stats.TotalCPUTime, " ms");
			DrawStat("GPU Time", stats.TotalGPUTime, " ms");
			ImGui::EndTable();
		}

		ImGui::TreePop();
	}

	void RendererDebuggerPanel::DrawProfiling(const SceneRenderer::Statistics& stats)
	{
		if (!ImGuiEx::PropertyGridHeader("Profiling", true))
			return;

		uint32_t activePassCount = 0;
		float profiledGPUTime = 0.0f;
		for (const auto& passProfile : stats.PassProfiles)
		{
			if (!passProfile.Active && passProfile.GPUTime <= 0.0f)
				continue;

			activePassCount++;
			profiledGPUTime += passProfile.GPUTime;
		}

		const auto& appTimers = Application::Get().GetPerformanceTimers();
		if (ImGui::BeginTable("##renderer_debugger_profiling_frame", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
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
		if (ImGui::BeginTable("##renderer_debugger_passes", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
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
		if (ImGui::BeginTable("##renderer_debugger_pipeline", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
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

		ImGui::TreePop();
	}

	void RendererDebuggerPanel::DrawMemory(const SceneRenderer::Statistics& stats)
	{
		if (!ImGuiEx::PropertyGridHeader("GPU Memory", true))
			return;

		const auto& memory = stats.MemoryStats;
		const float usedPercent = memory.BudgetBytes > 0
			? ((float)memory.UsedBytes / (float)memory.BudgetBytes) * 100.0f
			: 0.0f;

		if (ImGui::BeginTable("##renderer_debugger_gpu_memory", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Used / Budget", std::format("{} / {} ({:.1f}%)", Utils::BytesToString(memory.UsedBytes), Utils::BytesToString(memory.BudgetBytes), usedPercent).c_str());
			DrawStat("Textures", std::format("{} ({})", Utils::BytesToString(memory.TextureBytes), memory.TextureCount).c_str());
			DrawStat("Buffers", std::format("{} ({})", Utils::BytesToString(memory.BufferBytes), memory.BufferCount).c_str());
			DrawStat("Render Targets", std::format("{} ({})", Utils::BytesToString(memory.RenderTargetBytes), memory.RenderTargetCount).c_str());
			DrawStat("Framebuffers", memory.FramebufferCount);
			DrawStat("Descriptor Sets", memory.DescriptorSetCount);
			DrawStat("Render Graph Transients", std::format("{} ({})", Utils::BytesToString(memory.RenderGraphTransientBytes), memory.RenderGraphTransientCount).c_str());
			DrawStat("Alias Plan", std::format("{} groups, saves {}", memory.RenderGraphAliasGroupCount, Utils::BytesToString(memory.RenderGraphSavedBytes)).c_str());
			DrawStat("Alias Budget", std::format("{} / {}", Utils::BytesToString(memory.RenderGraphAliasedBytes), Utils::BytesToString(memory.RenderGraphTransientBytes)).c_str());
			ImGui::EndTable();
		}

		ImGui::TreePop();
	}

	void RendererDebuggerPanel::DrawWorkload(const SceneRenderer::Statistics& stats)
	{
		if (!ImGuiEx::PropertyGridHeader("Scene Workload", false))
			return;

		if (ImGui::BeginTable("##renderer_debugger_workload", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
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
			if (ImGui::BeginTable("##renderer_debugger_renderer2d", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
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

	void RendererDebuggerPanel::DrawShaders()
	{
		if (!ImGuiEx::PropertyGridHeader("Shaders", false))
			return;

		Ref<ShaderLibrary> shaderLibrary = Renderer::GetShaderLibrary();
		if (!shaderLibrary)
		{
			ImGui::TextDisabled("Shader library is not available.");
			ImGui::TreePop();
			return;
		}

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

		if (ImGui::BeginTable("##renderer_debugger_shader_table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
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

	void RendererDebuggerPanel::DrawRenderPassIsolation()
	{
		if (!ImGuiEx::PropertyGridHeader("Render Pass Isolation", true))
			return;

		struct DebugViewButton
		{
			SceneRenderer::DebugViewMode Mode;
			const char* Label;
		};

		static constexpr DebugViewButton debugViews[] = {
			{ SceneRenderer::DebugViewMode::Geometry, "Geometry" },
			{ SceneRenderer::DebugViewMode::Depth, "Depth" },
			{ SceneRenderer::DebugViewMode::Normals, "Normals" },
			{ SceneRenderer::DebugViewMode::SSR, "SSR" },
			{ SceneRenderer::DebugViewMode::AO, "AO" },
			{ SceneRenderer::DebugViewMode::Bloom, "Bloom" },
			{ SceneRenderer::DebugViewMode::Composite, "Composite" },
			{ SceneRenderer::DebugViewMode::Final, "Final" },
		};

		if (m_DebugViewsRuntimeSuspended)
			ImGui::TextDisabled("Suspended while Play is running; restored on Stop.");

		const SceneRenderer::DebugViewMode activeMode = m_Context->GetDebugViewMode();
		ImGui::BeginDisabled(m_DebugViewsRuntimeSuspended);
		if (ImGui::BeginTable("##renderer_debugger_pass_isolation", 4, ImGuiTableFlags_SizingStretchSame))
		{
			for (const DebugViewButton& debugView : debugViews)
			{
				ImGui::TableNextColumn();

				const bool selected = activeMode == debugView.Mode;
				const bool unavailable = debugView.Mode != SceneRenderer::DebugViewMode::Final && !m_Context->GetDebugViewImage(debugView.Mode);

				if (selected)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				}

				ImGui::BeginDisabled(unavailable);
				if (ImGui::Button(debugView.Label, ImVec2(-FLT_MIN, 0.0f)))
					m_Context->SetDebugViewMode(debugView.Mode);
				ImGui::EndDisabled();

				if (selected)
					ImGui::PopStyleColor(2);

				if (unavailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Pass output is unavailable with the current renderer settings.");
			}
			ImGui::EndTable();
		}
		ImGui::EndDisabled();

		const Ref<Image2D> activeImage = m_Context->GetDebugViewImage(activeMode);
		ImGui::Spacing();
		ImGui::Text("Active: %s", DebugViewModeToString(activeMode));
		if (activeImage)
		{
			const ImageSpecification& spec = activeImage->GetSpecification();
			ImGui::TextDisabled("%s, %ux%u, %u mip%s",
				std::string(Utils::ImageFormatToString(spec.Format)).c_str(),
				spec.Width,
				spec.Height,
				spec.Mips,
				spec.Mips == 1 ? "" : "s");
		}
		else if (activeMode != SceneRenderer::DebugViewMode::Final)
		{
			ImGui::TextDisabled("Selected pass is unavailable; viewport falls back to Final.");
		}

		ImGui::TreePop();
	}

	void RendererDebuggerPanel::DrawRenderGraphInspector()
	{
		if (!ImGuiEx::PropertyGridHeader("Render Graph Inspector", true))
			return;

		SceneRenderer::RenderGraphDebugSnapshot snapshot = m_Context->GetRenderGraphDebugSnapshot();
		ImGui::TextDisabled("%zu passes, %zu textures", snapshot.Passes.size(), snapshot.Textures.size());
		ImGuiEx::Widgets::SearchWidget(m_RenderGraphSearch, "Search passes or textures...");

		if (ImGui::BeginTable("##render_graph_pass_table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 180.0f);
			ImGui::TableSetupColumn("Inputs");
			ImGui::TableSetupColumn("Outputs");
			ImGui::TableHeadersRow();

			for (uint32_t passIndex = 0; passIndex < snapshot.Passes.size(); passIndex++)
			{
				const auto& pass = snapshot.Passes[passIndex];
				if (!MatchesRenderGraphSearch(snapshot, pass, m_RenderGraphSearch))
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", passIndex);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(pass.Name.c_str());
				ImGui::TableSetColumnIndex(2);
				DrawResourceList(snapshot, pass.Inputs);
				ImGui::TableSetColumnIndex(3);
				DrawResourceList(snapshot, pass.Outputs);
			}

			ImGui::EndTable();
		}

		ImGui::Spacing();
		if (ImGui::BeginTable("##render_graph_texture_table", 12, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX))
		{
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthFixed, 210.0f);
			ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Dim", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Mip", ImGuiTableColumnFlags_WidthFixed, 62.0f);
			ImGui::TableSetupColumn("Layers", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Lifetime", ImGuiTableColumnFlags_WidthFixed, 82.0f);
			ImGui::TableSetupColumn("Alias", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableSetupColumn("Current State", ImGuiTableColumnFlags_WidthFixed, 190.0f);
			ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableHeadersRow();

			for (const auto& texture : snapshot.Textures)
			{
				if (!m_RenderGraphSearch.empty() && !ImGuiEx::IsMatchingSearch(texture.Name, m_RenderGraphSearch, false, false, true))
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", texture.Resource);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(texture.Name.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(std::string(Utils::ImageFormatToString(texture.Format)).c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(ImageUsageToString(texture.Usage));
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(TextureDimensionToString(texture.Dimension));
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%u x %u", texture.Width, texture.Height);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("0-%u", texture.Mips > 0 ? texture.Mips - 1 : 0);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%u", texture.Layers);
				ImGui::TableSetColumnIndex(8);
				ImGui::TextUnformatted(LifetimeToString(texture).c_str());
				ImGui::TableSetColumnIndex(9);
				ImGui::Text("%s%s", AliasToString(texture.AliasGroup).c_str(), texture.AliasedNow ? " live" : "");
				ImGui::TableSetColumnIndex(10);
				ImGui::TextUnformatted(ResourceStateToString(texture.CurrentState).c_str());
				ImGui::TableSetColumnIndex(11);
				ImGui::TextUnformatted(Utils::BytesToString(texture.EstimatedBytes).c_str());
			}

			ImGui::EndTable();
		}

		ImGui::TreePop();
	}

	void RendererDebuggerPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Renderer Debugger", &isOpen))
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

		const auto& stats = m_Context->GetStatistics();
		UpdateProfilingHistory(stats);

		DrawOverview(stats);
		DrawRenderPassIsolation();
		DrawProfiling(stats);
		DrawMemory(stats);
		DrawRenderGraphInspector();
		DrawWorkload(stats);
		DrawShaders();

		ImGui::End();
	}

}
