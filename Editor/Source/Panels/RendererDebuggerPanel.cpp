#include "lpch.h"
#include "RendererDebuggerPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <implot/implot.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace Lux {

	namespace {

		// 60/30 FPS frame budgets and the graph-semantic series colours (data colours, not theme UI
		// colours).
		constexpr float kBudget60FPS = 1000.0f / 60.0f; // 16.67 ms
		constexpr ImU32 kCPUColor = IM_COL32(120, 170, 255, 255);   // blue-ish CPU series
		constexpr ImU32 kGPUColor = IM_COL32(200, 255, 77, 255);    // lime GPU series (on-brand)
		constexpr ImU32 kBarColor = IM_COL32(200, 255, 77, 255);    // per-pass GPU bars

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
				case SceneRenderer::DebugViewMode::GBufferBaseColor: return "GBuffer Base Color";
				case SceneRenderer::DebugViewMode::GBufferNormal: return "GBuffer Normal";
				case SceneRenderer::DebugViewMode::GBufferMetalRough: return "GBuffer Metal/Rough";
				case SceneRenderer::DebugViewMode::GBufferMaterialID: return "GBuffer Material ID";
				case SceneRenderer::DebugViewMode::GBufferObjectID: return "GBuffer Object ID";
				case SceneRenderer::DebugViewMode::DeferredLighting: return "Deferred Lighting";
				case SceneRenderer::DebugViewMode::GPUScenePrimitiveID: return "GPUScene Primitive ID";
				case SceneRenderer::DebugViewMode::GPUSceneMaterialIndex: return "GPUScene Material Index";
				case SceneRenderer::DebugViewMode::GPUSceneObjectID: return "GPUScene Object ID";
				case SceneRenderer::DebugViewMode::GPUSceneBounds: return "GPUScene Bounds";
				case SceneRenderer::DebugViewMode::GPUSceneMotion: return "GPUScene Motion";
				case SceneRenderer::DebugViewMode::GPUMaterialTextureValidity: return "GPU Material Texture Validity";
				case SceneRenderer::DebugViewMode::GPUMaterialAlphaMode: return "GPU Material Alpha Mode";
				case SceneRenderer::DebugViewMode::GPUMaterialRoughness: return "GPU Material Roughness";
				case SceneRenderer::DebugViewMode::GPUMaterialMetalness: return "GPU Material Metalness";
				case SceneRenderer::DebugViewMode::GPUMaterialMissing: return "GPU Material Missing";
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

		std::string RenderGraphPassFlagsToString(uint32_t flags)
		{
			if (flags == 0)
				return "-";

			std::string result;
			auto append = [&](RenderGraph::PassFlags flag, const char* name)
				{
					if ((flags & static_cast<uint32_t>(flag)) == 0)
						return;
					if (!result.empty())
						result += ", ";
					result += name;
				};

			append(RenderGraph::PassFlags::Graphics, "Graphics");
			append(RenderGraph::PassFlags::Compute, "Compute");
			append(RenderGraph::PassFlags::Transfer, "Transfer");
			append(RenderGraph::PassFlags::SideEffect, "Pinned");
			append(RenderGraph::PassFlags::NeverCull, "NeverCull");
			return result.empty() ? std::format("0x{:08X}", flags) : result;
		}

		std::string RenderGraphPassStatusToString(const SceneRenderer::RenderGraphPassDebugInfo& pass)
		{
			if (pass.Culled)
				return "Culled";
			return pass.Executable ? "Executable" : "Metadata";
		}

		const char* RenderGraphDiagnosticSeverityToString(RenderGraph::DiagnosticSeverity severity)
		{
			switch (severity)
			{
				case RenderGraph::DiagnosticSeverity::Info: return "Info";
				case RenderGraph::DiagnosticSeverity::Warning: return "Warning";
				case RenderGraph::DiagnosticSeverity::Error: return "Error";
			}
			return "Unknown";
		}

		ImVec4 RenderGraphDiagnosticColor(RenderGraph::DiagnosticSeverity severity)
		{
			switch (severity)
			{
				case RenderGraph::DiagnosticSeverity::Error: return ImVec4(0.95f, 0.25f, 0.25f, 1.0f);
				case RenderGraph::DiagnosticSeverity::Warning: return ImVec4(0.95f, 0.72f, 0.20f, 1.0f);
				case RenderGraph::DiagnosticSeverity::Info: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
			}
			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		const char* RenderGraphDiagnosticCodeToString(RenderGraph::DiagnosticCode code)
		{
			switch (code)
			{
				case RenderGraph::DiagnosticCode::InvalidResource: return "InvalidResource";
				case RenderGraph::DiagnosticCode::NullTexture: return "NullTexture";
				case RenderGraph::DiagnosticCode::InvalidTextureDesc: return "InvalidTextureDesc";
				case RenderGraph::DiagnosticCode::ReadBeforeWrite: return "ReadBeforeWrite";
				case RenderGraph::DiagnosticCode::UnwrittenExternalRead: return "UnwrittenExternalRead";
				case RenderGraph::DiagnosticCode::DeadWrite: return "DeadWrite";
				case RenderGraph::DiagnosticCode::ReadWriteSameResource: return "ReadWriteSameResource";
				case RenderGraph::DiagnosticCode::DuplicatePassName: return "DuplicatePassName";
				case RenderGraph::DiagnosticCode::DuplicateTextureName: return "DuplicateTextureName";
				case RenderGraph::DiagnosticCode::InvalidPassFlags: return "InvalidPassFlags";
				case RenderGraph::DiagnosticCode::EmptyExecutablePass: return "EmptyExecutablePass";
				case RenderGraph::DiagnosticCode::EmptyMetadataPass: return "EmptyMetadataPass";
				case RenderGraph::DiagnosticCode::AliasLifetimeConflict: return "AliasLifetimeConflict";
				case RenderGraph::DiagnosticCode::AliasIncompatibleResource: return "AliasIncompatibleResource";
			}
			return "Unknown";
		}

		bool RenderGraphDiagnosticVisible(const SceneRenderer::RenderGraphDiagnosticDebugInfo& diagnostic, bool errorsOnly, bool showWarnings)
		{
			if (errorsOnly)
				return diagnostic.Severity == RenderGraph::DiagnosticSeverity::Error;
			if (!showWarnings && diagnostic.Severity == RenderGraph::DiagnosticSeverity::Warning)
				return false;
			return true;
		}

		bool RenderGraphPassHasSeverity(const SceneRenderer::RenderGraphDebugSnapshot& snapshot, const SceneRenderer::RenderGraphPassDebugInfo& pass, RenderGraph::DiagnosticSeverity severity)
		{
			return std::any_of(pass.Diagnostics.begin(), pass.Diagnostics.end(), [&](uint32_t diagnosticIndex)
				{
					return diagnosticIndex < snapshot.Diagnostics.size() && snapshot.Diagnostics[diagnosticIndex].Severity == severity;
				});
		}

		std::string RenderGraphPassIndexToString(const SceneRenderer::RenderGraphDebugSnapshot& snapshot, uint32_t passIndex)
		{
			if (passIndex == UINT32_MAX)
				return "-";
			if (passIndex >= snapshot.Passes.size())
				return std::format("Invalid({})", passIndex);
			return std::format("#{} {}", passIndex, snapshot.Passes[passIndex].Name);
		}

		std::string RenderGraphConsumerListToString(const SceneRenderer::RenderGraphDebugSnapshot& snapshot, const std::vector<uint32_t>& consumers)
		{
			if (consumers.empty())
				return "-";

			std::string result;
			for (uint32_t passIndex : consumers)
			{
				if (!result.empty())
					result += ", ";
				result += RenderGraphPassIndexToString(snapshot, passIndex);
			}
			return result;
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
				{
					ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "#%u [%s] Invalid", resource.Resource, resource.State.c_str());
					continue;
				}
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
	}

	void RendererDebuggerPanel::DrawFrameHistoryPlot()
	{
		const size_t sampleCount = std::max(m_FrameCPUHistory.size(), m_FrameGPUHistory.size());
		if (sampleCount == 0)
			return;

		float maxValue = kBudget60FPS;
		maxValue = GetHistoryMax(m_FrameCPUHistory, m_FrameGPUHistory, maxValue);

		// X positions run from -(N-1)..0 so the newest frame sits at x=0 (right edge), scrolling left.
		m_FrameHistoryAxis.resize(sampleCount);
		for (size_t i = 0; i < sampleCount; i++)
			m_FrameHistoryAxis[i] = (float)i - (float)(sampleCount - 1);

		ImGui::TextUnformatted("Frame History (CPU / GPU ms)");
		ImPlot::PushStyleColor(ImPlotCol_FrameBg, Colors::Theme::backgroundDark);
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, Colors::Theme::backgroundDark);
		ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0.0f, 0.0f));

		const ImPlotFlags plotFlags = ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText | ImPlotFlags_NoBoxSelect;
		ImGuiEx::Fonts::PushFont("Mono"); // mono axis/tick labels, Tracy-style
		if (ImPlot::BeginPlot("##frame_history", ImVec2(-1.0f, 130.0f), plotFlags))
		{
			ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_Lock);
			ImPlot::SetupAxis(ImAxis_Y1, "ms", ImPlotAxisFlags_LockMin);
			ImPlot::SetupAxisLimits(ImAxis_X1, -(double)m_FrameHistoryAxis.size(), 0.0, ImGuiCond_Always);
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, (double)maxValue * 1.15, ImGuiCond_Always);
			ImPlot::SetupLegend(ImPlotLocation_NorthWest);

			if (!m_FrameCPUHistory.empty())
			{
				ImPlotSpec cpuSpec;
				cpuSpec.LineColor = ImGui::ColorConvertU32ToFloat4(kCPUColor);
				cpuSpec.LineWeight = 1.5f;
				ImPlot::PlotLine("CPU", m_FrameHistoryAxis.data(), m_FrameCPUHistory.data(), (int)m_FrameCPUHistory.size(), cpuSpec);
			}
			if (!m_FrameGPUHistory.empty())
			{
				ImPlotSpec gpuSpec;
				gpuSpec.LineColor = ImGui::ColorConvertU32ToFloat4(kGPUColor);
				gpuSpec.LineWeight = 1.5f;
				gpuSpec.FillColor = gpuSpec.LineColor;
				gpuSpec.FillAlpha = 0.18f;
				gpuSpec.Flags = ImPlotLineFlags_Shaded;
				ImPlot::PlotLine("GPU", m_FrameHistoryAxis.data(), m_FrameGPUHistory.data(), (int)m_FrameGPUHistory.size(), gpuSpec);
			}

			double budget = kBudget60FPS;
			ImPlotSpec budgetSpec;
			budgetSpec.LineColor = ImGui::ColorConvertU32ToFloat4(Colors::Theme::muted);
			budgetSpec.LineWeight = 1.0f;
			budgetSpec.Flags = ImPlotInfLinesFlags_Horizontal | ImPlotItemFlags_NoLegend;
			ImPlot::PlotInfLines("##budget", &budget, 1, budgetSpec);

			ImPlot::EndPlot();
		}
		ImGuiEx::Fonts::PopFont();

		ImPlot::PopStyleVar();
		ImPlot::PopStyleColor(2);
	}

	void RendererDebuggerPanel::DrawPassGPUChart(const SceneRenderer::Statistics& stats)
	{
		// Gather the active passes, sorted by GPU time descending — the heaviest at the top.
		std::vector<size_t> order;
		order.reserve(stats.PassProfiles.size());
		for (size_t i = 0; i < stats.PassProfiles.size(); i++)
		{
			const auto& p = stats.PassProfiles[i];
			if (p.Active || p.GPUTime > 0.0f)
				order.push_back(i);
		}
		if (order.empty())
			return;

		std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
			{
				return stats.PassProfiles[a].GPUTime > stats.PassProfiles[b].GPUTime;
			});

		// Bars are drawn against a Y position; index 0 is the bottom, so put the heaviest pass at the
		// top by giving it the largest Y and labelling ticks accordingly.
		const int count = (int)order.size();
		m_PassChartValues.resize(count);
		m_PassChartTicks.resize(count);
		m_PassChartLabels.resize(count);
		float maxGPU = 0.0f;
		for (int i = 0; i < count; i++)
		{
			const auto& p = stats.PassProfiles[order[i]];
			const int slot = count - 1 - i; // heaviest (i=0) -> top slot
			m_PassChartValues[slot] = p.GPUTime;
			m_PassChartTicks[slot] = (double)slot;
			m_PassChartLabels[slot] = p.Name;
			maxGPU = std::max(maxGPU, p.GPUTime);
		}

		ImGui::TextUnformatted("GPU Time by Pass (ms)");
		ImPlot::PushStyleColor(ImPlotCol_FrameBg, Colors::Theme::backgroundDark);
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, Colors::Theme::backgroundDark);

		const float chartHeight = std::max(120.0f, count * 20.0f + 20.0f);
		const ImPlotFlags plotFlags = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoBoxSelect;
		ImGuiEx::Fonts::PushFont("Mono"); // mono axis/tick labels, Tracy-style
		if (ImPlot::BeginPlot("##pass_gpu_chart", ImVec2(-1.0f, chartHeight), plotFlags))
		{
			ImPlot::SetupAxis(ImAxis_X1, "ms", ImPlotAxisFlags_LockMin);
			ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_Lock);
			ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, (double)maxGPU * 1.15 + 0.001, ImGuiCond_Always);
			ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, (double)count, ImGuiCond_Always);
			ImPlot::SetupAxisTicks(ImAxis_Y1, m_PassChartTicks.data(), count, m_PassChartLabels.data());

			ImPlotSpec barSpec;
			barSpec.FillColor = ImGui::ColorConvertU32ToFloat4(kBarColor);
			barSpec.LineColor = barSpec.FillColor;
			barSpec.Flags = ImPlotBarsFlags_Horizontal;
			// Single-array overload: bars at implicit Y positions 0..count-1, length = value along X.
			ImPlot::PlotBars("GPU ms", m_PassChartValues.data(), count, 0.67, 0.0, barSpec);

			ImPlot::EndPlot();
		}
		ImGuiEx::Fonts::PopFont();

		ImPlot::PopStyleColor(2);
	}

	void RendererDebuggerPanel::DrawOverview(const SceneRenderer::Statistics& stats)
	{
		if (!ImGuiEx::PropertyGridHeader("Overview", true))
			return;

		if (ImGui::BeginTable("##renderer_debugger_overview", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Ready", m_Context->IsReady() ? "Yes" : "No");
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
		DrawPassGPUChart(stats);

		ImGui::Spacing();
		ImGui::TextUnformatted("Pass Timings");
		if (ImGui::BeginTable("##renderer_debugger_passes", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate))
		{
			ImGui::TableSetupColumn("Pass");
			ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 82.0f);
			ImGui::TableSetupColumn("CPU %", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 62.0f);
			ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 82.0f);
			ImGui::TableSetupColumn("GPU %", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 62.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableHeadersRow();

			// Default is registration order (SortTristate → no sort until a header is
			// clicked). Clicking a column sorts a display-index list, leaving the
			// underlying stats untouched, so the heaviest passes can be surfaced at a
			// glance — click "GPU ms" to find the frame's real bottlenecks.
			std::vector<size_t> order;
			order.reserve(stats.PassProfiles.size());
			for (size_t i = 0; i < stats.PassProfiles.size(); i++)
				order.push_back(i);

			if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
			{
				if (sortSpecs->SpecsCount > 0)
				{
					const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
					const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
					std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
						{
							const auto& pa = stats.PassProfiles[a];
							const auto& pb = stats.PassProfiles[b];
							switch (spec.ColumnIndex)
							{
								case 0: { const int c = std::strcmp(pa.Name, pb.Name); return ascending ? c < 0 : c > 0; }
								case 1:
								case 2: return ascending ? pa.CPUTime < pb.CPUTime : pa.CPUTime > pb.CPUTime;
								case 3:
								case 4: return ascending ? pa.GPUTime < pb.GPUTime : pa.GPUTime > pb.GPUTime;
								default: return ascending ? a < b : a > b;
							}
						});
				}
			}

			for (size_t idx : order)
			{
				const auto& passProfile = stats.PassProfiles[idx];
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

	void RendererDebuggerPanel::DrawGPUScene()
	{
		if (!ImGuiEx::PropertyGridHeader("GPU Scene", true))
			return;

		// The snapshot is only built on frames where someone asks for it; keep
		// requesting while this section is open so next frame's data is fresh.
		m_Context->RequestGPUSceneDebugSnapshot();
		const SceneRenderer::GPUSceneDebugSnapshot& snapshot = m_Context->GetGPUSceneDebugSnapshot();
		if (ImGui::BeginTable("##renderer_debugger_gpu_scene", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Persistent Instances", snapshot.PersistentInstanceCount);
			DrawStat("Transient Instances", snapshot.TransientInstanceCount);
			DrawStat("Uploaded Instances", snapshot.TotalUploadedInstanceCount);
			DrawStat("Active Primitives", snapshot.ActivePrimitiveCount);
			DrawStat("Visible Primitives", snapshot.VisiblePrimitiveCount);
			DrawStat("Object Indexes", snapshot.ObjectIndexCount);
			DrawStat("Visible Object Indexes", snapshot.VisibleObjectIndexCount);
			DrawStat("Mesh Cull Draws", snapshot.MeshCullDrawCount);
			DrawStat("Indirect Draws", snapshot.IndirectDrawCount);
			DrawStat("Dirty Instances", snapshot.DirtyInstanceCount);
			DrawStat("Dirty Ranges", snapshot.DirtyRangeCount);
			DrawStat("Persistent Materials", snapshot.PersistentMaterialCount);
			DrawStat("Transient Materials", snapshot.TransientMaterialCount);
			DrawStat("Uploaded Materials", snapshot.UploadedMaterialCount);
			DrawStat("Dirty Materials", snapshot.DirtyMaterialCount);
			DrawStat("Dirty Material Ranges", snapshot.DirtyMaterialRangeCount);
			DrawStat("Persistent Textures", snapshot.PersistentTextureCount);
			DrawStat("Transient Textures", snapshot.TransientTextureCount);
			DrawStat("Uploaded Textures", snapshot.UploadedTextureCount);
			DrawStat("Dirty Textures", snapshot.DirtyTextureCount);
			DrawStat("Dirty Texture Ranges", snapshot.DirtyTextureRangeCount);
			DrawStat("Max Render Material ID", snapshot.MaxMaterialIndex);
			ImGui::EndTable();
		}

		ImGui::Spacing();
		if (ImGui::BeginTable("##renderer_debugger_gpu_scene_validation", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Invalid Object Indexes", snapshot.InvalidObjectIndexCount);
			DrawStat("Invalid Visible Indexes", snapshot.InvalidVisibleObjectIndexCount);
			DrawStat("Invalid Material IDs", snapshot.InvalidMaterialIDCount);
			DrawStat("Invalid Bounds", snapshot.InvalidBoundsCount);
			DrawStat("Invalid Previous Transforms", snapshot.InvalidPreviousTransformCount);
			DrawStat("Bad Stored Instance IDs", snapshot.InvalidStoredInstanceIDCount);
			DrawStat("Missing Primitive IDs", snapshot.PersistentInvalidPrimitiveIDCount);
			DrawStat("Missing Object IDs", snapshot.MissingPersistentObjectIDCount);
			DrawStat("Missing Materials", snapshot.MissingMaterialCount);
			DrawStat("Missing Textures", snapshot.MissingTextureCount);
			DrawStat("Invalid Texture Indices", snapshot.InvalidTextureIndexCount);
			DrawStat("Missing Texture Descriptors", snapshot.MissingTextureDescriptorCount);
			DrawStat("Texture Table Overflow", snapshot.TextureTableOverflowCount);
			ImGui::EndTable();
		}

		if (snapshot.Diagnostics.empty())
		{
			ImGui::TextDisabled("No GPUScene validation diagnostics.");
		}
		else
		{
			for (const std::string& diagnostic : snapshot.Diagnostics)
				ImGui::TextWrapped("Diagnostic: %s", diagnostic.c_str());
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
			{ SceneRenderer::DebugViewMode::GBufferBaseColor, "GBuf Base" },
			{ SceneRenderer::DebugViewMode::GBufferNormal, "GBuf Norm" },
			{ SceneRenderer::DebugViewMode::GBufferMetalRough, "GBuf MR" },
			{ SceneRenderer::DebugViewMode::GBufferMaterialID, "GBuf MatID" },
			{ SceneRenderer::DebugViewMode::GBufferObjectID, "GBuf ObjID" },
			{ SceneRenderer::DebugViewMode::DeferredLighting, "Deferred" },
			{ SceneRenderer::DebugViewMode::GPUScenePrimitiveID, "GPU Prim" },
			{ SceneRenderer::DebugViewMode::GPUSceneMaterialIndex, "GPU Mat" },
			{ SceneRenderer::DebugViewMode::GPUSceneObjectID, "GPU Object" },
			{ SceneRenderer::DebugViewMode::GPUSceneBounds, "GPU Bounds" },
			{ SceneRenderer::DebugViewMode::GPUSceneMotion, "GPU Motion" },
			{ SceneRenderer::DebugViewMode::GPUMaterialTextureValidity, "Mat Tex" },
			{ SceneRenderer::DebugViewMode::GPUMaterialAlphaMode, "Mat Alpha" },
			{ SceneRenderer::DebugViewMode::GPUMaterialRoughness, "Mat Rough" },
			{ SceneRenderer::DebugViewMode::GPUMaterialMetalness, "Mat Metal" },
			{ SceneRenderer::DebugViewMode::GPUMaterialMissing, "Mat Missing" },
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
		const SceneRenderer::RendererFrameDebugSnapshot frame = m_Context->GetRendererFrameDebugSnapshot();
		ImGui::Spacing();
		ImGui::Text("Active: %s", DebugViewModeToString(activeMode));
		ImGui::TextDisabled("Viewport: %ux%u",
			m_Context->GetViewportWidth(),
			m_Context->GetViewportHeight());
		ImGui::TextDisabled("Frame Contract: %s path, RenderScene %s, bloom %s, DOF %s",
			frame.DeferredPath ? "Deferred" : "Forward",
			frame.HasRenderScene ? "yes" : "no",
			frame.BloomEnabled ? "on" : "off",
			frame.DOFEnabled ? "on" : "off");
		const bool materialIDValid = m_Context->GetDebugViewImage(SceneRenderer::DebugViewMode::GBufferMaterialID) != nullptr;
		const bool objectIDValid = m_Context->GetDebugViewImage(SceneRenderer::DebugViewMode::GBufferObjectID) != nullptr;
		ImGui::TextDisabled("GBuffer: A RGBA16F, B RGBA16F, C RGBA8, MaterialID RED32UI %s, ObjectID RED32UI %s",
			materialIDValid ? "valid" : "unavailable",
			objectIDValid ? "valid" : "unavailable");
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
		if (m_RenderGraphSelectedPass >= snapshot.Passes.size())
			m_RenderGraphSelectedPass = UINT32_MAX;
		if (m_RenderGraphSelectedResource >= snapshot.Textures.size())
			m_RenderGraphSelectedResource = UINT32_MAX;

		ImGui::TextDisabled("%u error(s), %u warning(s), %zu passes (%u executed, %u culled), %zu textures, %zu alias group(s), transient %s, saved %s",
			snapshot.ErrorCount,
			snapshot.WarningCount,
			snapshot.Passes.size(),
			snapshot.ExecutedPassCount,
			snapshot.CulledPassCount,
			snapshot.Textures.size(),
			snapshot.AliasGroups.size(),
			Utils::BytesToString(snapshot.TransientBytes).c_str(),
			Utils::BytesToString(snapshot.SavedBytes).c_str());

		if (ImGui::Button("Run RenderGraph Self Tests"))
		{
			m_RenderGraphSelfTestFailures.clear();
			m_RenderGraphSelfTestPassed = RenderGraph::RunValidationSelfTests(&m_RenderGraphSelfTestFailures);
			m_RenderGraphSelfTestRan = true;
		}
		if (m_RenderGraphSelfTestRan)
		{
			ImGui::SameLine();
			if (m_RenderGraphSelfTestPassed)
			{
				ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "Self tests passed");
			}
			else
			{
				ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "Self tests failed (%zu)", m_RenderGraphSelfTestFailures.size());
				for (const std::string& failure : m_RenderGraphSelfTestFailures)
					ImGui::TextWrapped("%s", failure.c_str());
			}
		}

		if (ImGui::Button("Run Scene Serializer Self Tests"))
		{
			m_SerializerSelfTestFailures.clear();
			m_SerializerSelfTestPassed = SceneSerializer::RunRoundTripSelfTests(&m_SerializerSelfTestFailures);
			m_SerializerSelfTestRan = true;
		}
		if (m_SerializerSelfTestRan)
		{
			ImGui::SameLine();
			if (m_SerializerSelfTestPassed)
			{
				ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "Self tests passed");
			}
			else
			{
				ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "Self tests failed (%zu)", m_SerializerSelfTestFailures.size());
				for (const std::string& failure : m_SerializerSelfTestFailures)
					ImGui::TextWrapped("%s", failure.c_str());
			}
		}

		ImGuiEx::Widgets::SearchWidget(m_RenderGraphSearch, "Search passes or textures...");

		ImGui::Checkbox("Errors Only", &m_RenderGraphErrorsOnly);
		ImGui::SameLine();
		ImGui::Checkbox("Warnings", &m_RenderGraphShowWarnings);
		ImGui::SameLine();
		ImGui::Checkbox("Culled", &m_RenderGraphShowCulled);
		ImGui::SameLine();
		ImGui::Checkbox("Aliased", &m_RenderGraphShowAliased);
		ImGui::SameLine();
		ImGui::Checkbox("Transient", &m_RenderGraphShowTransient);
		ImGui::SameLine();
		ImGui::Checkbox("External", &m_RenderGraphShowExternal);
		ImGui::SameLine();
		ImGui::Checkbox("Graphics", &m_RenderGraphShowGraphics);
		ImGui::SameLine();
		ImGui::Checkbox("Compute", &m_RenderGraphShowCompute);

		if (ImGui::TreeNodeEx("Diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (snapshot.Diagnostics.empty())
			{
				ImGui::TextDisabled("No render graph diagnostics.");
			}
			else if (ImGui::BeginTable("##render_graph_diagnostics", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 78.0f);
				ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthFixed, 170.0f);
				ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 190.0f);
				ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 220.0f);
				ImGui::TableSetupColumn("Message");
				ImGui::TableHeadersRow();

				static constexpr RenderGraph::DiagnosticSeverity severityOrder[] = {
					RenderGraph::DiagnosticSeverity::Error,
					RenderGraph::DiagnosticSeverity::Warning,
					RenderGraph::DiagnosticSeverity::Info
				};

				for (RenderGraph::DiagnosticSeverity severity : severityOrder)
				{
					for (uint32_t diagnosticIndex = 0; diagnosticIndex < snapshot.Diagnostics.size(); diagnosticIndex++)
					{
						const auto& diagnostic = snapshot.Diagnostics[diagnosticIndex];
						if (diagnostic.Severity != severity || !RenderGraphDiagnosticVisible(diagnostic, m_RenderGraphErrorsOnly, m_RenderGraphShowWarnings))
							continue;

						if (!m_RenderGraphSearch.empty()
							&& !ImGuiEx::IsMatchingSearch(diagnostic.Message, m_RenderGraphSearch, false, false, true)
							&& !ImGuiEx::IsMatchingSearch(diagnostic.PassName, m_RenderGraphSearch, false, false, true)
							&& !ImGuiEx::IsMatchingSearch(diagnostic.ResourceName, m_RenderGraphSearch, false, false, true))
						{
							continue;
						}

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(RenderGraphDiagnosticColor(diagnostic.Severity), "%s", RenderGraphDiagnosticSeverityToString(diagnostic.Severity));
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(RenderGraphDiagnosticCodeToString(diagnostic.Code));
						ImGui::TableSetColumnIndex(2);
						if (diagnostic.PassIndex < snapshot.Passes.size())
						{
							if (ImGui::Selectable(RenderGraphPassIndexToString(snapshot, diagnostic.PassIndex).c_str(), m_RenderGraphSelectedPass == diagnostic.PassIndex, ImGuiSelectableFlags_SpanAllColumns))
								m_RenderGraphSelectedPass = diagnostic.PassIndex;
						}
						else
						{
							ImGui::TextDisabled("%s", diagnostic.PassName.empty() ? "-" : diagnostic.PassName.c_str());
						}
						ImGui::TableSetColumnIndex(3);
						if (diagnostic.Resource < snapshot.Textures.size())
						{
							const auto& texture = snapshot.Textures[diagnostic.Resource];
							if (ImGui::Selectable(std::format("#{} {}", diagnostic.Resource, texture.Name).c_str(), m_RenderGraphSelectedResource == diagnostic.Resource, ImGuiSelectableFlags_SpanAllColumns))
								m_RenderGraphSelectedResource = diagnostic.Resource;
						}
						else
						{
							ImGui::TextDisabled("%s", diagnostic.ResourceName.empty() ? "-" : diagnostic.ResourceName.c_str());
						}
						ImGui::TableSetColumnIndex(4);
						ImGui::TextWrapped("%s", diagnostic.Message.c_str());
					}
				}

				ImGui::EndTable();
			}
			ImGui::TreePop();
		}

		ImGui::Spacing();
		if (ImGui::BeginTable("##render_graph_pass_table", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX))
		{
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 180.0f);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 92.0f);
			ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 160.0f);
			ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Inputs");
			ImGui::TableSetupColumn("Outputs");
			ImGui::TableSetupColumn("Diagnostics", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableHeadersRow();

			for (uint32_t passIndex = 0; passIndex < snapshot.Passes.size(); passIndex++)
			{
				const auto& pass = snapshot.Passes[passIndex];
				if (!MatchesRenderGraphSearch(snapshot, pass, m_RenderGraphSearch))
					continue;
				if (m_RenderGraphErrorsOnly && !RenderGraphPassHasSeverity(snapshot, pass, RenderGraph::DiagnosticSeverity::Error))
					continue;
				if (!m_RenderGraphShowCulled && pass.Culled)
					continue;
				if (m_RenderGraphShowGraphics && (pass.Flags & static_cast<uint32_t>(RenderGraph::PassFlags::Graphics)) == 0)
					continue;
				if (m_RenderGraphShowCompute && (pass.Flags & static_cast<uint32_t>(RenderGraph::PassFlags::Compute)) == 0)
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", passIndex);
				ImGui::TableSetColumnIndex(1);
				const bool selected = m_RenderGraphSelectedPass == passIndex;
				if (RenderGraphPassHasSeverity(snapshot, pass, RenderGraph::DiagnosticSeverity::Error))
					ImGui::PushStyleColor(ImGuiCol_Text, RenderGraphDiagnosticColor(RenderGraph::DiagnosticSeverity::Error));
				else if (RenderGraphPassHasSeverity(snapshot, pass, RenderGraph::DiagnosticSeverity::Warning))
					ImGui::PushStyleColor(ImGuiCol_Text, RenderGraphDiagnosticColor(RenderGraph::DiagnosticSeverity::Warning));
				else
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
				if (ImGui::Selectable(pass.Name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					m_RenderGraphSelectedPass = passIndex;
				ImGui::PopStyleColor();
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(RenderGraphPassStatusToString(pass).c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(RenderGraphPassFlagsToString(pass.Flags).c_str());
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.3f ms", pass.CPUTime);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.3f ms", pass.GPUTime);
				ImGui::TableSetColumnIndex(6);
				DrawResourceList(snapshot, pass.Inputs);
				ImGui::TableSetColumnIndex(7);
				DrawResourceList(snapshot, pass.Outputs);
				ImGui::TableSetColumnIndex(8);
				ImGui::Text("%zu", pass.Diagnostics.size());
			}

			ImGui::EndTable();
		}

		ImGui::Spacing();
		if (ImGui::BeginTable("##render_graph_texture_table", 11, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX))
		{
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthFixed, 210.0f);
			ImGui::TableSetupColumn("Producer", ImGuiTableColumnFlags_WidthFixed, 190.0f);
			ImGui::TableSetupColumn("Consumers", ImGuiTableColumnFlags_WidthFixed, 240.0f);
			ImGui::TableSetupColumn("Lifetime", ImGuiTableColumnFlags_WidthFixed, 82.0f);
			ImGui::TableSetupColumn("Alias", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 120.0f);
			ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Validation", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableHeadersRow();

			for (const auto& texture : snapshot.Textures)
			{
				if (!m_RenderGraphSearch.empty() && !ImGuiEx::IsMatchingSearch(texture.Name, m_RenderGraphSearch, false, false, true))
					continue;
				if (m_RenderGraphErrorsOnly && texture.ErrorCount == 0)
					continue;
				if (!m_RenderGraphShowWarnings && texture.ErrorCount == 0 && texture.WarningCount > 0)
					continue;
				if (m_RenderGraphShowAliased && texture.AliasGroup == UINT32_MAX)
					continue;
				if (m_RenderGraphShowTransient && !texture.Transient)
					continue;
				if (m_RenderGraphShowExternal && texture.Transient)
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", texture.Resource);
				ImGui::TableSetColumnIndex(1);
				if (texture.ErrorCount > 0)
					ImGui::PushStyleColor(ImGuiCol_Text, RenderGraphDiagnosticColor(RenderGraph::DiagnosticSeverity::Error));
				else if (texture.WarningCount > 0)
					ImGui::PushStyleColor(ImGuiCol_Text, RenderGraphDiagnosticColor(RenderGraph::DiagnosticSeverity::Warning));
				else
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
				if (ImGui::Selectable(texture.Name.c_str(), m_RenderGraphSelectedResource == texture.Resource, ImGuiSelectableFlags_SpanAllColumns))
					m_RenderGraphSelectedResource = texture.Resource;
				ImGui::PopStyleColor();
				ImGui::TableSetColumnIndex(2);
				ImGui::TextWrapped("%s", RenderGraphPassIndexToString(snapshot, texture.FirstWriter).c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextWrapped("%s", RenderGraphConsumerListToString(snapshot, texture.Consumers).c_str());
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(LifetimeToString(texture).c_str());
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%s%s", AliasToString(texture.AliasGroup).c_str(), texture.AliasedNow ? " live" : "");
				ImGui::TableSetColumnIndex(6);
				ImGui::TextUnformatted(texture.Transient ? "Transient" : "External");
				ImGui::TableSetColumnIndex(7);
				ImGui::TextUnformatted(std::string(Utils::ImageFormatToString(texture.Format)).c_str());
				ImGui::TableSetColumnIndex(8);
				ImGui::Text("%u x %u, %s, mips %u, layers %u", texture.Width, texture.Height, TextureDimensionToString(texture.Dimension), texture.Mips, texture.Layers);
				ImGui::TableSetColumnIndex(9);
				ImGui::TextUnformatted(Utils::BytesToString(texture.EstimatedBytes).c_str());
				ImGui::TableSetColumnIndex(10);
				if (texture.ErrorCount > 0)
					ImGui::TextColored(RenderGraphDiagnosticColor(RenderGraph::DiagnosticSeverity::Error), "%u error(s)", texture.ErrorCount);
				else if (texture.WarningCount > 0)
					ImGui::TextColored(RenderGraphDiagnosticColor(RenderGraph::DiagnosticSeverity::Warning), "%u warning(s)", texture.WarningCount);
				else
					ImGui::TextDisabled("OK");
			}

			ImGui::EndTable();
		}

		ImGui::Spacing();
		if (ImGui::TreeNode("Alias Groups"))
		{
			if (snapshot.AliasGroups.empty())
			{
				ImGui::TextDisabled("No transient alias groups.");
			}
			else
			{
				for (const auto& aliasGroup : snapshot.AliasGroups)
				{
					const std::string label = std::format("Group #{}: {} texture(s), backing {}, saved {}", aliasGroup.AliasGroup, aliasGroup.Resources.size(), Utils::BytesToString(aliasGroup.BackingBytes), Utils::BytesToString(aliasGroup.SavedBytes));
					if (ImGui::TreeNode(label.c_str()))
					{
						ImGui::TextColored(aliasGroup.Compatible ? ImVec4(0.35f, 0.85f, 0.35f, 1.0f) : RenderGraphDiagnosticColor(RenderGraph::DiagnosticSeverity::Error),
							"%s",
							aliasGroup.Compatible ? "Compatible: same descriptor shape and non-overlapping lifetimes." : "Invalid: incompatible descriptors or overlapping lifetimes.");
						for (uint32_t resource : aliasGroup.Resources)
						{
							if (resource >= snapshot.Textures.size())
								continue;
							const auto& texture = snapshot.Textures[resource];
							if (ImGui::Selectable(std::format("#{} {} life {}", resource, texture.Name, LifetimeToString(texture)).c_str(), m_RenderGraphSelectedResource == resource))
								m_RenderGraphSelectedResource = resource;
						}
						ImGui::TreePop();
					}
				}
			}
			ImGui::TreePop();
		}

		ImGui::Spacing();
		if (m_RenderGraphSelectedPass < snapshot.Passes.size())
		{
			const auto& pass = snapshot.Passes[m_RenderGraphSelectedPass];
			if (ImGui::TreeNodeEx("Selected Pass", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("#%u %s", pass.Index, pass.Name.c_str());
				ImGui::TextDisabled("Status: %s, flags: %s, CPU %.3f ms, GPU %.3f ms", RenderGraphPassStatusToString(pass).c_str(), RenderGraphPassFlagsToString(pass.Flags).c_str(), pass.CPUTime, pass.GPUTime);
				ImGui::TextUnformatted("Inputs");
				DrawResourceList(snapshot, pass.Inputs);
				ImGui::TextUnformatted("Outputs");
				DrawResourceList(snapshot, pass.Outputs);
				if (!pass.Diagnostics.empty())
				{
					ImGui::TextUnformatted("Diagnostics");
					for (uint32_t diagnosticIndex : pass.Diagnostics)
					{
						if (diagnosticIndex >= snapshot.Diagnostics.size())
							continue;
						const auto& diagnostic = snapshot.Diagnostics[diagnosticIndex];
						ImGui::TextColored(RenderGraphDiagnosticColor(diagnostic.Severity), "%s %s: %s",
							RenderGraphDiagnosticSeverityToString(diagnostic.Severity),
							RenderGraphDiagnosticCodeToString(diagnostic.Code),
							diagnostic.Message.c_str());
					}
				}
				ImGui::TreePop();
			}
		}

		if (m_RenderGraphSelectedResource < snapshot.Textures.size())
		{
			const auto& texture = snapshot.Textures[m_RenderGraphSelectedResource];
			if (ImGui::TreeNodeEx("Selected Texture", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("#%u %s", texture.Resource, texture.Name.c_str());
				ImGui::TextDisabled("Format: %s, usage: %s, dimension: %s, size: %u x %u, mips: %u, layers: %u",
					std::string(Utils::ImageFormatToString(texture.Format)).c_str(),
					ImageUsageToString(texture.Usage),
					TextureDimensionToString(texture.Dimension),
					texture.Width,
					texture.Height,
					texture.Mips,
					texture.Layers);
				ImGui::TextDisabled("Lifetime: %s, first writer: %s, last reader: %s, alias: %s, live state: %s",
					LifetimeToString(texture).c_str(),
					RenderGraphPassIndexToString(snapshot, texture.FirstWriter).c_str(),
					RenderGraphPassIndexToString(snapshot, texture.LastReader).c_str(),
					AliasToString(texture.AliasGroup).c_str(),
					ResourceStateToString(texture.CurrentState).c_str());
				ImGui::TextDisabled("Consumers: %s", RenderGraphConsumerListToString(snapshot, texture.Consumers).c_str());
				ImGui::TextDisabled("Memory: %s, transient: %s, aliasable: %s, currently aliased: %s",
					Utils::BytesToString(texture.EstimatedBytes).c_str(),
					texture.Transient ? "yes" : "no",
					texture.AllowAlias ? "yes" : "no",
					texture.AliasedNow ? "yes" : "no");
				if (texture.DiagnosticCount > 0)
				{
					ImGui::TextUnformatted("Diagnostics");
					for (const auto& diagnostic : snapshot.Diagnostics)
					{
						if (diagnostic.Resource != texture.Resource)
							continue;
						ImGui::TextColored(RenderGraphDiagnosticColor(diagnostic.Severity), "%s %s: %s",
							RenderGraphDiagnosticSeverityToString(diagnostic.Severity),
							RenderGraphDiagnosticCodeToString(diagnostic.Code),
							diagnostic.Message.c_str());
					}
				}
				ImGui::TreePop();
			}
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

		// Sections are grouped into tabs so the panel is scanned by concern rather than scrolled as
		// one long column. Each section keeps its collapsing sub-header; closely-related sections
		// share a tab.
		if (ImGui::BeginTabBar("##renderer_debugger_tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
		{
			if (ImGui::BeginTabItem("Overview"))
			{
				DrawOverview(stats);
				ImGui::Spacing();
				DrawFrameHistoryPlot();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Profiling"))
			{
				DrawProfiling(stats);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Memory"))
			{
				DrawMemory(stats);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Render Graph"))
			{
				DrawRenderGraphInspector();
				DrawRenderPassIsolation();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("GPU Scene"))
			{
				DrawGPUScene();
				DrawWorkload(stats);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Shaders"))
			{
				DrawShaders();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

}
