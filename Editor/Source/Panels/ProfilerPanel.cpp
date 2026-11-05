// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ProfilerPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Lux {

	namespace {

		// A label/value chip inside a responsive table cell.
		void StatChip(const char* id, const char* label, const char* value, ImU32 valueColor)
		{
			const ImVec2 pad(ImGui::GetFontSize() * 0.6f, ImGui::GetFontSize() * 0.4f);
			const float innerGap = 2.0f;
			const ImVec2 labelSize = ImGui::CalcTextSize(label);
			const ImVec2 valueSize = ImGui::CalcTextSize(value);
			const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
			const float height = pad.y * 2.0f + labelSize.y + valueSize.y + innerGap;

			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton(id, ImVec2(width, height));
			const ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->PushClipRect(p0, p1, true);
			dl->AddRectFilled(p0, p1, Colors::Theme::groupHeader, 5.0f);
			dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), Colors::Theme::textDarker, label);
			dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y + labelSize.y + innerGap), valueColor, value);
			dl->PopClipRect();
			ImGui::SetItemTooltip("%s: %s", label, value);
		}

		// A labelled proportion bar: "name .... value" with a filled track behind it. Used for both the
		// CPU zone list and the GPU pass list so they read consistently.
		void TimingRow(const char* name, float ms, float maxMs, ImU32 barColor)
		{
			const float rowWidth = ImGui::GetContentRegionAvail().x;
			if (rowWidth < 1.0f)   // a zero/negative size_arg trips ImGui's InvisibleButton assert
				return;

			const float rowHeight = ImGui::GetFrameHeight();
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton(name, ImVec2(rowWidth, rowHeight));
			const ImVec2 p1 = ImVec2(p0.x + rowWidth, p0.y + rowHeight);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p0, p1, Colors::Theme::backgroundDark, 3.0f);

			const float fraction = maxMs > 0.0f ? std::clamp(ms / maxMs, 0.0f, 1.0f) : 0.0f;
			if (fraction > 0.0f)
				dl->AddRectFilled(p0, ImVec2(p0.x + rowWidth * fraction, p1.y), barColor, 3.0f);

			const float textY = p0.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
			dl->AddText(ImVec2(p0.x + 8.0f, textY), Colors::Theme::text, name);

			char valueBuffer[32];
			std::snprintf(valueBuffer, sizeof(valueBuffer), "%.3f ms", ms);
			const float valueWidth = ImGui::CalcTextSize(valueBuffer).x;
			dl->AddText(ImVec2(p1.x - valueWidth - 8.0f, textY), Colors::Theme::textBrighter, valueBuffer);
		}

		// A plain label/value row for a 2-column stretch table (counts, memory sizes).
		void MetricRow(const char* label, const std::string& value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "%s", label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value.c_str());
		}

	}

	void ProfilerPanel::SetContext(const Ref<SceneRenderer>& context)
	{
		if (m_Context != context)
			m_TimingPlot.Clear();
		m_Context = context;
	}

	const char* ProfilerPanel::GetWindowName() const
	{
		return LUX_ICON_TACHOMETER "  Profiler";
	}

	void ProfilerPanel::UI_CPUBreakdown(float cpuFrameMs)
	{
		Application& app = Application::Get();
		const Application::PerformanceTimers& timers = app.GetPerformanceTimers();

		if (!ImGui::CollapsingHeader(LUX_ICON_MICROCHIP "  CPU", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		struct NamedTime { const char* Name; float Ms; };
		const NamedTime coarse[] = {
			{ "Main Thread (work)",   timers.MainThreadWorkTime },
			{ "Main Thread (wait)",   timers.MainThreadWaitTime },
			{ "Render Thread (work)", timers.RenderThreadWorkTime },
			{ "Render Thread (wait)", timers.RenderThreadWaitTime },
			{ "Render Thread (GPU wait)", timers.RenderThreadGPUWaitTime },
			{ "Scripting Update",     timers.ScriptUpdate },
			{ "Physics Step",         timers.PhysicsStepTime },
		};

		float maxMs = cpuFrameMs;
		for (const NamedTime& t : coarse)
			maxMs = std::max(maxMs, t.Ms);

		for (const NamedTime& t : coarse)
		{
			const bool overBudget = &t == &coarse[0] && t.Ms > m_TimingPlot.BudgetMs;
			TimingRow(t.Name, t.Ms, maxMs, overBudget ? Colors::Theme::titlebarRed : Colors::Theme::selectionMuted);
			ImGui::Spacing();
		}

		// Named per-frame profiler zones (ScopePerfTimer), sorted by cost. These are engine-instrumented
		// zones (e.g. subsystem updates) surfaced without needing Tracy.
		const auto& zones = app.GetProfilerPreviousFrameData();
		if (!zones.empty())
		{
			std::vector<std::pair<const char*, float>> sorted;
			sorted.reserve(zones.size());
			for (const auto& [name, data] : zones)
				sorted.emplace_back(name, data.Time);
			std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

			ImGui::Spacing();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "Instrumented Zones");
			ImGui::Spacing();

			float zoneMax = 0.0f;
			for (const auto& [name, ms] : sorted)
				zoneMax = std::max(zoneMax, ms);

			for (const auto& [name, ms] : sorted)
			{
				TimingRow(name, ms, zoneMax, Colors::Theme::accentTabActive);
				ImGui::Spacing();
			}
		}
	}

	void ProfilerPanel::UI_GPUBreakdown()
	{
		if (!m_Context)
			return;

		const SceneRenderer::Statistics& stats = m_Context->GetStatistics();

		if (!ImGui::CollapsingHeader(LUX_ICON_BOLT "  GPU Passes", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		// Sort passes by GPU time, skipping inactive/zero-cost ones (RenderGraph skips disabled passes).
		std::vector<const SceneRenderer::PassProfile*> passes;
		passes.reserve(stats.PassProfiles.size());
		for (const SceneRenderer::PassProfile& p : stats.PassProfiles)
		{
			if (std::isfinite(p.GPUTime) && p.GPUTime > 0.0f)
				passes.push_back(&p);
		}
		std::sort(passes.begin(), passes.end(), [](const SceneRenderer::PassProfile* a, const SceneRenderer::PassProfile* b)
		{
			return a->GPUTime > b->GPUTime;
		});

		if (passes.empty())
		{
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "No GPU timings this frame.");
			return;
		}

		const float maxMs = passes.front()->GPUTime;
		for (const SceneRenderer::PassProfile* p : passes)
		{
			TimingRow(p->Name, p->GPUTime, maxMs, Colors::Theme::titlebarOrange);
			ImGui::Spacing();
		}

		if (ImGui::CollapsingHeader(LUX_ICON_BAR_CHART "  Pipeline Statistics"))
		{
			RenderCommandBuffer::RequestPipelineStatistics();
			ImGui::TextDisabled("Counted only while this section is open; counting slows mesh shaders.");
			const PipelineStatistics& ps = stats.PipelineStats;
			ImGui::Text("Input Vertices:       %llu", static_cast<unsigned long long>(ps.InputAssemblyVertices));
			ImGui::Text("Input Primitives:     %llu", static_cast<unsigned long long>(ps.InputAssemblyPrimitives));
			ImGui::Text("Vertex Shader Inv.:   %llu", static_cast<unsigned long long>(ps.VertexShaderInvocations));
			ImGui::Text("Fragment Shader Inv.: %llu", static_cast<unsigned long long>(ps.FragmentShaderInvocations));
			ImGui::Text("Compute Shader Inv.:  %llu", static_cast<unsigned long long>(ps.ComputeShaderInvocations));
			ImGui::Text("Clipping Primitives:  %llu", static_cast<unsigned long long>(ps.ClippingPrimitives));
		}
	}

	void ProfilerPanel::UI_SceneAndMemory()
	{
		if (!m_Context)
			return;

		const SceneRenderer::Statistics& stats = m_Context->GetStatistics();

		if (!ImGui::CollapsingHeader(LUX_ICON_CUBES "  Scene & Memory", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		if (ImGui::BeginTable("##profiler_scene", 2, ImGuiTableFlags_SizingStretchProp))
		{
			MetricRow("Draw Calls", std::to_string(stats.DrawCalls));
			MetricRow("Visible Instances", std::to_string(stats.VisibleInstances));
			MetricRow("GPU Visible", std::to_string(stats.GPUVisibleInstances));

			const SceneRenderer::Statistics::MemoryStatistics& memory = stats.MemoryStats;
			const std::string vram = memory.BudgetBytes > 0
				? Utils::BytesToString(memory.UsedBytes) + " / " + Utils::BytesToString(memory.BudgetBytes)
				: Utils::BytesToString(memory.UsedBytes);
			MetricRow("VRAM", vram);
			MetricRow("Textures", Utils::BytesToString(memory.TextureBytes));
			MetricRow("Buffers", Utils::BytesToString(memory.BufferBytes));
			MetricRow("Render Targets", Utils::BytesToString(memory.RenderTargetBytes));

			const RenderCommandQueueStats queue = Renderer::GetRenderCommandQueueStats();
			MetricRow("Render Commands", std::format("{} in {} ({} peak, {} page{})",
				queue.CommandCount, Utils::BytesToString(queue.Bytes), Utils::BytesToString(queue.MaxBytes),
				queue.MaxPageCount, queue.MaxPageCount == 1 ? "" : "s"));

			ImGui::EndTable();
		}
	}

	void ProfilerPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		ImGui::SetNextWindowSize({820.0f, 760.0f}, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetWindowName(), &isOpen))
		{
			ImGui::End();
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();
		const float frameMs = io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f;
		const float cpuMs = Application::Get().GetPerformanceTimers().MainThreadWorkTime;
		const float gpuMs = (m_Context && m_Context->IsReady()) ? m_Context->GetStatistics().TotalGPUTime : 0.0f;

		ImGuiEx::TimingPlotControls("profiler_history_controls", m_TimingPlot);
		m_TimingPlot.Record(cpuMs, gpuMs);
		ImGui::Spacing();
		ImGui::TextDisabled("Current frame (live)");

		// ---- Stat chips ----
		const ImU32 frameColor =
			frameMs > m_TimingPlot.BudgetMs ? Colors::Theme::textError : Colors::Theme::accent;
		const ImU32 cpuColor = cpuMs > m_TimingPlot.BudgetMs ? Colors::Theme::textError : Colors::Theme::text;
		char fpsBuf[24], frameBuf[24], cpuBuf[24], gpuBuf[24];
		std::snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", io.Framerate);
		std::snprintf(frameBuf, sizeof(frameBuf), "%.2f ms", frameMs);
		std::snprintf(cpuBuf, sizeof(cpuBuf), "%.2f ms", cpuMs);
		std::snprintf(gpuBuf, sizeof(gpuBuf), "%.2f ms", gpuMs);

		const int columns = std::clamp(
			static_cast<int>(ImGui::GetContentRegionAvail().x / (ImGui::GetFontSize() * 9.0f)), 1, 4);
		if (ImGui::BeginTable("profiler_live_metrics", columns, ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableNextColumn();
			StatChip("##chip_fps", "FPS", fpsBuf, frameColor);
			ImGui::TableNextColumn();
			StatChip("##chip_frame", "FRAME", frameBuf, frameColor);
			ImGui::TableNextColumn();
			StatChip("##chip_cpu", "CPU", cpuBuf, cpuColor);
			ImGui::TableNextColumn();
			StatChip("##chip_gpu", "GPU", std::isfinite(gpuMs) && gpuMs > 0.0f ? gpuBuf : "Unavailable",
					 Colors::Theme::titlebarOrange);
			ImGui::EndTable();
		}
		ImGui::Spacing();

		ImGuiEx::DrawTimingPlot("profiler_history", m_TimingPlot, "main-thread work");
		ImGui::Spacing();
		ImGui::Spacing();

		UI_CPUBreakdown(cpuMs);
		ImGui::Spacing();
		UI_GPUBreakdown();
		ImGui::Spacing();
		UI_SceneAndMemory();

		ImGui::End();
	}

}
