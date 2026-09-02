#include "lpch.h"
#include "ProfilerPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <vector>

namespace Lux {

	namespace {

		// A rounded "pill" showing a label over a value, sized to its content. Advances the cursor on
		// the same line so a row of chips reads as a compact stat strip.
		void StatChip(const char* id, const char* label, const std::string& value, ImU32 valueColor)
		{
			const ImVec2 pad(10.0f, 6.0f);
			const float innerGap = 2.0f;
			const ImVec2 labelSize = ImGui::CalcTextSize(label);
			const ImVec2 valueSize = ImGui::CalcTextSize(value.c_str());
			const float width = pad.x * 2.0f + std::max(labelSize.x, valueSize.x);
			const float height = pad.y * 2.0f + labelSize.y + valueSize.y + innerGap;

			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton(id, ImVec2(width, height));
			const ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p0, p1, Colors::Theme::groupHeader, 5.0f);
			dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), Colors::Theme::textDarker, label);
			dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y + labelSize.y + innerGap), valueColor, value.c_str());

			ImGui::SameLine(0.0f, 6.0f);
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

	void ProfilerPanel::PushFrameSample(float cpuMs, float gpuMs)
	{
		m_CPUHistory[m_HistoryHead] = cpuMs;
		m_GPUHistory[m_HistoryHead] = gpuMs;
		m_HistoryHead = (m_HistoryHead + 1) % kHistory;
		if (m_HistoryCount < kHistory)
			m_HistoryCount++;
	}

	void ProfilerPanel::UI_Graph(float cpuMs, float gpuMs)
	{
		const float availX = ImGui::GetContentRegionAvail().x;
		if (availX < 1.0f)   // a zero/negative size_arg trips ImGui's InvisibleButton assert
			return;

		const float height = 130.0f;
		const ImVec2 size(availX, height);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##profiler_graph", size);
		const ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p0, p1, Colors::Theme::backgroundDark, 4.0f);
		dl->AddRect(p0, p1, Colors::Theme::groupHeader, 4.0f);

		const int count = m_HistoryCount;
		if (count <= 1)
			return;

		// Oldest -> newest, so the newest sample sits at the right edge.
		const int oldest = (m_HistoryHead - count + kHistory) % kHistory;

		// Y scale: at least the budget line, with headroom above the worst recent spike.
		float peak = m_TargetMs * 1.25f;
		for (int i = 0; i < count; i++)
		{
			const int idx = (oldest + i) % kHistory;
			peak = std::max(peak, m_CPUHistory[idx]);
			peak = std::max(peak, m_GPUHistory[idx]);
		}
		peak *= 1.08f;

		auto yFor = [&](float ms) { return p1.y - std::clamp(ms / peak, 0.0f, 1.0f) * height; };

		// Budget reference line (dashed) + label.
		const float targetY = yFor(m_TargetMs);
		for (float x = p0.x; x < p1.x; x += 8.0f)
			dl->AddLine(ImVec2(x, targetY), ImVec2(std::min(x + 4.0f, p1.x), targetY), Colors::Theme::muted, 1.0f);

		char budgetLabel[48];
		std::snprintf(budgetLabel, sizeof(budgetLabel), "%.1f ms (%.0f fps)", m_TargetMs, 1000.0f / m_TargetMs);
		dl->AddText(ImVec2(p0.x + 6.0f, targetY - ImGui::GetTextLineHeight() - 1.0f), Colors::Theme::muted, budgetLabel);

		std::vector<ImVec2> cpuPoints, gpuPoints;
		cpuPoints.reserve(count);
		gpuPoints.reserve(count);
		for (int i = 0; i < count; i++)
		{
			const int idx = (oldest + i) % kHistory;
			const float x = p0.x + (size.x * i) / (count - 1);
			cpuPoints.emplace_back(x, yFor(m_CPUHistory[idx]));
			gpuPoints.emplace_back(x, yFor(m_GPUHistory[idx]));
		}

		dl->PushClipRect(p0, p1, true);
		dl->AddPolyline(gpuPoints.data(), static_cast<int>(gpuPoints.size()), Colors::Theme::titlebarOrange, 0, 1.8f);
		dl->AddPolyline(cpuPoints.data(), static_cast<int>(cpuPoints.size()), Colors::Theme::accent, 0, 1.8f);
		dl->PopClipRect();

		// Legend, bottom-left.
		const float legendY = p1.y - ImGui::GetTextLineHeight() - 4.0f;
		dl->AddText(ImVec2(p0.x + 6.0f, legendY), Colors::Theme::accent, "CPU");
		dl->AddText(ImVec2(p0.x + 40.0f, legendY), Colors::Theme::titlebarOrange, "GPU");
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
			const bool overBudget = &t == &coarse[0] && t.Ms > m_TargetMs;
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
			if (p.GPUActive || p.GPUTime > 0.0f)
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

			ImGui::EndTable();
		}
	}

	void ProfilerPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		if (!ImGui::Begin(LUX_ICON_TACHOMETER "  Profiler", &isOpen))
		{
			ImGui::End();
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();
		const float frameMs = io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f;
		const float cpuMs = Application::Get().GetPerformanceTimers().MainThreadWorkTime;
		const float gpuMs = (m_Context && m_Context->IsReady()) ? m_Context->GetStatistics().TotalGPUTime : 0.0f;

		if (!m_Paused)
			PushFrameSample(cpuMs, gpuMs);

		// ---- Controls ----
		ImGui::Checkbox("Pause", &m_Paused);
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker), "Budget");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		const char* budgets[] = { "30 fps", "60 fps", "120 fps", "144 fps" };
		const float budgetMs[] = { 1000.0f / 30.0f, 1000.0f / 60.0f, 1000.0f / 120.0f, 1000.0f / 144.0f };
		int budgetIndex = 1;
		for (int i = 0; i < IM_ARRAYSIZE(budgetMs); i++)
		{
			if (std::abs(m_TargetMs - budgetMs[i]) < 0.01f)
				budgetIndex = i;
		}
		if (ImGui::Combo("##profiler_budget", &budgetIndex, budgets, IM_ARRAYSIZE(budgets)))
			m_TargetMs = budgetMs[budgetIndex];

		ImGui::Spacing();

		// ---- Stat chips ----
		const ImU32 frameColor = frameMs > m_TargetMs ? Colors::Theme::textError : Colors::Theme::accent;
		const ImU32 cpuColor = cpuMs > m_TargetMs ? Colors::Theme::textError : Colors::Theme::text;
		char fpsBuf[24], frameBuf[24], cpuBuf[24], gpuBuf[24];
		std::snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", io.Framerate);
		std::snprintf(frameBuf, sizeof(frameBuf), "%.2f ms", frameMs);
		std::snprintf(cpuBuf, sizeof(cpuBuf), "%.2f ms", cpuMs);
		std::snprintf(gpuBuf, sizeof(gpuBuf), "%.2f ms", gpuMs);

		StatChip("##chip_fps", "FPS", fpsBuf, frameColor);
		StatChip("##chip_frame", "FRAME", frameBuf, frameColor);
		StatChip("##chip_cpu", "CPU", cpuBuf, cpuColor);
		StatChip("##chip_gpu", "GPU", gpuBuf, Colors::Theme::titlebarOrange);
		ImGui::NewLine();
		ImGui::Spacing();

		UI_Graph(cpuMs, gpuMs);
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
